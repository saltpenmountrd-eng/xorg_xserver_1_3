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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <xorg-server.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XIproto.h>

#include "penmount.h"
#include <X11/Xfuncproto.h>
#include <xf86.h>
#include <X11/extensions/XI.h>
#include <xf86Module.h>
#include <xf86Xinput.h>
#include <mipointer.h>


#include <xf86_OSproc.h>

/*
 * FIXME: This should most definitely not be here.
 * But I need it, even if it _is_ private.
 */

void xf86ActivateDevice(InputInfoPtr pInfo);

static void
PenmountReadInput(InputInfoPtr pInfo)
{
    struct input_event ev;
    int len;

    while (xf86WaitForInput (pInfo->fd, 0) > 0) {
        len = read(pInfo->fd, &ev, sizeof(ev));
        if (len != sizeof(ev)) {
            /* The kernel promises that we always only read a complete
             * event, so len != sizeof ev is an error. */
            xf86Msg(X_ERROR, "Read error: %s (%d, %d != %ld)\n",
		    strerror(errno), errno, len, sizeof (ev));
	    if (len < 0) {
		penmountDevicePtr pPenmount = pInfo->private;
		pPenmount->callback(pPenmount->pInfo->dev, DEVICE_OFF);
		pPenmount->seen--;
	    }
            break;
        }

        switch (ev.type) {
        case EV_REL:
	    PenmountAxesRelProcess (pInfo, &ev);
	    break;

        case EV_ABS:
	    PenmountAxesAbsProcess (pInfo, &ev);
            break;

        case EV_KEY:
	    if ((ev.code >= BTN_MISC) && (ev.code < KEY_OK))
		PenmountBtnProcess (pInfo, &ev);
	    else
		PenmountKeyProcess (pInfo, &ev);
	    break;

        case EV_SYN:
	    if (ev.code == SYN_REPORT) {
		PenmountAxesSynRep (pInfo);
		/* PenmountBtnSynRep (pInfo); */
		/* PenmountKeySynRep (pInfo); */
	    } else if (ev.code == SYN_CONFIG) {
		PenmountAxesSynCfg (pInfo);
		/* PenmountBtnSynCfg (pInfo); */
		/* PenmountKeySynCfg (pInfo); */
	    }
            break;
        }
    }
}

static void
PenmountSigioReadInput (int fd, void *data)
{
    PenmountReadInput ((InputInfoPtr) data);
}

static int
PenmountProc(DeviceIntPtr device, int what)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    penmountDevicePtr pPenmount = pInfo->private;

    if (!pPenmount->device)
	return BadRequest;

    switch (what)
    {
    case DEVICE_INIT:
	if (pPenmount->state.axes)
	    PenmountAxesInit (device);
	if (pPenmount->state.btn)
	    PenmountBtnInit (device);
	if (pPenmount->state.key)
	    PenmountKeyInit (device);

	if (penmountCalibLoad (&pPenmount->calib))
	    xf86Msg(X_INFO, "%s: Loaded calibration from %s.\n", pInfo->name, PM_CAL_FILE);
	else
	    xf86Msg(X_INFO, "%s: No valid calibration in %s, running uncalibrated.\n",
		    pInfo->name, PM_CAL_FILE);

	xf86Msg(X_INFO, "%s: Init\n", pInfo->name);
	break;

    case DEVICE_ON:
	xf86Msg(X_INFO, "%s: On\n", pInfo->name);
	if (device->public.on)
	    break;

	if ((pInfo->fd = penmountGetFDForDevice (pPenmount)) == -1) {
	    xf86Msg(X_ERROR, "%s: cannot open input device.\n", pInfo->name);

	    if (pPenmount->phys)
		xfree(pPenmount->phys);
	    pPenmount->phys = NULL;

	    if (pPenmount->device)
		xfree(pPenmount->device);
	    pPenmount->device = NULL;

	    return BadRequest;
	}

	if (pPenmount->state.can_grab)
	    if (ioctl(pInfo->fd, EVIOCGRAB, (void *)1))
		xf86Msg(X_ERROR, "%s: Unable to grab device (%s).\n", pInfo->name, strerror(errno));

	xf86FlushInput (pInfo->fd);
	if (!xf86InstallSIGIOHandler (pInfo->fd, PenmountSigioReadInput, pInfo))
	    AddEnabledDevice (pInfo->fd);

	device->public.on = TRUE;

	if (pPenmount->state.axes)
	    PenmountAxesOn (device);
	if (pPenmount->state.btn)
	    PenmountBtnOn (device);
	if (pPenmount->state.key)
	    PenmountKeyOn (device);
	break;

    case DEVICE_CLOSE:
    case DEVICE_OFF:
	xf86Msg(X_INFO, "%s: Off\n", pInfo->name);
	if (pInfo->fd != -1) {
	    if (pPenmount->state.can_grab)
		ioctl(pInfo->fd, EVIOCGRAB, (void *)0);

	    RemoveEnabledDevice (pInfo->fd);
	    xf86RemoveSIGIOHandler (pInfo->fd);
	    close (pInfo->fd);
	    pInfo->fd = -1;

	    if (pPenmount->state.axes)
		PenmountAxesOff (device);
	    if (pPenmount->state.btn)
		PenmountBtnOff (device);
	    if (pPenmount->state.key)
		PenmountKeyOff (device);
	}

        if (what == DEVICE_CLOSE)
            penmountRemoveDevice(pPenmount);

	device->public.on = FALSE;
	break;
    }

    return Success;
}

