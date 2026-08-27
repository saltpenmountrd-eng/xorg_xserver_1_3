/*
 * Shared PenMount calibration file format, used identically by the
 * "penmount" X input driver (penmount_calib.c) and the standalone
 * pm_calibrate GTK2 calibration program (penmount-calibrate/pm_calibrate.c).
 *
 * IMPORTANT: this header is intentionally duplicated verbatim in both
 * source trees (they are two separate build systems: an autotools-based
 * X driver module and a plain-Makefile GTK2 application) rather than
 * shared via a build-time include path. If you change this format, copy
 * the updated file over both copies -- do not let them drift apart, or
 * calibrations written by one side will silently fail CRC validation on
 * the other.
 *
 * File: /etc/penmount/CalibData, fixed-size binary, native byte order and
 * alignment (both programs always run on the very same target machine,
 * so no endianness/cross-arch portability is needed):
 *
 *   offset  size  field
 *   0       4     magic    = PM_CAL_MAGIC ("PMC1")
 *   4       4     version  = PM_CAL_VERSION
 *   8       8     ax  (double)
 *   16      8     ay  (double)
 *   24      8     az  (double)
 *   32      8     bx  (double)
 *   40      8     by  (double)
 *   48      8     bz  (double)
 *   56      4     crc32 over bytes [0, 56)
 *   ----------------------------------------
 *   total: 60 bytes, packed (no implicit padding)
 *
 * calX = ax*rawX + ay*rawY + az
 * calY = bx*rawX + by*rawY + bz
 *
 * calib.valid is only ever set TRUE when the file exists, is exactly the
 * expected size, magic/version match, and the CRC32 matches.
 */

#ifndef PENMOUNT_CALIB_FORMAT_H_
#define PENMOUNT_CALIB_FORMAT_H_

#include <stdint.h>
#include <stddef.h>

#define PM_CAL_DIR	"/etc/penmount"
#define PM_CAL_FILE	PM_CAL_DIR "/CalibData"
#define PM_CAL_START	PM_CAL_DIR "/CalibStart"
#define PM_CAL_OK	PM_CAL_DIR "/CalibOK"

/* Fixed "evdev resolution" domain the calibration matrix targets; the
 * driver's conversion_proc maps this onto the current screen resolution,
 * and this calibration program's crosshair placement does the same for
 * drawing. Must match PM_CAL_RES in penmount.h. */
#define PM_CAL_RES	4095

#define PM_CAL_MAGIC	0x504D4331UL	/* "PMC1" */
#define PM_CAL_VERSION	1UL

typedef struct {
    uint32_t	magic;
    uint32_t	version;
    double	ax, ay, az;
    double	bx, by, bz;
    uint32_t	crc32;
} __attribute__((packed)) penmountCalibFile;

static __inline__ uint32_t
pmCrc32 (const unsigned char *buf, size_t len)
{
    static uint32_t table[256];
    static int have_table = 0;
    uint32_t crc;
    size_t i, j;

    if (!have_table) {
	for (i = 0; i < 256; i++) {
	    uint32_t c = (uint32_t) i;
	    for (j = 0; j < 8; j++)
		c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
	    table[i] = c;
	}
	have_table = 1;
    }

    crc = 0xFFFFFFFFUL;
    for (i = 0; i < len; i++)
	crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);

    return crc ^ 0xFFFFFFFFUL;
}

#endif /* PENMOUNT_CALIB_FORMAT_H_ */
