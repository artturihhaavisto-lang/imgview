# imgview

A lightweight pan/zoom image viewer inspired by Photoshop, without the tools.

## Features

- **Pan**: Left-click and drag to pan across the image
- **Zoom**: Scroll wheel to zoom in/out (centered on cursor)
- **Directory navigation**: Browse images in a folder
- **Rotation & flip**: Rotate and flip images
- **Fullscreen**: Press F11
- **Slideshow**: Press S to auto-advance through directory

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

## Requirements

- Python 3.6+
- GTK3
- GdkPixbuf
- Cairo

## Usage

```bash
imgview <file|dir> [...]
```
