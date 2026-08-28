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
/*
 * Copyright © 2004 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of Red Hat
 * not be used in advertising or publicity pertaining to distribution
 * of the software without specific, written prior permission.  Red
 * Hat makes no representations about the suitability of this software
 * for any purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Kristian Høgsberg (krh@redhat.com)
 */

#ifndef PENMOUNT_BRAIN_H_
#define PENMOUNT_BRAIN_H_

#define _XF86_ANSIC_H
#define XF86_LIBC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <linux/input.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <xorg-server.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XIproto.h>
#include <X11/Xfuncproto.h>
#include <xf86.h>
#include <X11/extensions/XI.h>
#include <xf86Module.h>
#include <xf86Xinput.h>
#include <mipointer.h>
#include <xf86_OSproc.h>

#ifndef BITS_PER_LONG
#define BITS_PER_LONG		(sizeof(unsigned long) * 8)
#endif

#define NBITS(x)		((((x)-1)/BITS_PER_LONG)+1)
#define LONG(x)			((x)/BITS_PER_LONG)
#define MASK(x)			(1UL << ((x) & (BITS_PER_LONG - 1)))

#ifndef test_bit
#define test_bit(bit, array)	(!!(array[LONG(bit)] & MASK(bit)))
#endif
#ifndef set_bit
#define set_bit(bit, array)	(array[LONG(bit)] |= MASK(bit))
#endif
#ifndef clear_bit
#define clear_bit(bit, array)	(array[LONG(bit)] &= ~MASK(bit))
#endif

/* 2.4 compatibility */
#ifndef EVIOCGSW

#include <sys/time.h>
#include <sys/ioctl.h>

#define EVIOCGSW(len)		_IOC(_IOC_READ, 'E', 0x1b, len)		/* get all switch states */

#define EV_SW			0x05
#endif

#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif

#ifndef BTN_TASK
#define BTN_TASK 0x117
#endif

#ifndef EV_SYN
#define EV_SYN EV_RST
#endif
/* end compat */

#include <X11/extensions/XKB.h>
#include <X11/extensions/XKBstr.h>

/* XInput 1.4+ compatability. */
#ifndef SendCoreEvents
#define SendCoreEvents		59
#define DontSendCoreEvents	60
#endif


/*
 * Switch events
 */

#define EV_SW_0		0x00
#define EV_SW_1		0x01
#define EV_SW_2		0x02
#define EV_SW_3		0x03
#define EV_SW_4		0x04
#define EV_SW_5		0x05
#define EV_SW_6		0x06
#define EV_SW_7		0x07
#define EV_SW_MAX	0x0f

#define EV_BUS_GSC		0x1A

#define PENMOUNT_MAXBUTTONS	96

typedef struct {
    unsigned long	ev[NBITS(EV_MAX)];
    unsigned long	key[NBITS(KEY_MAX)];
    unsigned long	rel[NBITS(REL_MAX)];
    unsigned long	abs[NBITS(ABS_MAX)];
    unsigned long	msc[NBITS(MSC_MAX)];
    unsigned long	led[NBITS(LED_MAX)];
    unsigned long	snd[NBITS(SND_MAX)];
    unsigned long	ff[NBITS(FF_MAX)];
} penmountBitsRec, *penmountBitsPtr;

typedef struct {
    int		real_buttons;
    int		buttons;
    CARD8	map[PENMOUNT_MAXBUTTONS];
    void	(*callback[PENMOUNT_MAXBUTTONS])(InputInfoPtr pInfo, int button, int value);
} penmountBtnRec, *penmountBtnPtr;

typedef struct {
    int		axes;
    int		v[ABS_MAX];
    int		old_x, old_y;
    int		count;
    int		min[ABS_MAX];
    int		max[ABS_MAX];
    int		map[ABS_MAX];
    int		screen; /* Screen number for this device. */
    Bool	use_touch;
    Bool	touch;
    Bool	reset_x, reset_y;
} penmountAbsRec, *penmountAbsPtr;

typedef struct {
    int		axes;
    int		v[REL_MAX];
    int		count;
    int		map[REL_MAX];
    int		btnMap[REL_MAX][2];
} penmountRelRec, *penmountRelPtr;

typedef struct {
    int		axes;
    int		v[ABS_MAX];
} penmountAxesRec, *penmountAxesPtr;

typedef struct {
    char	*xkb_rules;
    char	*xkb_model;
    char	*xkb_layout;
    char	*xkb_variant;
    char	*xkb_options;
    XkbComponentNamesRec xkbnames;
} penmountKeyRec, *penmountKeyPtr;

typedef struct _penmountState {
    Bool	can_grab;
    Bool	sync;
    int		mode;	/* Either Absolute or Relative. */

    penmountBtnPtr	btn;
    penmountAbsPtr	abs;
    penmountRelPtr	rel;
    penmountKeyPtr	key;
    penmountAxesPtr axes;
} penmountStateRec, *penmountStatePtr;

