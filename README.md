# imgview

A lightweight native GTK image, video and audio viewer inspired by Photoshop, without the tools.

## Features

- **Animated GIFs**: Automatic playback with frame timing, including while panning, zooming, rotating, or flipping
- **Video with audio**: Autoplay MP4, MKV, WebM, MOV, AVI, and other common video files, with pause, seek, and mute controls
- Videos initially fit the viewing area and keep their zoom relative to it when the window is resized, including after manual zoom changes.
- **Audio files**: Play MP3, FLAC, WAV, Ogg, Opus, M4A, AAC and other common audio formats. Click the viewing area or press Space to pause/resume; use the timeline to seek and Ctrl+scroll for volume.
- **Video progress bar**: Click or drag the timeline above the bottom info bar to jump to any point; seeking keeps the current play/pause state
- **Pan**: Left-click and drag to pan across the image
- **Efficient canvas movement**: Dragging redraws only the old/new media bounds, paced by GTK's display frame clock with no fixed FPS limit. Native Wayland is preferred for high-refresh monitors; XWayland can fall back to 60 Hz. Still images reuse a scaled RAM surface (at most 32 MiB) to avoid resampling each movement; oversized zooms and animated media use the original surface. No extra polling timer runs for panning.
- **Click to pause**: Click the video to pause/resume; dragging pans without changing playback
- **Custom header**: Matching dark title bar with filename and window controls
- **Metadata**: Hover at the right edge to slide out file details, resolution and available media tags, duration, frame rate and codec information. Move away to hide it. Metadata and fullscreen hover controls animate without resizing the media.
- **Playlist**: Hover at the left edge for the current folder's media, with compact filenames, file types, and image/video previews. Click an entry to open it. The animated panel works in fullscreen too. All folder previews start loading in the background when media opens (two at a time), and stay in memory only for that session. Closing the player frees them; no preview files accumulate on disk. Older disk previews are removed automatically on the next launch.
- **Window geometry**: Remembers the last window size and maximized state across launches; fullscreen does not replace the saved geometry. Native Wayland leaves position to the compositor. On X11, position is also restored; `GDK_BACKEND=x11 imgview <file>` explicitly selects X11/XWayland if needed, with potentially lower frame pacing. Settings are stored in `$XDG_CONFIG_HOME/imgview/window.ini` (normally `~/.config/imgview/window.ini`).
- **Volume**: Ctrl+scroll adjusts volume in 2% steps up to 200%, with a persistent level bar in the bottom UI
- Playback status, seeking and volume controls appear only for video/audio. Images and GIFs show filename, dimensions, index and zoom.
- **More formats**: Image discovery automatically includes every installed GdkPixbuf decoder (including AVIF, HEIC/HEIF, JPEG XL, SVG/SVGZ, TGA, DDS, EXR, QOI and camera RAW when available). Video discovery also recognizes VOB, MXF, ASF, DV, 3G2, F4V, RealMedia, NUT, IVF and raw H.264/H.265 streams. Decoding depends on installed image loaders and GStreamer codecs.
- **Zoom**: Scroll wheel to zoom in/out (centered on cursor)
- **Directory navigation**: Browse images in a folder
- **Rotation & flip**: Rotate and flip images
- **Fullscreen**: Press F or F11; Escape returns to windowed mode
- Fullscreen video stays centered and scales to the available area (with black letterboxing to preserve aspect ratio); canvas panning and zoom are disabled until you exit fullscreen.
- In fullscreen, hover at the top edge for the header or bottom edge for playback controls. Each bar hides when you move away, without resizing the video; the seek controls stay visible while scrubbing.
- **Slideshow**: Press S to auto-advance through directory

## Screenshots
<img width="1898" height="1020" alt="image" src="https://github.com/user-attachments/assets/63c44659-9cfa-4d5f-a75a-a9132981d0c4" />



## Keybindings

