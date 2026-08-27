/*
 * PenMount calibration file and control-flag handling.
 *
 * The on-disk file format (magic/version/6 doubles/crc32) lives in
 * penmount_calib_format.h, shared verbatim with the pm_calibrate GUI --
 * see that header for the full layout.
 *
 * calib->valid is only ever set TRUE when the file exists, is exactly the
 * expected size, the magic/version match, and the CRC32 matches -- a
 * torn/partial write (e.g. power loss mid-save on embedded hardware)
 * leaves calib->valid FALSE and the driver falls back to its uncalibrated
 * default (see PenmountAxesAbsSynRep in penmount_axes.c).
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "penmount.h"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * Best-effort, one-shot: make sure /etc/penmount exists so a fresh image
 * that has never been calibrated doesn't silently fail every check below.
 * Errors (including EEXIST) are deliberately ignored -- if the directory
 * really can't be created, the fopen()/access() calls that follow will
 * fail on their own and calib->valid just stays FALSE.
 */
static void
pmEnsureCalibDir (void)
{
    mkdir (PM_CAL_DIR, 0755);
}

Bool
penmountCalibLoad (penmountCalibPtr calib)
{
    penmountCalibFile f;
    FILE *fp;
    size_t n;
    uint32_t crc;

    calib->valid = FALSE;

    pmEnsureCalibDir ();

    fp = fopen (PM_CAL_FILE, "rb");
    if (!fp)
	return FALSE;

    n = fread (&f, 1, sizeof (f), fp);
    fclose (fp);

    if (n != sizeof (f))
	return FALSE;

    if (f.magic != PM_CAL_MAGIC || f.version != PM_CAL_VERSION)
	return FALSE;

    crc = pmCrc32 ((unsigned char *) &f, sizeof (f) - sizeof (f.crc32));
    if (crc != f.crc32)
	return FALSE;

    calib->ax = f.ax; calib->ay = f.ay; calib->az = f.az;
    calib->bx = f.bx; calib->by = f.by; calib->bz = f.bz;
    calib->valid = TRUE;

    return TRUE;
}

Bool
penmountCalibCheckStart (void)
{
    return access (PM_CAL_START, F_OK) == 0;
}

/*
 * If /etc/penmount/CalibOK exists, reload the calibration file and delete
 * the flag file so it is only ever consumed once. Returns TRUE if a reload
 * was attempted (regardless of whether the reloaded file turned out to be
 * valid -- callers only need calib->valid for that).
 */
Bool
penmountCalibCheckAndConsumeOK (penmountCalibPtr calib)
{
    if (access (PM_CAL_OK, F_OK) != 0)
	return FALSE;

    penmountCalibLoad (calib);
    unlink (PM_CAL_OK);

    return TRUE;
}
