##############################################
# GNU tools
##############################################

ifndef OS
OS=$(shell uname -s)
endif

CC     = gcc
AR     = ar
LD     = ld
STRIP  = strip
MOVE   = mv

ifeq ($(OS),SunOS)
# SunOS should use /usr/xpg4/bin/awk
AWK=gawk
else
AWK=awk
endif

ifndef MAKE
MAKE=make
endif

##############################################
# flags
##############################################
ifndef TARGET_XORGPATH
TARGET_XORGPATH = /usr/include/xorg
endif

ifndef ARCH
ifeq ($(OS),SunOS)
ARCH=$(shell isainfo -k)
else
ARCH=$(shell uname -m)
endif
endif

ifeq ($(OS),Linux)
ARCH32=i686
ARCH64=x86_64
else ifeq ($(OS),SunOS)
ARCH32=i386
ARCH64=amd64
else
ARCH32=x86
ARCH64=x64
endif

XINPUT_ABIMAJOR=$(shell cat $(TARGET_XORGPATH)/xf86Module.h | grep ABI_XINPUT_VERSION | $(AWK) 'BEGIN { FS = "(" } ; { print $$2 }' | $(AWK) 'BEGIN { FS = "[, )]" } ; { print $$1 }')
XINPUT_ABIMIONOR=$(shell cat  $(TARGET_XORGPATH)/xf86Module.h | grep ABI_XINPUT_VERSION | $(AWK) 'BEGIN { FS = "(" } ; { print $$2 }' | $(AWK) 'BEGIN { FS = "[, )]" } ; { print $$3 }')
XINPUT_ABI_STR=$(XINPUT_ABIMAJOR).$(XINPUT_ABIMIONOR)

CDEFINE    := -DPENMOUNT_XORG
ifeq ($(ARCH),$(ARCH64))
ARCHFLAG   := -m64
CDEFINE    += -DPENMOUNT_64BIT
else
ifeq ($(ARCH),$(ARCH32))
ARCHFLAG   := -m32
else
ARCHFLAG   :=
endif
endif

ifndef RELEASE
RELEASE=$(shell uname -r)
endif

ifeq ($(OS),FreeBSD)
  CDEFINE    += -DPENMOUNT_BSD
  TARGET_XORGPATH=/usr/local/include/xorg 
  INCPATH_X11=/usr/local/include
  INCPATH_PIXMAN=/usr/local/include/pixman-1
else ifeq ($(OS),SunOS)
  CDEFINE    += -DPENMOUNT_SOLARIS
  ifeq ($(RELEASE),5.11)
    CINCLUDE   += -I/usr/openwin/server/include
  else
    CINCLUDE   += -ISolaris10/xorg -ISolaris10/X11 -ISolaris10 -I/usr/openwin/server/include
  endif
  INCPATH_X11=/usr/X11/include
  INCPATH_PIXMAN=/usr/include/pixman-1
else
  CDEFINE    += -DPENMOUNT_LNX
  INCPATH_X11=/usr/include/X11
  INCPATH_PIXMAN=/usr/include/pixman-1
endif

CINCLUDE   += -I../../inc        \
              -I$(TARGET_XORGPATH) -I$(INCPATH_X11) -I$(INCPATH_PIXMAN)

ifeq ($(OEM),NeoKylin)
CDEFINE    += -DNEOKYLIN
endif

CDEFINE    += -DPENMOUNT_EEPROM_SUPPORT
ifeq ($(OEM),Smartmatic)
CDEFINE    += -DPENMOUNT_NO_ENABLE_RS232 -DODM_PM6007
endif

ifeq ($(OS),Linux)
CDEFINE    += -D_USE_PULSEAUDIO_
endif

#ifeq ($(OEM),IPCMX)
#CDEFINE    += -DPENMOUNT_FILELOG_ENABLE
#endif

ifeq ($(XINPUT_ABI_STR),0.4)
CINCLUDE   += -Ixorg
endif

CFLAGS     := -Wall -fPIC
ifneq ($(XINPUT_ABI_STR),0.4)
ifneq ($(OS),SunOS)
CFLAGS     += -fvisibility=hidden
endif
endif
CDEFINE    += -U_FORTIFY_SOURCE
CFLAGS     += $(ARCHFLAG) $(CDEFINE) $(CINCLUDE)
LIBS        = ../../lib/$(ARCH)/libpm-driver.a
LDLIBS      = -L../../lib/$(ARCH) -lpm-driver 
STRIP_FLAGS = --remove-section=.comment

##############################################
# targets
##############################################

TARGETNAME  = penmount_drv
OBJPATH     = ./obj/$(ARCH)
DYNAMIC_LIB = $(TARGETNAME).so
OBJECTS     = $(OBJPATH)/penmount.o $(OBJPATH)/penmount.o $(OBJPATH)/penmount_axes.o $(OBJPATH)/penmount_brain.o $(OBJPATH)/penmount_btn.o $(OBJPATH)/penmount_calib.o $(OBJPATH)/penmount_key.o
LINK_OBJECT = PenMount
SHARE_FLAGS = -shared $(ARCHFLAG) -Wl,-soname,$(DYNAMIC_LIB)

##############################################
# rules
##############################################

all: $(DYNAMIC_LIB)
	@if [ ! -d ../../lib/$(ARCH) ]; then \
		mkdir ../../lib/$(ARCH) ; \
	fi
	@if [ ! -d ../../lib/$(ARCH)/$(XINPUT_ABI_STR) ]; then \
		mkdir ../../lib/$(ARCH)/$(XINPUT_ABI_STR) ; \
	fi
	@$(MOVE) $(DYNAMIC_LIB) ../../lib/$(ARCH)/$(XINPUT_ABI_STR)

$(OBJPATH)/%.o : %.c
	@if [ ! -d obj ] ; then \
		mkdir obj ; \
	fi
	@if [ ! -d obj/$(ARCH) ] ; then \
		mkdir obj/$(ARCH) ; \
	fi
	@$(CC) $(CFLAGS) -o $@ -c $<

$(DYNAMIC_LIB): $(OBJECTS)
	@$(CC) $(SHARE_FLAGS) -o $(DYNAMIC_LIB) $(OBJECTS) $(LDLIBS)

install:
	@if [ -d /usr/lib/xorg/modules/input ] ; then \
		cp ../../lib/$(ARCH)/$(XINPUT_ABI_STR)/$(DYNAMIC_LIB) /usr/lib/xorg/modules/input ;\
	elif [ -d /usr/lib64/xorg/modules/input ] ; then \
		cp ../../lib/$(ARCH)/$(XINPUT_ABI_STR)/$(DYNAMIC_LIB) /usr/lib64/xorg/modules/input ;\
	fi

clean:
	@rm -f $(OBJPATH)/*.o *.a *.so*