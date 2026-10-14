OPTS	:= -O2
CFLAGS	+= -D_GNU_SOURCE -Isrc -std=c99 -Wall -Wextra -fdata-sections -ffunction-sections -fno-strict-aliasing -fPIC -fno-exceptions
LDFLAGS	+= -Wl,--gc-sections -lrt
SFLAGS	:= -s -R .comment -R .gnu.version -R .note -R .note.ABI-tag

CC	?= $(CROSS_COMPILE)cc
STRIP	?= $(CROSS_COMPILE)strip

PREFIX	?= /usr/local
UNITDIR	?= $(PREFIX)/lib/systemd/system

SOURCES	:= $(wildcard src/*.c)
OBJECTS	:= $(patsubst %.c,%.o,$(SOURCES))
TARGETS	:= tinysrv

.PHONY:	all clean install uninstall

ifdef DEBUG
OPTS	+= -DDEBUG
endif
ifdef USE_SSL
OPTS	+= -DUSE_SSL
LDFLAGS	+= -lssl
endif

all: $(OBJECTS) $(TARGETS)

%.o: %.c
	$(CC) $(CFLAGS) $(OPTS) -c -o $@ $<

$(TARGETS): $(OBJECTS) $(TARGETS:%=%.c)
	$(CC) $(CFLAGS) $(OPTS) -o $@ $^ $(LDFLAGS)
	$(STRIP) $(SFLAGS) $@

install: $(TARGETS)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(TARGETS) $(DESTDIR)$(PREFIX)/bin/$(TARGETS)
	install -d $(DESTDIR)$(UNITDIR)
	install -m 0644 tinysrv.service $(DESTDIR)$(UNITDIR)/tinysrv.service

uninstall:
	rm -v $(DESTDIR)$(PREFIX)/bin/$(TARGETS)
	rm -v $(DESTDIR)$(UNITDIR)/tinysrv.service

clean:
ifneq (,$(OBJECTS))
	rm -f $(OBJECTS)
endif
ifneq (,$(TARGETS))
	rm -f $(TARGETS)
endif