| Key | Action |
|-----|--------|
| `Right` / `n` / `l` / `Page Down` | Next image or video |
| `Left` / `p` / `h` / `Page Up` | Previous image or video |
| `Space` | Pause/resume video; replay when ended |
| `Shift+Left` / `Shift+Right` | Seek video backward/forward 5 seconds |
| `m` | Mute/unmute video audio |
| `Ctrl+scroll` | Adjust volume (0–200%, 2% steps); also unmutes |
| `Home` / `End` | First / last image |
| `+` / `=` | Zoom in |
| `-` | Zoom out |
| `0` | Actual size (1:1) |
| `c` | Resize the window to the video's native resolution at 100%, without letterboxing; exits fullscreen/maximized mode |
| `w` | Fit to window |
| `r` | Rotate clockwise |
| `R` | Rotate counter-clockwise |
| `/` | Flip horizontal |
| `f` / `F11` | Toggle fullscreen |
| `i` | Toggle info bar |
| `s` | Slideshow |
| `q` / `Esc` | Quit |

## Install

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/artturihhaavisto-lang/imgview/master/install.sh)
```

This installs `imgview` for the current user under `~/.local`, creates a desktop entry,
and sets it as the default handler for common image and video MIME types.

For a system-wide install:

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/artturihhaavisto-lang/imgview/master/install.sh) --system
```

From a local checkout:

```bash
git clone https://github.com/artturihhaavisto-lang/imgview.git
cd imgview
bash ./install.sh
```

The local checkout path builds and installs the files you have on disk. Use
`--branch`, `--repo`, or `--force-clone` when you want the installer to fetch from
GitHub instead.

### Installer options

| Option | Description |
|--------|-------------|
| `--user` | Install under `~/.local` for the current user. This is the default. |
| `--system` | Install the executable under `/usr/local`. The script uses `sudo` when needed. |
| `--prefix PATH` | Install under a custom prefix. User-writable prefixes do not require `sudo`. |
| `--skip-deps` | Skip package manager dependency installation. |
| `--no-defaults` | Install the desktop entry without changing default image handlers. |
| `--branch NAME` | Fetch and install a different Git branch. |
| `--repo URL` | Fetch and install from a different repository URL. |
| `--force-clone` | Re-download the cached repository before installing. |

On Arch-based systems, the installer uses `pacman -Syu --needed` before installing
dependencies. This avoids partial-upgrade conflicts. If you manage dependencies
yourself, install the packages listed below and pass `--skip-deps`.

## Build

```bash
make
./build/imgview <file|dir> [...]
```

Run `make test` to generate small media fixtures with FFmpeg and check playback,
pause/seek/replay, transforms, GIF/image switching, and resource cleanup. Tests
run without a display and use a silent audio sink. They require FFmpeg and the
GStreamer codecs listed below.
Run `make test-ui` with a display available to check the video progress bar;
it briefly opens a muted test video and closes automatically.

## Requirements

- C compiler
- make
- pkg-config
- GTK3 development headers
- GStreamer development headers (core, app, and video), plus base/good/bad plugins
- GStreamer libav plugin for additional codecs such as H.264, where available

Package names used by the installer:

| Distribution | Packages |
|--------------|----------|
| Arch | `base-devel pkgconf gtk3 giflib libheif xdg-utils git gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-libav` |
| Debian/Ubuntu | `build-essential make pkg-config libgtk-3-dev libgif-dev xdg-utils git libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav` |
| Fedora | `gcc make pkgconf-pkg-config gtk3-devel giflib-devel xdg-utils git gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-base gstreamer1-plugins-good gstreamer1-plugins-bad-free` |
| openSUSE | `gcc make pkg-config gtk3-devel giflib-devel xdg-utils git gstreamer-devel gstreamer-plugins-base-devel gstreamer-plugins-base gstreamer-plugins-good gstreamer-plugins-bad` |

Video codec availability depends on your distribution's installed GStreamer plugins.
GIFs decode incrementally with giflib: decoded frame memory stays bounded as the
animation grows. Transparency, frame disposal, interlacing and finite/infinite
loop counts are supported. Video pixbufs share their GStreamer pixel buffers,
removing the per-frame RGB copy while keeping buffers alive until drawing finishes.
Videos stop on their final frame; press Space to replay. Pan, zoom, rotation, and
flipping work during playback. Directory navigation includes both images and videos.
Slideshow mode retains its four-second advance interval, including for videos.

## Usage

```bash
imgview <file|dir> [...]
```
