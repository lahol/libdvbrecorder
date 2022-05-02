CC = gcc
LD = gcc
PKG_CONFIG = pkg-config
CFLAGS += -Wall `$(PKG_CONFIG) --cflags glib-2.0`
LIBS += `$(PKG_CONFIG) --libs glib-2.0` -ldvbpsi -lsqlite3
RM ?= rm

PREFIX := /usr

ifdef DVBDUMMY
	CFLAGS += -DDVB_TUNER_DUMMY
endif

ifdef DEBUG
	CFLAGS += -g
else
	CFLAGS += -O2
endif

all: libdvbrecorder.so.1.0

dr_SRC := $(wildcard *.c)
dr_OBJ := $(dr_SRC:.c=.o)
dr_HEADERS := $(wildcard *.h)

libdvbrecorder.so.1.0: $(dr_OBJ)
	$(LD) -shared -Wl,-soname,libdvbrecorder.so.1 -o $@ $^ $(CFLAGS) $(LIBS)

%.o: %.c $(dr_HEADERS)
	$(CC) -I. $(CFLAGS) -fPIC -c -o $@ $<

install: libdvbrecorder.so.1.0
	install libdvbrecorder.so.1.0 $(PREFIX)/lib/
	ln -sf $(PREFIX)/lib/libdvbrecorder.so.1.0 $(PREFIX)/lib/libdvbrecorder.so.1
	ln -sf $(PREFIX)/lib/libdvbrecorder.so.1 $(PREFIX)/lib/libdvbrecorder.so
	install -d $(PREFIX)/include/dvbrecorder
	install dvbrecorder.h events.h channels.h channel-db.h dvb-scanner.h epg.h streaminfo.h filter.h scheduled.h logging.h $(PREFIX)/include/dvbrecorder

clean:
	$(RM) -f libdvbrecorder.so.1.0 $(dr_OBJ)

.PHONY: all clean install
