OPTS	:= -O2
#-O0 -g -ggdb
CFLAGS	+= -Isrc -std=c99 -Wall -Wextra -ffunction-sections -fdata-sections -fno-strict-aliasing
LDFLAGS	+= -Wl,--gc-sections -lrt
SFLAGS	:= -s -R .comment -R .gnu.version -R .note -R .note.ABI-tag
STRIP	?= strip

SOURCES	:= $(wildcard src/*.c)
OBJECTS	:= $(patsubst %.c,%.o,$(SOURCES))
TARGETS := tinysrv

ifdef USE_SSL
LDFLAGS	+= ssl
endif

all: $(OBJECTS) $(TARGETS)

%.o: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTS) -c -o $@ $<

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
