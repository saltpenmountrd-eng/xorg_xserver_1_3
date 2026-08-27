/*
 * Copyright © 2006 Zephaniah E. Hull
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Soft-
 * ware"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, provided that the above copyright
 * notice(s) and this permission notice appear in all copies of the Soft-
 * ware and that both the above copyright notice(s) and this permission
 * notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABIL-
 * ITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN
 * THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSE-
 * QUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFOR-
 * MANCE OF THIS SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization of
 * the copyright holder.
 *
 * Author:  Zephaniah E. Hull (warp@aehallh.com)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XIproto.h>

#include <string.h>

#include "penmount.h"

#include <xf86.h>

#include <xf86Module.h>
#include <mipointer.h>


#include <xf86_OSproc.h>

#undef DEBUG

static char *rel_axis_names[] = {
    "X",
    "Y",
    "Z",
    "RX",
    "RY",
    "RZ",
    "HWHEEL",
    "DIAL",
    "WHEEL",
    "MISC",
    "10",
    "11",
    "12",
    "13",
    "14",
    "15",
    NULL
};

static char *abs_axis_names[] = {
    "X",
    "Y",
    "Z",
    "RX",
    "RY",
    "RZ",
    "THROTTLE",
    "RUDDER",
    "WHEEL",
    "GAS",
    "BRAKE",
    "11",
    "12",
    "13",
    "14",
    "15",
    "HAT0X",
    "HAT0Y",
    "HAT1X",
    "HAT1Y",
    "HAT2X",
    "HAT2Y",
    "HAT3X",
    "HAT3Y",
    "PRESSURE",
    "TILT_X",
    "TILT_Y",
    "TOOL_WIDTH",
    "VOLUME",
    "29",
    "30",
    "31",
    "32",
    "33",
    "34",
    "35",
    "36",
    "37",
    "38",
    "39",
    "MISC",
    "41",
    "42",
    "43",
    "44",
    "45",
    "46",
    "47",
    "48",
    "49",
    "50",
    "51",
    "52",
    "53",
    "54",
    "55",
    "56",
    "57",
    "58",
    "59",
    "60",
    "61",
    "62",
    NULL
};

static void PenmountAxesTouchCallback (InputInfoPtr pInfo, int button, int value);

/*
 * conversion_proc: maps the fixed "evdev resolution" domain (0..PM_CAL_RES,
 * the space the calibration matrix targets) onto the current screen's
 * pixel resolution. This is deliberately the *only* place screen size is
 * consulted -- PenmountAxesAbsSynRep() and the calibration matrix itself
 * never need to know it, and re-reading screenInfo here on every motion
 * event (rather than caching it once at init) means a RandR resolution
 * change takes effect immediately without needing recalibration.
 *
 * Only touch-panel absolute mode is remapped this way; a plain relative
 * pointer sharing this same conversion_proc (see PenmountAxisRelNew0)
 * falls through to the old identity passthrough untouched.
 */
static Bool
PenmountConvert(InputInfoPtr pInfo, int first, int num, int v0, int v1, int v2,
	     int v3, int v4, int v5, int *x, int *y)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;

    if (first != 0)
        return FALSE;

    if (state->abs && state->mode == Absolute && state->abs->screen != -1) {
        int width  = screenInfo.screens[state->abs->screen]->width;
        int height = screenInfo.screens[state->abs->screen]->height;
        static int pmConvDbgCount = 0;

        *x = xf86ScaleAxis (v0, 0, width,  0, PM_CAL_RES);
        *y = xf86ScaleAxis (v1, 0, height, 0, PM_CAL_RES);

        if ((pmConvDbgCount++ % 15) == 0)
            xf86Msg(X_INFO, "%s: [calib-debug] PenmountConvert screen=%d %dx%d eres=(%d,%d) -> px=(%d,%d)\n",
                    pInfo->name, state->abs->screen, width, height, v0, v1, *x, *y);
    } else {
        *x = v0;
        *y = v1;
    }

    return TRUE;
}