static int
PenmountSwitchMode (ClientPtr client, DeviceIntPtr device, int mode)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    penmountDevicePtr pPenmount = pInfo->private;
    penmountStatePtr state = &pPenmount->state;

    switch (mode)
    {
	case Absolute:
	case Relative:
	    xf86Msg(X_INFO, "%s: Switching mode to %d.\n", pInfo->name, mode);
	    if (state->abs)
		state->mode = mode;
	    else
		return !Success;
	    break;
#if 0
	case SendCoreEvents:
	case DontSendCoreEvents:
	    xf86XInputSetSendCoreEvents (pInfo, (mode == SendCoreEvents));
	    break;
#endif
	default:
	    return !Success;
    }

    return Success;
}

static Bool
PenmountNew(penmountDriverPtr driver, penmountDevicePtr device)
{
    InputInfoPtr pInfo;
    char name[512] = {0};

    if (!(pInfo = xf86AllocateInput(driver->drv, 0)))
	return 0;

    /* Initialise the InputInfoRec. */
    strncat (name, driver->dev->identifier, sizeof(name));
    strncat (name, "-", sizeof(name));
    strncat (name, device->phys, sizeof(name));
    pInfo->name = xstrdup(name);
    pInfo->flags = 0;
    pInfo->type_name = "UNKNOWN";
    pInfo->device_control = PenmountProc;
    pInfo->read_input = PenmountReadInput;
    pInfo->switch_mode = PenmountSwitchMode;
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
    pInfo->motion_history_proc = xf86GetMotionEvents;
#endif
    pInfo->conf_idev = driver->dev;

    pInfo->private = device;

    device->callback = PenmountProc;
    device->pInfo = pInfo;
    device->debug = driver->debug;
    device->calib.debug = driver->debug;

    xf86CollectInputOptions(pInfo, NULL, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    if ((pInfo->fd = penmountGetFDForDevice (device)) == -1) {
	xf86Msg(X_ERROR, "%s: cannot open input device\n", pInfo->name);
	pInfo->private = NULL;
	xf86DeleteInput (pInfo, 0);
	return 0;
    }

    if (!penmountGetBits (pInfo->fd, &device->bits)) {
	xf86Msg(X_ERROR, "%s: cannot load bits\n", pInfo->name);
	pInfo->private = NULL;
	close (pInfo->fd);
	xf86DeleteInput (pInfo, 0);
	return 0;
    }

    if (ioctl(pInfo->fd, EVIOCGRAB, (void *)1)) {
	xf86Msg(X_INFO, "%s: Unable to grab device (%s).  Cowardly refusing to check use as keyboard.\n", pInfo->name, strerror(errno));
	device->state.can_grab = 0;
    } else {
	device->state.can_grab = 1;
        ioctl(pInfo->fd, EVIOCGRAB, (void *)0);
    }


    /* XXX: Note, the order of these is (maybe) still important. */
    PenmountAxesNew0 (pInfo);
    PenmountBtnNew0 (pInfo);

    PenmountAxesNew1 (pInfo);
    PenmountBtnNew1 (pInfo);

    if (device->state.can_grab)
	PenmountKeyNew (pInfo);

    close (pInfo->fd);
    pInfo->fd = -1;

    pInfo->flags |= XI86_OPEN_ON_INIT;
    if (!(pInfo->flags & XI86_CONFIGURED)) {
        xf86Msg(X_ERROR, "%s: Don't know how to use device.\n", pInfo->name);
	pInfo->private = NULL;
	close (pInfo->fd);
	xf86DeleteInput (pInfo, 0);
        return 0;
    }

    if (driver->configured) {
	xf86ActivateDevice (pInfo);

	pInfo->dev->inited = (device->callback(device->pInfo->dev, DEVICE_INIT) == Success);
	EnableDevice (pInfo->dev);
    }

    return 1;
}

static void
PenmountParseBits (char *in, unsigned long *out, int len)
{
    unsigned long v[2];
    int n, i, max_bits = len * BITS_PER_LONG;

    n = sscanf (in, "%lu-%lu", &v[0], &v[1]);
    if (!n)
	return;

    if (v[0] >= max_bits)
	return;

    if (n == 2) {
	if (v[1] >= max_bits)
	    v[1] = max_bits - 1;

	for (i = v[0]; i <= v[1]; i++)
	    set_bit (i, out);
    } else
	set_bit (v[0], out);
}

static void
PenmountParseBitOption (char *opt, unsigned long *all, unsigned long *not, unsigned long *any, int len)
{
    char *cur, *next;

    next = opt - 1;
    while (next) {
	cur = next + 1;
	if ((next = strchr(cur, ' ')))
	    *next = '\0';

	switch (cur[0]) {
	    case '+':
		PenmountParseBits (cur + 1, all, len);
		break;
	    case '-':
		PenmountParseBits (cur + 1, not, len);
		break;
	    case '~':
		PenmountParseBits (cur + 1, any, len);
		break;
	}
    }
}

static InputInfoPtr
PenmountCorePreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    penmountDriverPtr pPenmount;
    char *opt, *tmp;

    if (!(pPenmount = Xcalloc(sizeof(*pPenmount))))
        return NULL;

    pPenmount->name = xf86CheckStrOption(dev->commonOptions, "Name", NULL);
    pPenmount->phys = xf86CheckStrOption(dev->commonOptions, "Phys", NULL);
    pPenmount->device = xf86CheckStrOption(dev->commonOptions, "Device", NULL);

#define bitoption(field)							\
    opt = xf86CheckStrOption(dev->commonOptions, #field "Bits", NULL);		\
    if (opt) {									\
	tmp = strdup(opt);							\
	PenmountParseBitOption (tmp, pPenmount->all_bits.field,			\
		pPenmount->not_bits.field,					\
		pPenmount->any_bits.field,					\
		sizeof(pPenmount->not_bits.field) / sizeof (unsigned long));	\
	free (tmp);								\
    }
    bitoption(ev);
    bitoption(key);
    bitoption(rel);
    bitoption(abs);
    bitoption(msc);
    bitoption(led);
    bitoption(snd);
    bitoption(ff);
#undef bitoption

    /*
     * This is a dedicated PenMount driver: default the USB match to the
     * panel it targets (vendor 0x14e1 / product 0x6000) so a bare
     * `Driver "penmount"` InputDevice section auto-detects the right
     * hardware. An explicit "vendor"/"product" Option still overrides
     * this, e.g. for a different PenMount USB PID variant.
     */
    pPenmount->id.bustype = xf86CheckIntOption(dev->commonOptions, "bustype", 0);
    pPenmount->id.vendor = xf86CheckIntOption(dev->commonOptions, "vendor", PM_USB_VENDOR);
    pPenmount->id.product = xf86CheckIntOption(dev->commonOptions, "product", PM_USB_PRODUCT);
    pPenmount->id.version = xf86CheckIntOption(dev->commonOptions, "version", 0);

    pPenmount->pass = xf86CheckIntOption(dev->commonOptions, "Pass", 0);
    if (pPenmount->pass > 3)
	pPenmount->pass = 3;
    else if (pPenmount->pass < 0)
	pPenmount->pass = 0;

    /*
     * Off by default: the [calib-debug] logging in penmount_axes.c and
     * penmount_calib.c is per-touch-sample chatty and only meant for
     * diagnosing a specific problem, not for routine production use.
     * Add `Option "Debug" "on"` to this InputDevice section to enable it.
     */
    pPenmount->debug = xf86SetBoolOption(dev->commonOptions, "Debug", FALSE);
    if (pPenmount->debug)
	xf86Msg(X_CONFIG, "%s: Debug logging enabled.\n", dev->identifier);

    pPenmount->callback = PenmountNew;

    pPenmount->dev = dev;
    pPenmount->drv = drv;

    if (!penmountStart (drv)) {
	xf86Msg(X_ERROR, "%s: cannot start penmount brain.\n", dev->identifier);
        xfree(pPenmount);
	return NULL;
    }

    penmountNewDriver (pPenmount);

    if (pPenmount->devices && pPenmount->devices->pInfo)
	return pPenmount->devices->pInfo;

    return NULL;
}



_X_EXPORT InputDriverRec PENMOUNT = {
    1,
    "penmount",
    NULL,
    PenmountCorePreInit,
    NULL,
    NULL,
    0
};

static void
PenmountUnplug(pointer	p)
{
}

static pointer
PenmountPlug(pointer	module,
          pointer	options,
          int		*errmaj,
          int		*errmin)
{
    xf86AddInputDriver(&PENMOUNT, module, 0);
    return module;
}

static XF86ModuleVersionInfo PenmountVersionRec =
{
    "penmount",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    1, 1, 0,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData penmountModuleData =
{
    &PenmountVersionRec,
    PenmountPlug,
    PenmountUnplug
};
