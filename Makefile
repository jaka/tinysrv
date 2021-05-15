OPTS	:= -O2
CFLAGS	+= -Isrc -std=c99 -Wall -Wextra -fdata-sections -ffunction-sections -fno-strict-aliasing -fPIC -fno-exceptions
LDFLAGS	+= -Wl,--gc-sections -lrt
SFLAGS	:= -s -R .comment -R .gnu.version -R .note -R .note.ABI-tag

CC	?= $(CROSS_COMPILE)cc
STRIP	?= $(CROSS_COMPILE)strip

SOURCES	:= $(wildcard src/*.c)
OBJECTS	:= $(patsubst %.c,%.o,$(SOURCES))
TARGETS := tinysrv

ifdef USE_SSL
LDFLAGS	+= ssl
endif

all: $(OBJECTS) $(TARGETS)

%.o: %.c
	$(CC) $(CFLAGS) $(OPTS) -c -o $@ $<

$(TARGETS): $(OBJECTS)
	$(CC) $(CFLAGS) $(OPTS) -o $@ $@.c $^ $(LDFLAGS)
	$(STRIP) $(SFLAGS) $@

clean:
ifneq (,$(OBJECTS))
	rm -f $(OBJECTS)
endif
ifneq (,$(TARGETS))
	rm -f $(TARGETS)
endif