static void
PenmountAxesRealSyn (InputInfoPtr pInfo, int absolute, int skip_xy)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    penmountAxesPtr axes = state->axes;
    int i;

#if DEBUG
    if (skip_xy == 2 && (axes->v[0] || axes->v[1]))
	xf86Msg(X_INFO, "%s: skip_xy: %d, x: %d, y: %d.\n", pInfo->name, skip_xy, axes->v[0], axes->v[1]);
#endif

    /* FIXME: This is a truly evil kluge. */
    if (skip_xy == 1 && state->axes->axes >= 2)
	xf86PostMotionEvent(pInfo->dev, absolute, 2,
	    state->axes->axes - 2,
	    axes->v[0x02], axes->v[0x03],
	    axes->v[0x04], axes->v[0x05], axes->v[0x06], axes->v[0x07],
	    axes->v[0x08], axes->v[0x09], axes->v[0x0a], axes->v[0x0b],
	    axes->v[0x0c], axes->v[0x0d], axes->v[0x0e], axes->v[0x0f],
	    axes->v[0x10], axes->v[0x11], axes->v[0x12], axes->v[0x13],
	    axes->v[0x14], axes->v[0x15], axes->v[0x16], axes->v[0x17],
	    axes->v[0x18], axes->v[0x19], axes->v[0x1a], axes->v[0x1b],
	    axes->v[0x1c], axes->v[0x1d], axes->v[0x1e], axes->v[0x1f],
	    axes->v[0x20], axes->v[0x21], axes->v[0x22], axes->v[0x23],
	    axes->v[0x24], axes->v[0x25], axes->v[0x26], axes->v[0x27],
	    axes->v[0x28], axes->v[0x29], axes->v[0x2a], axes->v[0x2b],
	    axes->v[0x2c], axes->v[0x2d], axes->v[0x2e], axes->v[0x2f],
	    axes->v[0x30], axes->v[0x31], axes->v[0x32], axes->v[0x33],
	    axes->v[0x34], axes->v[0x35], axes->v[0x36], axes->v[0x37],
	    axes->v[0x38], axes->v[0x39], axes->v[0x3a], axes->v[0x3b],
	    axes->v[0x3c], axes->v[0x3d], axes->v[0x3e], axes->v[0x3f]);
    else
	xf86PostMotionEvent(pInfo->dev, absolute, 0,
	    state->axes->axes,
	    axes->v[0x00], axes->v[0x01], axes->v[0x02], axes->v[0x03],
	    axes->v[0x04], axes->v[0x05], axes->v[0x06], axes->v[0x07],
	    axes->v[0x08], axes->v[0x09], axes->v[0x0a], axes->v[0x0b],
	    axes->v[0x0c], axes->v[0x0d], axes->v[0x0e], axes->v[0x0f],
	    axes->v[0x10], axes->v[0x11], axes->v[0x12], axes->v[0x13],
	    axes->v[0x14], axes->v[0x15], axes->v[0x16], axes->v[0x17],
	    axes->v[0x18], axes->v[0x19], axes->v[0x1a], axes->v[0x1b],
	    axes->v[0x1c], axes->v[0x1d], axes->v[0x1e], axes->v[0x1f],
	    axes->v[0x20], axes->v[0x21], axes->v[0x22], axes->v[0x23],
	    axes->v[0x24], axes->v[0x25], axes->v[0x26], axes->v[0x27],
	    axes->v[0x28], axes->v[0x29], axes->v[0x2a], axes->v[0x2b],
	    axes->v[0x2c], axes->v[0x2d], axes->v[0x2e], axes->v[0x2f],
	    axes->v[0x30], axes->v[0x31], axes->v[0x32], axes->v[0x33],
	    axes->v[0x34], axes->v[0x35], axes->v[0x36], axes->v[0x37],
	    axes->v[0x38], axes->v[0x39], axes->v[0x3a], axes->v[0x3b],
	    axes->v[0x3c], axes->v[0x3d], axes->v[0x3e], axes->v[0x3f]);

    if (!skip_xy)
	for (i = 0; i < ABS_MAX; i++)
	    state->axes->v[i] = 0;
    else if (skip_xy == 1)
	for (i = 2; i < ABS_MAX; i++)
	    state->axes->v[i] = 0;
    else if (skip_xy == 2)
	for (i = 0; i < 2; i++)
	    state->axes->v[i] = 0;
}

