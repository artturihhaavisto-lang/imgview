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
VID_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0 gstreamer-1.0 gstreamer-video-1.0)
VID_LIBS := $(shell $(PKG_CONFIG) --libs gtk+-3.0 gstreamer-1.0 gstreamer-video-1.0)

IMG_TARGET := build/imgview
VID_TARGET := build/vidview
IMG_SRC := src/imgview.c
VID_SRC := src/vidview.c

.PHONY: all clean install uninstall

all: $(IMG_TARGET) $(VID_TARGET)

$(IMG_TARGET): $(IMG_SRC)
	install -d "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

$(VID_TARGET): $(VID_SRC)
	install -d "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(VID_CFLAGS) -o $@ $< $(LDFLAGS) $(VID_LIBS) -lm

install: all
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 "$(IMG_TARGET)" "$(DESTDIR)$(BINDIR)/imgview"
	install -m 755 "$(VID_TARGET)" "$(DESTDIR)$(BINDIR)/vidview"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/imgview" "$(DESTDIR)$(BINDIR)/vidview"

clean:
	rm -rf build
