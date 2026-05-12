CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS ?= -O3 -pipe
CPPFLAGS += -DG_DISABLE_CAST_CHECKS
LDFLAGS ?= -Wl,-O1,--as-needed
WARNFLAGS ?= -Wall -Wextra -Wpedantic
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

IMG_TARGET := build/imgview
IMG_SRC := src/imgview.c

.PHONY: all clean install uninstall

all: $(IMG_TARGET)

$(IMG_TARGET): $(IMG_SRC)
	install -d "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

install: all
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 "$(IMG_TARGET)" "$(DESTDIR)$(BINDIR)/imgview"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/imgview"

clean:
	rm -rf build