static void
PenmountAxesAbsSynCfg (InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    struct input_absinfo absinfo;
    int i;

    for (i = 0; i < ABS_MAX; i++) {
	if (!test_bit (i, pPenmount->bits.abs))
	    continue;

	if (ioctl (pInfo->fd, EVIOCGABS(i), &absinfo) < 0) {
	    xf86Msg(X_ERROR, "ioctl EVIOCGABS (%d) failed: %s\n", i, strerror(errno));
	    continue;
	}
	state->abs->min[state->abs->map[i]] = absinfo.minimum;
	state->abs->max[state->abs->map[i]] = absinfo.maximum;
    }

}

static void
PenmountAxesAbsSynRep (InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    int i = 0;
    Bool skip_xy = 0;

    if (!state->axes || !state->abs || !state->abs->count)
	return;

    if (state->mode == Relative && state->abs->axes >= 2) {
	if (!state->abs->use_touch || state->abs->touch) {
	    if (state->abs->reset_x && state->abs->v[0] != state->abs->old_x) {
		state->axes->v[0] = 0;
		state->abs->reset_x = 0;
#if DEBUG
		xf86Msg(X_INFO, "%s: Resetting X.\n", pInfo->name);
#endif
	    } else
		state->axes->v[0] = state->abs->v[0] - state->abs->old_x;

	    if (state->abs->reset_y && state->abs->v[1] != state->abs->old_y) {
		state->axes->v[1] = 0;
		state->abs->reset_y = 0;
#if DEBUG
		xf86Msg(X_INFO, "%s: Resetting Y.\n", pInfo->name);
#endif
	    } else
		state->axes->v[1] = state->abs->v[1] - state->abs->old_y;

	    state->abs->old_x = state->abs->v[0];
	    state->abs->old_y = state->abs->v[1];
	    PenmountAxesRealSyn (pInfo, 0, 2);
	}
	skip_xy = 1;
    } else if (state->mode == Absolute && state->abs->screen != -1 && state->abs->axes >= 2) {
	int conv_x, conv_y;
	int rawX = state->abs->v[0];
	int rawY = state->abs->v[1];
	int calX, calY;
	const char *pmMode;
	static Bool pmLastRawMode = FALSE;
	Bool pmRawModeNow;
	static int pmDbgCount = 0;

	/*
	 * A finished calibration run wins over "still calibrating": pick up
	 * the fresh matrix before deciding whether raw-report mode still
	 * applies below. penmountCalibCheckAndConsumeOK() deletes CalibOK
	 * itself once consumed, so this only actually reloads once.
	 */
	if (penmountCalibCheckAndConsumeOK (&pPenmount->calib))
	    xf86Msg(X_INFO, "%s: [calib-debug] CalibOK consumed -> reloaded, valid=%d "
		    "ax=%.6f ay=%.6f az=%.6f bx=%.6f by=%.6f bz=%.6f\n",
		    pInfo->name, pPenmount->calib.valid,
		    pPenmount->calib.ax, pPenmount->calib.ay, pPenmount->calib.az,
		    pPenmount->calib.bx, pPenmount->calib.by, pPenmount->calib.bz);

	pmRawModeNow = penmountCalibCheckStart ();
	if (pmRawModeNow != pmLastRawMode) {
	    xf86Msg(X_INFO, "%s: [calib-debug] raw-report mode %s\n", pInfo->name,
		    pmRawModeNow ? "ENTERED (CalibStart present)" : "EXITED (CalibStart gone)");
	    pmLastRawMode = pmRawModeNow;
	}

	if (pmRawModeNow) {
	    /*
	     * Calibration program is running: report untouched raw ADC
	     * counts (via this device's extended DeviceValuator events,
	     * which always carry state->axes->v[] as-is -- PenmountConvert
	     * below only affects the core-pointer cursor position) so the
	     * calibration program can see genuine raw data.
	     */
	    calX = rawX;
	    calY = rawY;
	    pmMode = "RAW";
	} else if (pPenmount->calib.valid) {
	    double x = pPenmount->calib.ax * rawX + pPenmount->calib.ay * rawY + pPenmount->calib.az;
	    double y = pPenmount->calib.bx * rawX + pPenmount->calib.by * rawY + pPenmount->calib.bz;

	    if (x < 0) x = 0;
	    else if (x > PM_CAL_RES) x = PM_CAL_RES;
	    if (y < 0) y = 0;
	    else if (y > PM_CAL_RES) y = PM_CAL_RES;

	    calX = (int) x;
	    calY = (int) y;
	    pmMode = "CALIBRATED";
	} else {
	    /*
	     * No valid calibration on disk yet: fall back to a naive linear
	     * stretch of the panel's own reported native range into the
	     * same fixed evdev-resolution domain, so the pointer is at
	     * least roughly usable before the panel has ever been
	     * calibrated.
	     */
	    calX = xf86ScaleAxis (rawX, 0, PM_CAL_RES, state->abs->min[0], state->abs->max[0]);
	    calY = xf86ScaleAxis (rawY, 0, PM_CAL_RES, state->abs->min[1], state->abs->max[1]);
	    pmMode = "FALLBACK-STRETCH";
	}

	/*
	 * Throttled: one line roughly every 15 samples so a held touch
	 * during dragging doesn't flood Xorg.log, but you still get a live
	 * trickle of raw -> calibrated values to compare against what
	 * pm_calibrate reports on its side.
	 */
	if ((pmDbgCount++ % 15) == 0)
	    xf86Msg(X_INFO, "%s: [calib-debug] mode=%s raw=(%d,%d) min=(%d,%d) max=(%d,%d) -> cal=(%d,%d)\n",
		    pInfo->name, pmMode, rawX, rawY,
		    state->abs->min[0], state->abs->min[1],
		    state->abs->max[0], state->abs->max[1],
		    calX, calY);

	state->axes->v[0] = calX;
	state->axes->v[1] = calY;
	i = 2;

	PenmountConvert (pInfo, 0, 2, state->axes->v[0], state->axes->v[1],
		0, 0, 0, 0, &conv_x, &conv_y);
	xf86XInputSetScreen (pInfo, state->abs->screen, conv_x, conv_y);
    }

    for (; i < ABS_MAX; i++)
	state->axes->v[i] = state->abs->v[i];

    PenmountAxesRealSyn (pInfo, 1, skip_xy);
    state->abs->count = 0;
}

