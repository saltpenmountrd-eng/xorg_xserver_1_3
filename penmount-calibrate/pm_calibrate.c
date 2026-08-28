/*
 * pm_calibrate -- GTK2 touch-panel calibration tool for the "penmount"
 * X input driver, modeled on the classic ts_calibrate flow.
 *
 * Protocol with the driver (see xf86-input-penmount/penmount_axes.c and
 * penmount_calib.c in the sibling xf86-input-penmount/ tree):
 *
 *   1. Create /etc/penmount/CalibStart  -> driver stops applying any
 *      calibration and reports raw ADC counts (0..PM_CAL_RES domain is
 *      NOT yet applied either -- truly raw device counts) on this
 *      device's extended XInput 1.0 events.
 *   2. Show a full-screen crosshair sequence (4/9/16/25 points), reading
 *      the raw touch coordinate for each point via the classic X Input
 *      extension (XInput 1.0 -- this xserver predates XInput2/XIRawEvent).
 *   3. Solve the affine calibration matrix with a least-squares fit
 *      (pm_solve3x3, run once for the X row, once for the Y row).
 *   4. Write /etc/penmount/CalibData (see penmount_calib_format.h).
 *   5. Create /etc/penmount/CalibOK -> driver reloads the new matrix and
 *      deletes CalibOK itself once consumed.
 *   6. On exit (success, or ESC to abort) delete /etc/penmount/CalibStart
 *      -> driver resumes applying whatever calibration is currently
 *      valid (the new one on success; the previous one on abort, since
 *      an aborted run never writes CalibData/CalibOK at all).
 *
 * NOTE ON XInput 1.0 USAGE: this program talks to the legacy XInput 1.0
 * extension (<X11/extensions/XInput.h>), not XInput2/XIRawEvent, because
 * that is what this old xserver (and this driver) actually implements.
 * That header/ABI could not be verified against a real X11 SDK in the
 * environment this file was written in (no libXi/inputproto headers were
 * available there) -- treat the XOpenDevice/XSelectExtensionEvent/
 * DeviceMotionNotify plumbing below as the highest-risk part of this file
 * and compile+test it against the real target's X11 development headers
 * before relying on it.
 */

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "penmount_calib_format.h"

#define PM_MAX_K	5	/* 5x5 = 25 points, the largest grid supported */
#define PM_MAX_POINTS	(PM_MAX_K * PM_MAX_K)

typedef struct {
    int		k;		/* grid size: 2, 3, 4 or 5 -> k*k points */
    int		point;		/* index of the point currently being shown */
    int		n_points;	/* k*k, cached */

    double	calib_data[2 * PM_MAX_POINTS]; /* rawX,rawY pairs, row-major */

    gboolean	pressed;
    gboolean	have_sample;		/* TRUE once a real (non-guessed) sample has
					 * arrived for the current press */
    double	sample_x, sample_y;	/* latest raw sample while pressed */

    GtkWidget	*window;

    Display	*dpy;
    XDevice	*xdev;
    int		motion_type;
    int		press_type;
    int		release_type;

    int		screen_width, screen_height;

    gboolean	aborted;
    gboolean	finished;
    const char	*error;
} PmCalibCtx;

static PmCalibCtx ctx;

/* ------------------------------------------------------------------ */
/* Target point geometry, in the fixed "evdev resolution" domain that   */
/* both the calibration matrix and the driver's valuator axis range     */
/* use (PM_CAL_RES). Formula as specified for the calibration solver.   */
/* ------------------------------------------------------------------ */

static void
pm_target_point (int idx, int k, double *tx, double *ty)
{
    double p = 0.02;
    double res = (double) PM_CAL_RES;

    *tx = (p + (idx % k) * (1.0 - 2.0 * p) / (k - 1)) * res;
    *ty = (p + (idx / k) * (1.0 - 2.0 * p) / (k - 1)) * res;
}

/*
 * Map an evdev-resolution-domain point to a screen pixel for drawing the
 * crosshair. This mirrors PenmountConvert() in the driver, so the target
 * is drawn exactly where the driver would eventually place the OS
 * cursor for that same evdev-resolution coordinate.
 */
