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
git clone https://github.com/<username>/imgview.git
cd imgview
sudo ln -s $(pwd)/imgview /usr/local/bin/imgview
```

## Set as Default

```bash
cp imgview.desktop ~/.local/share/applications/
xdg-mime default imgview.desktop image/png image/jpeg image/gif image/webp image/bmp image/tiff
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