static void
PenmountAxesRelSynRep (InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    penmountRelPtr rel = state->rel;
    int i, btn;

    if (!state->axes || !state->rel || !state->rel->count)
	return;

    for (i = 0; i < REL_MAX; i++) {
	if (rel->btnMap[i][0] || rel->btnMap[i][1]) {
	    if ((rel->v[i] > 0) && (btn = rel->btnMap[i][0]))
		PenmountBtnPostFakeClicks (pInfo, btn, rel->v[i]);
	    else if ((rel->v[i] < 0) && (btn = rel->btnMap[i][1]))
		PenmountBtnPostFakeClicks (pInfo, btn, -rel->v[i]);
	}

	state->axes->v[i] = rel->v[i];
	rel->v[i] = 0;
    }

    PenmountAxesRealSyn (pInfo, 0, 0);
    rel->count = 0;
}

void
PenmountAxesSynRep (InputInfoPtr pInfo)
{
    PenmountAxesAbsSynRep (pInfo);
    PenmountAxesRelSynRep (pInfo);
}

void
PenmountAxesSynCfg (InputInfoPtr pInfo)
{
    PenmountAxesAbsSynCfg (pInfo);
/*    PenmountAxesRelSynCfg (pInfo);*/
}

void
PenmountAxesAbsProcess (InputInfoPtr pInfo, struct input_event *ev)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    int map;

    if (ev->code >= ABS_MAX)
	return;

    /* FIXME: Handle inverted axes properly. */
    map = state->abs->map[ev->code];
    if (map >= 0)
	state->abs->v[map] = ev->value;
    else
	state->abs->v[-map] = ev->value;

    state->abs->count++;

    if (!state->sync)
	PenmountAxesAbsSynRep (pInfo);
}

