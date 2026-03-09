# kc - kai calendar
# See LICENSE file for copyright and license details.

VERSION = 0.1.0

PREFIX = /usr/local
PKG_CONFIG = pkg-config

# detect OS for ncurses library name
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
NCURSES = -lncurses
else
NCURSES = -lncursesw
endif

INCS = `$(PKG_CONFIG) --cflags libical libcurl`
LIBS = `$(PKG_CONFIG) --libs libical libcurl` $(NCURSES)

CFLAGS  += -std=c99 -pedantic -Wall -Wextra -Wno-unused-variable -Os \
           -D_XOPEN_SOURCE=700 -DVERSION=\"$(VERSION)\" $(INCS)
LDFLAGS += $(LIBS)

SRC = src/kc.c src/ui.c src/cal.c src/ical.c src/vdir.c \
      src/caldav.c src/goauth.c src/sanitize.c
OBJ = $(SRC:.c=.o)
BIN = kc

all: $(BIN)

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

$(BIN): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f $(BIN) $(OBJ)

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(BIN) $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

.PHONY: all clean install uninstall
