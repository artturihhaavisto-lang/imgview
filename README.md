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