void
PenmountAxesRelProcess (InputInfoPtr pInfo, struct input_event *ev)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    int map;

    if (ev->code >= REL_MAX)
	return;

    map = state->rel->map[ev->code];
    if (map >= 0)
	state->rel->v[map] += ev->value;
    else
	state->rel->v[-map] -= ev->value;

    state->rel->count++;

    if (!state->sync)
	PenmountAxesRelSynRep (pInfo);
}

int
PenmountAxesOn (DeviceIntPtr device)
{
    return Success;
}

int
PenmountAxesOff (DeviceIntPtr device)
{
    return Success;
}

static int
PenmountAxisAbsNew0(InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    struct input_absinfo absinfo;
    char option[64];
    int i, j, k = 0, real_axes;

    real_axes = 0;
    for (i = 0; i < ABS_MAX; i++)
	if (test_bit (i, pPenmount->bits.abs))
	    real_axes++;

    if (!real_axes)
	return !Success;

    state->abs = Xcalloc (sizeof (penmountAbsRec));

    xf86Msg(X_INFO, "%s: Found %d absolute axes.\n", pInfo->name, real_axes);
    xf86Msg(X_INFO, "%s: Configuring as pointer.\n", pInfo->name);
    pInfo->flags |= XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS |
	XI86_CONFIGURED;
    pInfo->type_name = XI_MOUSE;
    pInfo->conversion_proc = PenmountConvert;

    for (i = 0, j = 0; i < ABS_MAX; i++) {
	if (!test_bit (i, pPenmount->bits.abs))
	    continue;

	snprintf(option, sizeof(option), "%sAbsoluteAxisMap", abs_axis_names[i]);
	k = xf86SetIntOption(pInfo->options, option, -1);
	if (k != -1)
	    state->abs->map[i] = k;
	else
	    state->abs->map[i] = j;

	if (k != -1)
	    xf86Msg(X_CONFIG, "%s: %s: %d.\n", pInfo->name, option, k);

	if (ioctl (pInfo->fd, EVIOCGABS(i), &absinfo) < 0) {
	    xf86Msg(X_ERROR, "ioctl EVIOCGABS failed: %s\n", strerror(errno));
	    return !Success;
	}
	state->abs->min[state->abs->map[i]] = absinfo.minimum;
	state->abs->max[state->abs->map[i]] = absinfo.maximum;

	j++;
    }

    state->abs->axes = real_axes;
    for (i = 0; i < ABS_MAX; i++) {
	if (state->abs->map[i] > state->abs->axes)
	    state->abs->axes = state->abs->map[i];
    }

    return Success;
}

