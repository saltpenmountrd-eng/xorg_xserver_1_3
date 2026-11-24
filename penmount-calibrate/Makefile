# Plain Makefile (not autotools) -- this is a small standalone GTK2
# application, independent of the xf86-input-penmount driver's autotools
# build. Requires: gtk+-2.0, x11, xi (XInput 1.0 client library) dev
# packages on the build machine.

CC      ?= gcc
PKGS    := gtk+-2.0 x11 xi
CFLAGS  += -Wall -O2 $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

# Switch for the CalibOK live-reload (see pm_calibrate.c and penmount.h).
# Default is 0 (disabled). Set PENMOUNT_CALIB_LIVE_RELOAD=1 to enable:
#   make clean && make PENMOUNT_CALIB_LIVE_RELOAD=1
ifdef PENMOUNT_CALIB_LIVE_RELOAD
CFLAGS  += -DPENMOUNT_CALIB_LIVE_RELOAD=$(PENMOUNT_CALIB_LIVE_RELOAD)
endif

# Switch for restart-X notice method (GUI messagebox popup vs terminal text).
# Default is terminal text. Set PENMOUNT_CALIB_POPUP_MESSAGEBOX=1 to enable GUI messagebox popup:
#   make clean && make PENMOUNT_CALIB_POPUP_MESSAGEBOX=1
ifeq ($(PENMOUNT_CALIB_POPUP_MESSAGEBOX),1)
CFLAGS  += -DPENMOUNT_CALIB_POPUP_MESSAGEBOX=1
endif

TARGET  := pm_calibrate
SRCS    := pm_calibrate.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRCS) penmount_calib_format.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

install: $(TARGET)
	install -d $(DESTDIR)/usr/bin
	install -m 0755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)

clean:
	rm -f $(TARGET)