static void
pm_evdev_to_screen (double ex, double ey, int *sx, int *sy)
{
    *sx = (int) (ex / (double) PM_CAL_RES * ctx.screen_width);
    *sy = (int) (ey / (double) PM_CAL_RES * ctx.screen_height);
}

/* ------------------------------------------------------------------ */
/* Least-squares 3x3 solver, verbatim from spec.                        */
/* ------------------------------------------------------------------ */

static int
pm_solve3x3 (double A[3][3], double b[3], double x[3])
{
    double M[3][4];
    double t, f;
    int i, j, col, row, j2, pivot;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) M[i][j] = A[i][j];
        M[i][3] = b[i];
    }
    for (col = 0; col < 3; col++) {
        pivot = col;
        for (row = col + 1; row < 3; row++)
            if (M[row][col] * M[row][col] > M[pivot][col] * M[pivot][col]) pivot = row;
        if (pivot != col)
            for (j2 = 0; j2 < 4; j2++) {
                t = M[col][j2]; M[col][j2] = M[pivot][j2]; M[pivot][j2] = t;
            }
        if (M[col][col] * M[col][col] < 1e-20) return 0;
        for (row = col + 1; row < 3; row++) {
            f = M[row][col] / M[col][col];
            for (j2 = col; j2 < 4; j2++) M[row][j2] -= f * M[col][j2];
        }
    }
    for (i = 2; i >= 0; i--) {
        x[i] = M[i][3];
        for (j = i + 1; j < 3; j++) x[i] -= M[i][j] * x[j];
        x[i] /= M[i][i];
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Calibration file + control-flag helpers (mirrors penmount_calib.c). */
/* ------------------------------------------------------------------ */

static void
pm_ensure_calib_dir (void)
{
    mkdir (PM_CAL_DIR, 0755);
}

static gboolean
pm_write_calib_file (double ax, double ay, double az,
                      double bx, double by, double bz)
{
    penmountCalibFile f;
    FILE *fp;

    pm_ensure_calib_dir ();

    f.magic = PM_CAL_MAGIC;
    f.version = PM_CAL_VERSION;
    f.ax = ax; f.ay = ay; f.az = az;
    f.bx = bx; f.by = by; f.bz = bz;
    f.crc32 = pmCrc32 ((unsigned char *) &f, sizeof (f) - sizeof (f.crc32));

    fp = fopen (PM_CAL_FILE, "wb");
    if (!fp)
        return FALSE;

    if (fwrite (&f, 1, sizeof (f), fp) != sizeof (f)) {
        fclose (fp);
        return FALSE;
    }

    /*
     * Flush + fsync before returning: if power is lost right after this
     * write, we want either the complete old file or the complete new
     * one, never a half-written one that happens to pass size checks
     * but fail CRC (the driver would reject it either way, but syncing
     * here means we don't lose a previously-good calibration to a torn
     * write for no reason).
     */
    fflush (fp);
    fsync (fileno (fp));
    fclose (fp);

    fprintf (stderr, "[pm_calibrate] [debug] wrote %s: magic=0x%08lx version=%lu "
             "ax=%.6f ay=%.6f az=%.6f bx=%.6f by=%.6f bz=%.6f crc32=0x%08lx\n",
             PM_CAL_FILE, (unsigned long) f.magic, (unsigned long) f.version,
             f.ax, f.ay, f.az, f.bx, f.by, f.bz, (unsigned long) f.crc32);

    return TRUE;
}

static void
pm_signal_calib_start (void)
{
    FILE *fp;

    pm_ensure_calib_dir ();
    fp = fopen (PM_CAL_START, "w");
    if (fp)
        fclose (fp);
    fprintf (stderr, "[pm_calibrate] [debug] created %s (fp=%p)\n", PM_CAL_START, (void *) fp);
}

static void
pm_signal_calib_ok (void)
{
    FILE *fp = fopen (PM_CAL_OK, "w");
    if (fp)
        fclose (fp);
    fprintf (stderr, "[pm_calibrate] [debug] created %s (fp=%p)\n", PM_CAL_OK, (void *) fp);
}

static void
pm_clear_calib_start (void)
{
    int r = unlink (PM_CAL_START);
    fprintf (stderr, "[pm_calibrate] [debug] removed %s (result=%d, %s)\n",
             PM_CAL_START, r, r == 0 ? "ok" : strerror (errno));
}

/* ------------------------------------------------------------------ */
/* XInput 1.0 device lookup + raw event plumbing.                       */
/* ------------------------------------------------------------------ */

static gboolean
pm_open_xinput_device (const char *name_prefix)
{
    XDeviceInfo *list;
    int n, i;
    XID found_id = (XID) -1;
    XEventClass classes[3];
    int n_classes = 0;

    list = XListInputDevices (ctx.dpy, &n);
    if (!list) {
        g_printerr ("pm_calibrate: XListInputDevices failed.\n");
        return FALSE;
    }

    fprintf (stderr, "[pm_calibrate] [debug] %d XInput devices found (matching prefix \"%s\"):\n",
             n, name_prefix);
    for (i = 0; i < n; i++) {
        gboolean match = (g_ascii_strncasecmp (list[i].name, name_prefix, strlen (name_prefix)) == 0);
        fprintf (stderr, "[pm_calibrate] [debug]   #%d id=%lu name=\"%s\" use=%d%s\n",
                 i, (unsigned long) list[i].id, list[i].name, list[i].use,
                 match ? "  <-- MATCH" : "");
        if (match && found_id == (XID) -1)
            found_id = list[i].id;
    }
    XFreeDeviceList (list);

    if (found_id == (XID) -1) {
        g_printerr ("pm_calibrate: no XInput device with name prefix \"%s\" found.\n",
                     name_prefix);
        return FALSE;
    }

    ctx.xdev = XOpenDevice (ctx.dpy, found_id);
    if (!ctx.xdev) {
        g_printerr ("pm_calibrate: XOpenDevice failed for device id %lu.\n",
                     (unsigned long) found_id);
        return FALSE;
    }

    DeviceMotionNotify (ctx.xdev, ctx.motion_type, classes[n_classes]);
    if (ctx.motion_type) n_classes++;
    DeviceButtonPress (ctx.xdev, ctx.press_type, classes[n_classes]);
    if (ctx.press_type) n_classes++;
    DeviceButtonRelease (ctx.xdev, ctx.release_type, classes[n_classes]);
    if (ctx.release_type) n_classes++;

    fprintf (stderr, "[pm_calibrate] [debug] opened device id=%lu: motion_type=%d "
             "press_type=%d release_type=%d (n_classes=%d)\n",
             (unsigned long) found_id, ctx.motion_type, ctx.press_type,
             ctx.release_type, n_classes);

    if (n_classes == 0) {
        g_printerr ("pm_calibrate: device has no motion/button classes to select.\n");
        XCloseDevice (ctx.dpy, ctx.xdev);
        ctx.xdev = NULL;
        return FALSE;
    }

    if (XSelectExtensionEvent (ctx.dpy, GDK_WINDOW_XID (ctx.window->window),
                               classes, n_classes) != Success) {
        g_printerr ("pm_calibrate: XSelectExtensionEvent failed.\n");
        XCloseDevice (ctx.dpy, ctx.xdev);
        ctx.xdev = NULL;
        return FALSE;
    }

    return TRUE;
}

static void pm_advance_point (void);
static void pm_compute_and_save (void);

static void
pm_record_release (void)
{
    if (!ctx.pressed)
        return;

    ctx.pressed = FALSE;

    if (!ctx.have_sample) {
        /*
         * Never got a real sample for this press -- neither the press
         * event nor any motion event carried valid axis data (axes_count
         * was 0 on both, which can happen if this driver build predates
         * attaching position to button events, or if the touch was too
         * brief for even one ABS report to arrive). Recording (0,0) or a
         * stale value from a previous point here would silently corrupt
         * the fit -- retry this same point instead.
         */
        fprintf (stderr, "[pm_calibrate] [debug] point %d/%d RELEASE: NO VALID SAMPLE "
                 "received (press and motion both had axes_count=0) -- please touch "
                 "this point again\n", ctx.point + 1, ctx.n_points);
        return;
    }

    ctx.calib_data[ctx.point * 2]     = ctx.sample_x;
    ctx.calib_data[ctx.point * 2 + 1] = ctx.sample_y;

    fprintf (stderr, "[pm_calibrate] [debug] point %d/%d RELEASE: recorded raw=(%.1f,%.1f)\n",
             ctx.point + 1, ctx.n_points, ctx.sample_x, ctx.sample_y);

    ctx.point++;
    if (ctx.point >= ctx.n_points)
        pm_compute_and_save ();
    else
        pm_advance_point ();
}

static GdkFilterReturn
pm_event_filter (GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
    XEvent *xe = (XEvent *) xevent;

    if (xe->type == ctx.motion_type) {
        XDeviceMotionEvent *xdme = (XDeviceMotionEvent *) xe;
        if (ctx.pressed && xdme->axes_count >= 2) {
            static int pmMotionDbgCount = 0;
            ctx.sample_x = xdme->axis_data[0];
            ctx.sample_y = xdme->axis_data[1];
            ctx.have_sample = TRUE;
            if ((pmMotionDbgCount++ % 5) == 0)
                fprintf (stderr, "[pm_calibrate] [debug] point %d/%d MOTION: "
                         "first_axis=%d axes_count=%d raw=(%.1f,%.1f)\n",
                         ctx.point + 1, ctx.n_points, xdme->first_axis, xdme->axes_count,
                         ctx.sample_x, ctx.sample_y);
        }
        return GDK_FILTER_REMOVE;
    } else if (xe->type == ctx.press_type) {
        XDeviceButtonEvent *xdbe = (XDeviceButtonEvent *) xe;
        ctx.pressed = TRUE;
        ctx.have_sample = FALSE;
        if (xdbe->axes_count >= 2) {
            ctx.sample_x = xdbe->axis_data[0];
            ctx.sample_y = xdbe->axis_data[1];
            ctx.have_sample = TRUE;
        }
        fprintf (stderr, "[pm_calibrate] [debug] point %d/%d PRESS: button=%d "
                 "first_axis=%d axes_count=%d raw=(%.1f,%.1f)%s\n",
                 ctx.point + 1, ctx.n_points, xdbe->button, xdbe->first_axis,
                 xdbe->axes_count, (double) xdbe->axis_data[0], (double) xdbe->axis_data[1],
                 ctx.have_sample ? "" : "  (no axes on press -- waiting for motion)");
        return GDK_FILTER_REMOVE;
    } else if (xe->type == ctx.release_type) {
        pm_record_release ();
        return GDK_FILTER_REMOVE;
    }

    return GDK_FILTER_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Drawing + main flow.                                                  */
/* ------------------------------------------------------------------ */

static void
pm_show_error (const char *msg)
{
    GtkWidget *dlg = gtk_message_dialog_new (GTK_WINDOW (ctx.window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
    gtk_dialog_run (GTK_DIALOG (dlg));
    gtk_widget_destroy (dlg);
}

static void
pm_advance_point (void)
{
    gtk_widget_queue_draw (ctx.window);
}

static void
pm_compute_and_save (void)
{
    double AtA[3][3] = {{0}};
    double AtBx[3] = {0}, AtBy[3] = {0};
    double ax3[3], ay3[3];
    int idx, i, j;

    fprintf (stderr, "[pm_calibrate] [debug] solving from %d points (k=%d):\n",
             ctx.n_points, ctx.k);

    for (idx = 0; idx < ctx.n_points; idx++) {
        double rx = ctx.calib_data[idx * 2];
        double ry = ctx.calib_data[idx * 2 + 1];
        double tx, ty;
        double row[3];

        pm_target_point (idx, ctx.k, &tx, &ty);
        fprintf (stderr, "[pm_calibrate] [debug]   point %2d: target=(%7.1f,%7.1f) raw=(%7.1f,%7.1f)\n",
                 idx, tx, ty, rx, ry);

        row[0] = rx; row[1] = ry; row[2] = 1.0;
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++)
                AtA[i][j] += row[i] * row[j];
            AtBx[i] += row[i] * tx;
            AtBy[i] += row[i] * ty;
        }
    }

    if (!pm_solve3x3 (AtA, AtBx, ax3) || !pm_solve3x3 (AtA, AtBy, ay3)) {
        fprintf (stderr, "[pm_calibrate] [debug] pm_solve3x3 FAILED (singular matrix) -- "
                 "points are likely too close together, collinear, or all identical raw samples\n");
        ctx.error = "Calibration matrix could not be solved (points too close "
                    "together or collinear) -- please retry.";
        ctx.aborted = TRUE;
        gtk_main_quit ();
        return;
    }

    fprintf (stderr, "[pm_calibrate] [debug] solved: ax=%.6f ay=%.6f az=%.6f  "
             "bx=%.6f by=%.6f bz=%.6f\n",
             ax3[0], ax3[1], ax3[2], ay3[0], ay3[1], ay3[2]);

    /*
     * Residual check: re-apply the just-solved matrix to each raw sample
     * and compare against its own target. Large residuals here mean the
     * *fit itself* is bad (noisy/inconsistent raw samples, wrong axis
     * assignment, or too few/collinear points) -- as opposed to the
     * driver applying a good matrix incorrectly, which is a different
     * bug living in penmount_axes.c instead.
     */
    for (idx = 0; idx < ctx.n_points; idx++) {
        double rx = ctx.calib_data[idx * 2];
        double ry = ctx.calib_data[idx * 2 + 1];
        double tx, ty, fx, fy;

        pm_target_point (idx, ctx.k, &tx, &ty);
        fx = ax3[0] * rx + ax3[1] * ry + ax3[2];
        fy = ay3[0] * rx + ay3[1] * ry + ay3[2];
        fprintf (stderr, "[pm_calibrate] [debug]   residual point %2d: target=(%7.1f,%7.1f) "
                 "fit=(%7.1f,%7.1f) err=(%+6.1f,%+6.1f)\n",
                 idx, tx, ty, fx, fy, fx - tx, fy - ty);
    }

    if (!pm_write_calib_file (ax3[0], ax3[1], ax3[2], ay3[0], ay3[1], ay3[2])) {
        ctx.error = "Failed to write " PM_CAL_FILE ".";
        ctx.aborted = TRUE;
        gtk_main_quit ();
        return;
    }

    pm_signal_calib_ok ();

    ctx.finished = TRUE;
    gtk_main_quit ();
}

static gboolean
pm_on_expose (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
    cairo_t *cr = gdk_cairo_create (widget->window);
    double tx, ty;
    int sx, sy;
    char buf[64];

    /* Background */
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_paint (cr);

    if (ctx.point < ctx.n_points) {
        pm_target_point (ctx.point, ctx.k, &tx, &ty);
        pm_evdev_to_screen (tx, ty, &sx, &sy);

        cairo_set_source_rgb (cr, 1, 1, 1);
        cairo_set_line_width (cr, 2.0);

        cairo_move_to (cr, sx - 15, sy);
        cairo_line_to (cr, sx + 15, sy);
        cairo_move_to (cr, sx, sy - 15);
        cairo_line_to (cr, sx, sy + 15);
        cairo_stroke (cr);

        cairo_arc (cr, sx, sy, 6, 0, 2 * G_PI);
        cairo_stroke (cr);

        cairo_select_font_face (cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size (cr, 18);
        snprintf (buf, sizeof (buf), "Touch the crosshair  (%d / %d)   -- ESC to abort",
                  ctx.point + 1, ctx.n_points);
        cairo_move_to (cr, 20, 30);
        cairo_show_text (cr, buf);
    }

    cairo_destroy (cr);
    return TRUE;
}

static gboolean
pm_on_key (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    if (event->keyval == GDK_Escape) {
        ctx.aborted = TRUE;
        gtk_main_quit ();
        return TRUE;
    }
    return FALSE;
}

static void
pm_on_realize (GtkWidget *widget, gpointer data)
{
    gdk_window_add_filter (widget->window, pm_event_filter, NULL);
}

int
main (int argc, char **argv)
{
    const char *device_prefix = "PenMount";
    int points = 9;
    int i;
    GdkScreen *screen;

    gtk_init (&argc, &argv);

    for (i = 1; i < argc; i++) {
        if (!strcmp (argv[i], "--device") && i + 1 < argc)
            device_prefix = argv[++i];
        else if (!strcmp (argv[i], "--points") && i + 1 < argc)
            points = atoi (argv[++i]);
        else if (!strcmp (argv[i], "--help")) {
            g_print ("usage: %s [--device NAME-PREFIX] [--points 4|9|16|25]\n", argv[0]);
            return 0;
        }
    }

    memset (&ctx, 0, sizeof (ctx));
    switch (points) {
        case 4:  ctx.k = 2; break;
        case 16: ctx.k = 4; break;
        case 25: ctx.k = 5; break;
        case 9:
        default: ctx.k = 3; points = 9; break;
    }
    ctx.n_points = ctx.k * ctx.k;

    ctx.dpy = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
    screen = gdk_screen_get_default ();
    ctx.screen_width  = gdk_screen_get_width (screen);
    ctx.screen_height = gdk_screen_get_height (screen);

    fprintf (stderr, "[pm_calibrate] [debug] device_prefix=\"%s\" points=%d k=%d n_points=%d "
             "screen=%dx%d PM_CAL_RES=%d\n",
             device_prefix, points, ctx.k, ctx.n_points,
             ctx.screen_width, ctx.screen_height, PM_CAL_RES);

    ctx.window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    /*
     * gtk_window_fullscreen() only takes effect if a window manager is
     * running and honors the EWMH _NET_WM_STATE_FULLSCREEN request -- on
     * an embedded touch-panel target that boots straight into this one
     * app with no window manager at all, that request is silently
     * ignored and the window is left at GTK's own natural (small,
     * top-left) size instead of covering the screen. Setting the size
     * and position explicitly makes the window cover the full screen
     * either way: a real window manager still sees the fullscreen
     * request below and takes over cleanly, while a WM-less setup falls
     * back to exactly the geometry we asked for, since nothing else is
     * around to override it.
     */
    gtk_window_set_default_size (GTK_WINDOW (ctx.window), ctx.screen_width, ctx.screen_height);
    gtk_window_move (GTK_WINDOW (ctx.window), 0, 0);
    gtk_window_set_decorated (GTK_WINDOW (ctx.window), FALSE);
    gtk_window_fullscreen (GTK_WINDOW (ctx.window));
    gtk_widget_set_events (ctx.window, GDK_EXPOSURE_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect (ctx.window, "expose-event", G_CALLBACK (pm_on_expose), NULL);
    g_signal_connect (ctx.window, "key-press-event", G_CALLBACK (pm_on_key), NULL);
    g_signal_connect (ctx.window, "realize", G_CALLBACK (pm_on_realize), NULL);
    g_signal_connect (ctx.window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

    gtk_widget_show_all (ctx.window);
    /* Force realize so the GdkWindow (and its X window) exists before we
     * try to select extension events on it below. */
    while (gtk_events_pending ())
        gtk_main_iteration ();

    /*
     * Tell the driver to stop calibrating and report raw data BEFORE we
     * open/select on the XInput device, so there is no window where a
     * stray calibrated event could be misread as raw.
     */
    pm_signal_calib_start ();

    if (!pm_open_xinput_device (device_prefix)) {
        pm_clear_calib_start ();
        g_printerr ("pm_calibrate: could not attach to the penmount device, aborting.\n");
        return 1;
    }

    gtk_main ();

    if (ctx.xdev)
        XCloseDevice (ctx.dpy, ctx.xdev);

    /*
     * Always clear CalibStart on the way out, success or abort alike --
     * this is what tells the driver it may resume applying whatever
     * calibration is currently valid.
     */
    pm_clear_calib_start ();

    if (ctx.error)
        g_printerr ("pm_calibrate: %s\n", ctx.error);

    return ctx.finished ? 0 : 1;
}
