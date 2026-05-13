# imgview

A lightweight native GTK image viewer inspired by Photoshop, without the tools.

## Features

- **Pan**: Left-click and drag to pan across the image
- **Zoom**: Scroll wheel to zoom in/out (centered on cursor)
- **Directory navigation**: Browse images in a folder
- **Rotation & flip**: Rotate and flip images
- **Fullscreen**: Press F11
- **Slideshow**: Press S to auto-advance through directory

## Screenshots
<img width="1898" height="1020" alt="image" src="https://github.com/user-attachments/assets/63c44659-9cfa-4d5f-a75a-a9132981d0c4" />



## Keybindings

| Key | Action |
|-----|--------|
| `Right` / `n` / `l` / `Page Down` | Next image |
| `Left` / `p` / `h` / `Page Up` | Previous image |
| `Home` / `End` | First / last image |
| `+` / `=` | Zoom in |
| `-` | Zoom out |
| `0` | Actual size (1:1) |
| `f` / `w` | Fit to window |
| `r` | Rotate clockwise |
| `R` | Rotate counter-clockwise |
| `/` | Flip horizontal |
| `F11` | Fullscreen |
| `i` | Toggle info bar |
| `s` | Slideshow |
| `q` / `Esc` | Quit |

## Install

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/artturihhaavisto-lang/imgview/master/install.sh)
```

This installs `imgview` for the current user under `~/.local`, creates a desktop entry,
and sets it as the default handler for common image MIME types.

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
dependencies. This avoids partial-upgrade conflicts with version-locked packages such
as GStreamer plugins. If you manage dependencies yourself, install the packages listed
below and pass `--skip-deps`.

## Build

```bash
make
./build/imgview <file|dir> [...]
./build/vidview <file|dir> [...]
```

## Requirements

- C compiler
- make
- pkg-config
- GTK3 development headers
- GStreamer development headers and base/good plugins

Package names used by the installer:

| Distribution | Packages |
|--------------|----------|
| Arch | `base-devel pkgconf gtk3 gstreamer gst-plugins-base gst-plugins-good xdg-utils git` |
| Debian/Ubuntu | `build-essential make pkg-config libgtk-3-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good xdg-utils git` |
| Fedora | `gcc make pkgconf-pkg-config gtk3-devel gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-good xdg-utils git` |
| openSUSE | `gcc make pkg-config gtk3-devel gstreamer-devel gstreamer-plugins-base-devel gstreamer-plugins-good xdg-utils git` |

## Usage

```bash
imgview <file|dir> [...]
vidview <file|dir> [...]
```

## vidview

`vidview` is a lightweight GTK/GStreamer video player with the same dark, compact style.

| Key | Action |
|-----|--------|
| `Space` / `k` | Play / pause |
| `Right` / `l` | Seek forward 5s |
| `Left` / `h` | Seek backward 5s |
| `Page Down` / `Page Up` | Seek forward / backward 60s |
| `Home` / `End` | Start / near end |
| `n` / `Down` | Next video |
| `p` / `Up` | Previous video |
| `+` / `-` | Volume up / down |
| `m` | Mute |
| `f` / `F11` | Fullscreen |
| `i` | Toggle controls |
| `q` / `Esc` | Quit |