static int
PenmountAxisAbsNew1(InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    char *s;
    int k = 0;

    if (!state->abs)
	return !Success;

    xf86Msg(X_CONFIG, "%s: Configuring %d absolute axes.\n", pInfo->name,
	    state->abs->axes);

    {
	int btn;

	s = xf86SetStrOption(pInfo->options, "AbsoluteTouch", "DIGI_Touch");
	btn = PenmountBtnFind (pInfo, s);
	if (btn != -1) {
	    if (PenmountBtnExists (pInfo, btn)) {
		state->abs->use_touch = 1;
		xf86Msg(X_ERROR, "%s: Button: %d.\n", pInfo->name, btn);
		xf86Msg(X_ERROR, "%s: state->btn: %p.\n", pInfo->name, state->btn);
		state->btn->callback[btn] = &PenmountAxesTouchCallback;
	    } else {
		xf86Msg(X_ERROR, "%s: AbsoluteTouch: '%s' does not exist.\n", pInfo->name, s);
	    }
	} else {
	    xf86Msg(X_ERROR, "%s: AbsoluteTouch: '%s' is not a valid button name.\n", pInfo->name, s);
	}
    }

    s = xf86SetStrOption(pInfo->options, "Mode", "Absolute");
    if (!strcasecmp(s, "Absolute")) {
	state->mode = Absolute;
	xf86Msg(X_CONFIG, "%s: Configuring in %s mode.\n", pInfo->name, s);
    } else if (!strcasecmp(s, "Relative")) {
	state->mode = Relative;
	xf86Msg(X_CONFIG, "%s: Configuring in %s mode.\n", pInfo->name, s);
    } else {
	state->mode = Absolute;
	xf86Msg(X_CONFIG, "%s: Unknown Mode: %s.\n", pInfo->name, s);
    }

    if (test_bit (ABS_X, pPenmount->bits.abs) && test_bit (ABS_Y, pPenmount->bits.abs))
	k = xf86SetIntOption(pInfo->options, "AbsoluteScreen", 0);
    else
	k = xf86SetIntOption(pInfo->options, "AbsoluteScreen", -1);
    if (k < screenInfo.numScreens && k >= 0) {
	state->abs->screen = k;
	xf86Msg(X_CONFIG, "%s: AbsoluteScreen: %d.\n", pInfo->name, k);
    } else {
	if (k != -1)
	    xf86Msg(X_CONFIG, "%s: AbsoluteScreen: %d is not a valid screen.\n", pInfo->name, k);
	state->abs->screen = -1;
    }

    return Success;
}

static int
PenmountAxisRelNew0(InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    char *s, option[64];
    int i, j, k = 0, real_axes;

    real_axes = 0;
    for (i = 0; i < REL_MAX; i++)
	if (test_bit (i, pPenmount->bits.rel))
	    real_axes++;

    if (!real_axes && (!state->abs || state->abs->axes < 2))
	return !Success;

    state->rel = Xcalloc (sizeof (penmountRelRec));

    xf86Msg(X_INFO, "%s: Found %d relative axes.\n", pInfo->name,
	    real_axes);
    xf86Msg(X_INFO, "%s: Configuring as pointer.\n", pInfo->name);
    pInfo->flags |= XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS |
	XI86_CONFIGURED;
    pInfo->type_name = XI_MOUSE;
    pInfo->conversion_proc = PenmountConvert;

    for (i = 0, j = 0; i < REL_MAX; i++) {
	if (!test_bit (i, pPenmount->bits.rel))
	    continue;

	snprintf(option, sizeof(option), "%sRelativeAxisMap", rel_axis_names[i]);
	s = xf86SetStrOption(pInfo->options, option, "0");
	if (s && (k = strtol(s, NULL, 0)))
	    state->rel->map[i] = k;
	else
	    state->rel->map[i] = j;

	if (s && k)
	    xf86Msg(X_CONFIG, "%s: %s: %d.\n", pInfo->name, option, k);


	snprintf(option, sizeof(option), "%sRelativeAxisButtons", rel_axis_names[i]);
	if (i == REL_WHEEL || i == REL_Z)
	    s = xf86SetStrOption(pInfo->options, option, "4 5");
	else if (i == REL_HWHEEL)
	    s = xf86SetStrOption(pInfo->options, option, "6 7");
	else
	    s = xf86SetStrOption(pInfo->options, option, "0 0");

	k = state->rel->map[i];

	if (!s || (sscanf(s, "%d %d", &state->rel->btnMap[k][0],
			&state->rel->btnMap[k][1]) != 2))
	    state->rel->btnMap[k][0] = state->rel->btnMap[k][1] = 0;

	if (state->rel->btnMap[k][0] || state->rel->btnMap[k][1])
	    xf86Msg(X_CONFIG, "%s: %s: %d %d.\n", pInfo->name, option,
		    state->rel->btnMap[k][0], state->rel->btnMap[k][1]);

	j++;
    }

    state->rel->axes = real_axes;
    for (i = 0; i < REL_MAX; i++)
	if (state->rel->map[i] > state->rel->axes)
	    state->rel->axes = state->rel->map[i];

    if (state->abs && (state->abs->axes >= 2) && (state->rel->axes < 2))
	state->rel->axes += 2;

    return Success;
}

