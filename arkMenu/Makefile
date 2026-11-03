PSPSDK = $(shell psp-config --pspsdk-path)

TARGET = ARKMENU
CFLAGS = -Os -G0 -Wall -Wno-unused -Wno-write-strings -Wno-sign-compare -MMD -MP
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
ifdef DEBUG
CFLAGS += -DDEBUG=$(DEBUG)
endif
LIBDIR = 
INCDIR = \
	include \
	include/anim \

LDFLAGS =
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = arkMenu
PSP_EBOOT_ICON = ICON0.PNG
PSP_FW_VERSION=660
BUILD_PRX = 1
PRX_EXPORTS = exports.exp
PSP_LARGE_MEMORY = 0

OBJS = \
	main.o \
	imports.o \
	src/debug.o \
	src/common.o \
	src/lang.o \
	src/system_mgr.o \
	src/gamemgr.o \
	src/net_mgr.o \
	src/entry.o \
	src/iso.o \
	src/umd.o \
	src/eboot.o \
	src/menu.o \
	src/controller.o \
	src/image.o \
	src/browser.o \
	src/browser_entries.o \
	src/osk.o \
	src/usb.o \
	src/network.o \
	src/settingsmenu.o \
	src/optionsmenu.o \
	src/texteditor.o \
	src/image_viewer.o \
	src/music_player.o \
	src/mp3.o \
	src/ftp_driver.o \
	src/pspav_wrapper.o \
	src/anim/anim.o \
	src/anim/noanim.o \
	src/anim/pixel.o \
	src/anim/wave.o \
	src/anim/sprites.o \
	src/anim/fire.o \
	src/anim/tetris.o \
	src/anim/matrix.o \
	src/anim/hacker.o \
	src/anim/bsod.o \
	src/anim/snow.o \
	src/anim/gol.o \

LIBS = \
	-lstdc++ \
	-lya2d \
	-lpng \
	-lcjson \
	-lintrafont \
	-lz \
	-lm \
	-lcisoread \
	-lpspav \
	-lpspftp \
	-lpspvfpu \
	-lpspjpeg \
	-lpspmpeg \
	-lpspaudio \
	-lpsppower \
	-lpspgum \
	-lpspgu \
	-lpsprtc \
	-lpspvram \
	-lpspmath \
	-lpspumd \
	-lpspmp3 \
	-lpspatrac3 \
	-lpspwlan \
	-lpspnet \
	-lpspnet_inet \
	-lpsphttp \
	-lpspusb \
	-lpspdisplay \
	-lpspusbdevice \
	-lpspsystemctrl_user \
	-lpspkubridge \
	-larkctrl \

include $(PSPSDK)/lib/build.mak

# The stock pspdev template only depends on the .cpp: without these, editing a header
# leaves stale objects and silently links the previous code.
-include $(OBJS:.o=.d)
