# Plain Makefile (not autotools) -- this is a small standalone GTK2
# application, independent of the xf86-input-penmount driver's autotools
# build. Requires: gtk+-2.0, x11, xi (XInput 1.0 client library) dev
# packages on the build machine.

CC      ?= gcc
PKGS    := gtk+-2.0 x11 xi
CFLAGS  += -Wall -O2 $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

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