typedef struct _penmountCalib {
    double	ax, ay, az;
    double	bx, by, bz;
    Bool	valid;
    Bool	debug;	/* mirrors penmountDeviceRec.debug; see PenmountNew() */
} penmountCalibRec, *penmountCalibPtr;

typedef struct _penmountDevice {
    const char		*name;
    const char		*phys;
    const char		*device;
    int			seen;

    InputInfoPtr	pInfo;
    int			(*callback)(DeviceIntPtr cb_data, int what);

    penmountBitsRec	bits;
    struct input_id	id;

    penmountStateRec	state;
    penmountCalibRec	calib;
    Bool		debug;	/* from the "Debug" xorg.conf Option; gates the
				 * [calib-debug] xf86Msg calls in
				 * penmount_axes.c/penmount_calib.c so a
				 * production system doesn't get flooded with
				 * per-touch-sample logging by default */

    struct _penmountDevice *next;
} penmountDeviceRec, *penmountDevicePtr;

typedef struct _penmountDriver {
    const char		*name;
    const char		*phys;
    const char		*device;

    penmountBitsRec	all_bits;
    penmountBitsRec	not_bits;
    penmountBitsRec	any_bits;

    struct input_id	id;

    int			pass;
    Bool		debug;	/* parsed once from Option "Debug" in
				 * PenmountCorePreInit(), copied into each
				 * matched penmountDeviceRec by PenmountNew() */

    InputDriverPtr	drv;
    IDevPtr		dev;
    Bool		(*callback)(struct _penmountDriver *driver, penmountDevicePtr device);
    penmountDevicePtr	devices;
    Bool		configured;

    struct _penmountDriver	*next;
} penmountDriverRec, *penmountDriverPtr;

int penmountGetFDForDevice (penmountDevicePtr driver);
Bool penmountStart (InputDriverPtr drv);
Bool penmountNewDriver (penmountDriverPtr driver);
Bool penmountGetBits (int fd, penmountBitsPtr bits);
void penmountRemoveDevice (penmountDevicePtr device);

int PenmountBtnInit (DeviceIntPtr device);
int PenmountBtnOn (DeviceIntPtr device);
int PenmountBtnOff (DeviceIntPtr device);
int PenmountBtnNew0(InputInfoPtr pInfo);
int PenmountBtnNew1(InputInfoPtr pInfo);
void PenmountBtnProcess (InputInfoPtr pInfo, struct input_event *ev);
void PenmountBtnPostFakeClicks(InputInfoPtr pInfo, int button, int count);
int PenmountBtnFind (InputInfoPtr pInfo, const char *button);
int PenmountBtnExists (InputInfoPtr pInfo, int button);

int PenmountAxesInit (DeviceIntPtr device);
int PenmountAxesOn (DeviceIntPtr device);
int PenmountAxesOff (DeviceIntPtr device);
int PenmountAxesNew0(InputInfoPtr pInfo);
int PenmountAxesNew1(InputInfoPtr pInfo);
void PenmountAxesAbsProcess (InputInfoPtr pInfo, struct input_event *ev);
void PenmountAxesRelProcess (InputInfoPtr pInfo, struct input_event *ev);
void PenmountAxesSynRep (InputInfoPtr pInfo);
void PenmountAxesSynCfg (InputInfoPtr pInfo);

int PenmountKeyInit (DeviceIntPtr device);
int PenmountKeyNew (InputInfoPtr pInfo);
int PenmountKeyOn (DeviceIntPtr device);
int PenmountKeyOff (DeviceIntPtr device);
void PenmountKeyProcess (InputInfoPtr pInfo, struct input_event *ev);

/*
 * --- PenMount calibration support -----------------------------------
 *
 * Default USB identity for the PenMount panel this driver targets; used
 * to pre-fill the driver-level match template in PenmountCorePreInit()
 * when the xorg.conf InputDevice section does not override "vendor"/
 * "product" itself.
 */
#define PM_USB_VENDOR	0x14e1
#define PM_USB_PRODUCT	0x6000

/*
 * PM_CAL_DIR/FILE/START/OK and PM_CAL_RES (the fixed "evdev resolution"
 * domain the calibration matrix and the registered valuator axis range
 * both use) are defined in penmount_calib_format.h, shared verbatim with
 * the pm_calibrate GUI -- see that header for the on-disk file format.
 */
#include "penmount_calib_format.h"

Bool penmountCalibLoad (penmountCalibPtr calib);
Bool penmountCalibCheckStart (void);
Bool penmountCalibCheckAndConsumeOK (penmountCalibPtr calib);

#endif	/* LNX_PENMOUNT_H_ */