static int
PenmountAxisRelNew1(InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;

    if (!state->rel)
	return !Success;

    xf86Msg(X_CONFIG, "%s: Configuring %d relative axes.\n", pInfo->name,
	    state->rel->axes);

    return Success;
}

int
PenmountAxesNew0 (InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    int ret = Success;

    state->axes = Xcalloc (sizeof (penmountAxesRec));
    if (PenmountAxisAbsNew0(pInfo) != Success)
	ret = !Success;
    if (PenmountAxisRelNew0(pInfo) != Success)
	ret = !Success;
    if (!state->abs && !state->rel) {
	Xfree (state->axes);
	state->axes = NULL;
    }

    return ret;
}

int
PenmountAxesNew1 (InputInfoPtr pInfo)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    int ret = Success;

    state->axes = Xcalloc (sizeof (penmountAxesRec));
    if (PenmountAxisAbsNew1(pInfo) != Success)
	ret = !Success;
    if (PenmountAxisRelNew1(pInfo) != Success)
	ret = !Success;
    if (!state->abs && !state->rel) {
	Xfree (state->axes);
	state->axes = NULL;
    }

    return ret;
}


static void
PenmountPtrCtrlProc(DeviceIntPtr device, PtrCtrl *ctrl)
{
    /* Nothing to do, dix handles all settings */
}

int
PenmountAxesInit (DeviceIntPtr device)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;
    int i, axes = 0;

    if (state->abs && state->abs->axes > axes)
	axes = state->abs->axes;
    if (state->rel && state->rel->axes > axes)
	axes = state->rel->axes;

    state->axes->axes = axes;

    xf86Msg(X_CONFIG, "%s: %d valuators.\n", pInfo->name,
	    axes);
    if (!axes)
	return Success;

    if (!InitValuatorClassDeviceStruct(device, axes,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 1
                                       GetMotionHistory,
                                       GetMotionHistorySize(),
#else
                                       miPointerGetMotionEvents,
                                       miPointerGetMotionBufferSize(),
#endif
                                       0))
        return !Success;

    for (i = 0; i < axes; i++) {
	/*
	 * Axes 0/1 (X/Y) on an absolute touch panel are registered in the
	 * fixed "evdev resolution" domain (see PM_CAL_RES) that the
	 * calibration matrix targets, not in screen pixels -- the
	 * conversion_proc (PenmountConvert) is what maps that onto the
	 * current screen size. Any other axis keeps the previous
	 * driver-decided default.
	 */
	if (i < 2 && state->abs && state->mode == Absolute)
	    xf86InitValuatorAxisStruct(device, i, 0, PM_CAL_RES, 0, 0, 1);
	else
	    xf86InitValuatorAxisStruct(device, i, 0, -1, 0, 0, 1);
	xf86InitValuatorDefaults(device, i);
    }

    if (!InitPtrFeedbackClassDeviceStruct(device, PenmountPtrCtrlProc))
        return !Success;

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
    xf86MotionHistoryAllocate (pInfo);
#endif

    return Success;
}

static void
PenmountAxesTouchCallback (InputInfoPtr pInfo, int button, int value)
{
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;

#if DEBUG
    xf86Msg(X_INFO, "%s: Touch callback; %d.\n", pInfo->name, value);
#endif
    if (state->abs->use_touch) {
	state->abs->touch = !!value;
	if (value)
	    state->abs->reset_x = state->abs->reset_y = 1;
    }
}
