# kc - kai calendar
# See LICENSE file for copyright and license details.

# Version lives in the VERSION file (single source of truth).
# To release: bump VERSION, commit, tag the same value with `v` prefix, push tag.
VERSION := $(shell cat VERSION)

PREFIX = /usr/local
PKG_CONFIG = pkg-config

# detect OS for ncurses library name and macOS pkg-config paths
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
NCURSES = -lncurses
CFLAGS += -D_DARWIN_C_SOURCE
# Homebrew on Apple Silicon (and Intel) may install icu4c in a versioned keg
# that is not on the default pkg-config search path.
ICU4C_PREFIX := $(shell brew --prefix icu4c 2>/dev/null)
ifneq ($(ICU4C_PREFIX),)
export PKG_CONFIG_PATH := $(ICU4C_PREFIX)/lib/pkgconfig:$(PKG_CONFIG_PATH)
endif
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

# --- debian package ---
# `make deb` produces dist/kc_<version>_<arch>.deb. CI overrides DEB_VERSION
# from the git tag (refs/tags/vX.Y.Z -> X.Y.Z); local builds default to the
# VERSION file.
DEB_VERSION ?= $(VERSION)
DEB_ARCH    := $(shell dpkg --print-architecture 2>/dev/null)
DEB_STAGE   := dist/deb-stage
DEB_FILE    := dist/kc_$(DEB_VERSION)_$(DEB_ARCH).deb

deb: debian/control.in
	$(MAKE) clean
	$(MAKE) PREFIX=/usr
	rm -rf $(DEB_STAGE)
	$(MAKE) install DESTDIR=$(DEB_STAGE) PREFIX=/usr
	mkdir -p $(DEB_STAGE)/DEBIAN
	sed -e 's|__VERSION__|$(DEB_VERSION)|g' \
	    -e 's|__ARCH__|$(DEB_ARCH)|g' \
	    debian/control.in > $(DEB_STAGE)/DEBIAN/control
	mkdir -p dist
	fakeroot dpkg-deb --build --root-owner-group $(DEB_STAGE) $(DEB_FILE)
	@echo
	@echo "  built $(DEB_FILE)"

.PHONY: all clean install uninstall deb
