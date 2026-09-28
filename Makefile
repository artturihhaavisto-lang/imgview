CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS ?= -O3 -pipe
CPPFLAGS += -DG_DISABLE_CAST_CHECKS
LDFLAGS ?= -Wl,-O1,--as-needed
WARNFLAGS ?= -Wall -Wextra -Wpedantic
PACKAGES := gtk+-3.0 gstreamer-app-1.0 gstreamer-video-1.0
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PACKAGES))
GTK_LIBS := $(shell $(PKG_CONFIG) --libs $(PACKAGES)) -lgif

IMG_TARGET := build/imgview
IMG_SRC := src/imgview.c

.PHONY: all clean install uninstall test test-ui

all: $(IMG_TARGET)

$(IMG_TARGET): $(IMG_SRC) src/gif-stream.h
	install -d "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

install: all
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 "$(IMG_TARGET)" "$(DESTDIR)$(BINDIR)/imgview"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/imgview"

# Integration fixtures require ffmpeg; tests use real decoders without a GUI.
build/test.mp4:
	install -d build
	ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=160x90:rate=20 -f lavfi -i sine=frequency=440 -t 8 -c:v libx264 -pix_fmt yuv420p -c:a aac -y $@

build/test.gif: build/test.mp4
	ffmpeg -hide_banner -loglevel error -i $< -t 1 -vf fps=10 -y $@

build/test.wav: build/test.mp4
	ffmpeg -hide_banner -loglevel error -i $< -vn -c:a pcm_s16le -y $@

build/test.png: build/test.mp4
	ffmpeg -hide_banner -loglevel error -i $< -frames:v 1 -y $@

build/test.webp: build/test.png
	ffmpeg -hide_banner -loglevel error -i $< -frames:v 1 -y $@

build/test.avif: build/test.png
	ffmpeg -hide_banner -loglevel error -i $< -frames:v 1 -c:v libaom-av1 -still-picture 1 -y $@

build/test.asf: build/test.mp4
	ffmpeg -hide_banner -loglevel error -i $< -t 2 -an -c:v msmpeg4 -y $@

build/test-playback: tests/playback.c $(IMG_SRC) src/gif-stream.h
	install -d build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

build/test-gif: tests/gif-stream.c src/gif-stream.h
	install -d build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

build/test-canvas: tests/canvas.c $(IMG_SRC) src/gif-stream.h
	install -d build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

build/test-frame-clock: tests/frame-clock.c $(IMG_SRC) src/gif-stream.h
	install -d build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

test: build/test-canvas build/test-gif build/test-playback build/test.mp4 build/test.gif build/test.png build/test.wav build/test.webp build/test.asf
	./build/test-canvas
	./build/test-gif
	./build/test-playback build/test.mp4 build/test.gif build/test.png build/test.wav build/test.webp tests/format.svg build/test.asf

build/test-progress: tests/progress.c $(IMG_SRC) src/gif-stream.h
	install -d build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

build/test-audio-ui: tests/audio-ui.c $(IMG_SRC) src/gif-stream.h
	install -d build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

build/test-playlist: tests/playlist.c $(IMG_SRC) src/gif-stream.h
	install -d build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(GTK_CFLAGS) -o $@ $< $(LDFLAGS) $(GTK_LIBS) -lm

test-ui: build/test-progress build/test-audio-ui build/test-playlist build/test.mp4 build/test.png build/test.webp build/test.wav
	./build/test-progress build/test.mp4 build/test.webp
	./build/test-audio-ui build/test.wav
	./build/test-playlist

clean:
	rm -rf build
