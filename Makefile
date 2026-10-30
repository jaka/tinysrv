OPTS	:= -O2
CFLAGS	+= -D_GNU_SOURCE -Isrc -std=c99 -Wall -Wextra -fdata-sections -ffunction-sections -fno-strict-aliasing -fPIC -MMD -MP
LDFLAGS	+= -Wl,--gc-sections
SFLAGS	:= -s -R .comment -R .gnu.version -R .note -R .note.ABI-tag

ifeq ($(origin CC),default)
CC	:= $(CROSS_COMPILE)cc
endif
STRIP	?= $(CROSS_COMPILE)strip

PREFIX	?= /usr/local
UNITDIR	?= $(PREFIX)/lib/systemd/system

SOURCES	:= $(wildcard src/*.c) tinysrv.c
OBJECTS	:= $(patsubst %.c,%.o,$(SOURCES))
DEPS	:= $(OBJECTS:.o=.d)
TARGETS	:= tinysrv

.PHONY:	all clean install uninstall

ifdef DEBUG
OPTS	+= -DDEBUG
endif
ifdef USE_SSL
OPTS	+= -DUSE_SSL
LDFLAGS	+= -lssl
endif

# Rebuild everything when the compiler or flags change (e.g. USE_SSL, DEBUG).
FLAGS_STAMP	:= .build-flags
BUILD_FLAGS	:= $(CC) $(CFLAGS) $(OPTS) $(LDFLAGS)
$(shell [ "$$(cat $(FLAGS_STAMP) 2>/dev/null)" = '$(BUILD_FLAGS)' ] || echo '$(BUILD_FLAGS)' > $(FLAGS_STAMP))

all: $(OBJECTS) $(TARGETS)

$(OBJECTS): $(FLAGS_STAMP)

%.o: %.c
	$(CC) $(CFLAGS) $(OPTS) -c -o $@ $<

$(TARGETS): $(OBJECTS)
	$(CC) $(CFLAGS) $(OPTS) -o $@ $^ $(LDFLAGS)
ifndef DEBUG
	$(STRIP) $(SFLAGS) $@
endif

-include $(DEPS)

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
	rm -f $(OBJECTS) $(DEPS) $(FLAGS_STAMP)
endif
ifneq (,$(TARGETS))
	rm -f $(TARGETS)
endif
