#!/usr/bin/env bash

set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/artturihhaavisto-lang/imgview.git}"
BRANCH="${BRANCH:-master}"
APP_NAME="imgview"
APP_ID="imgview.desktop"
DEFAULT_PREFIX="${HOME}/.local"
SYSTEM_PREFIX="/usr/local"
INSTALL_PREFIX="${INSTALL_PREFIX:-$DEFAULT_PREFIX}"
INSTALL_DEPS=1
SET_MIME_DEFAULTS=1
FORCE_CLONE=0
USE_LOCAL_SOURCE=1
CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/imgview-installer"
REPO_DIR="${CACHE_ROOT}/repo"
ROOT_CMD=()
INSTALL_CMD=()

MIME_TYPES=(
  image/png
  image/jpeg
  image/gif
  image/webp
  image/bmp
  image/tiff
  image/x-tiff
  image/ico
  image/x-ico
  image/x-icon
  image/vnd.microsoft.icon
  image/xpm
  image/x-portable-pixmap
  image/x-portable-graymap
  image/x-portable-bitmap
  image/avif
  image/heic
  image/heif
  image/jxl
  image/svg+xml
  image/svg+xml-compressed
  image/x-tga
  image/vnd.ms-dds
  image/x-dds
  image/x-exr
  image/x-qoi
  image/x-icns
  image/x-xbitmap
  image/x-adobe-dng
  image/x-canon-cr2
  image/x-canon-crw
  image/x-nikon-nef
  image/x-olympus-orf
  image/x-pentax-pef
  image/x-sony-arw
  image/x-fuji-raf
  video/mp4
  video/x-matroska
  video/webm
  video/quicktime
  video/x-msvideo
  video/ogg
  video/mpeg
  video/mp2t
  video/x-ms-wmv
  video/x-flv
  video/3gpp
  video/3gpp2
  video/x-ms-asf
  video/x-dv
  video/x-h264
  video/x-h265
  application/mxf
  application/vnd.rn-realmedia
  application/vnd.rn-realmedia-vbr
  audio/mpeg
  audio/flac
  audio/x-flac
  audio/wav
  audio/x-wav
  audio/ogg
  audio/opus
  audio/mp4
  audio/aac
  audio/x-aiff
  audio/x-ms-wma
  audio/x-ape
)

log() {
  printf '[imgview-install] %s\n' "$*" >&2
}

die() {
  printf '[imgview-install] error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage:
  bash <(curl -fsSL URL) [options]
  bash ./install.sh [options]

Options:
  --user             Install for the current user under ~/.local (default)
  --system           Install system-wide under /usr/local
  --prefix PATH      Install under a custom prefix
  --branch NAME      Install from a different git branch
  --repo URL         Install from a different repository URL
  --skip-deps        Do not install system packages
  --no-defaults      Do not set imgview as the default media handler
  --force-clone      Re-download even if a cached clone already exists
  -h, --help         Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      INSTALL_PREFIX="$DEFAULT_PREFIX"
      shift
      ;;
    --system)
      INSTALL_PREFIX="$SYSTEM_PREFIX"
      shift
      ;;
    --prefix)
      [[ $# -ge 2 ]] || die "--prefix requires a value"
      [[ -n "$2" ]] || die "--prefix requires a non-empty value"
      INSTALL_PREFIX="$2"
      shift 2
      ;;
    --branch)
      [[ $# -ge 2 ]] || die "--branch requires a value"
      [[ -n "$2" ]] || die "--branch requires a non-empty value"
      BRANCH="$2"
      USE_LOCAL_SOURCE=0
      shift 2
      ;;
    --repo)
      [[ $# -ge 2 ]] || die "--repo requires a value"
      [[ -n "$2" ]] || die "--repo requires a non-empty value"
      REPO_URL="$2"
      USE_LOCAL_SOURCE=0
      shift 2
      ;;
    --skip-deps)
      INSTALL_DEPS=0
      shift
      ;;
    --no-defaults)
      SET_MIME_DEFAULTS=0
      shift
      ;;
    --force-clone)
      FORCE_CLONE=1
      USE_LOCAL_SOURCE=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

nearest_existing_dir() {
  local path
  path="$1"

  while [[ ! -e "$path" && "$path" != "/" ]]; do
    path="$(dirname "$path")"
  done

  [[ -d "$path" ]] || path="$(dirname "$path")"
  printf '%s\n' "$path"
}

prefix_needs_root() {
  local existing_dir

  [[ "$(id -u)" -eq 0 ]] && return 1

  existing_dir="$(nearest_existing_dir "$INSTALL_PREFIX")"
  [[ -w "$existing_dir" ]] && return 1

  return 0
}

setup_privileges() {
  if command -v sudo >/dev/null 2>&1 && [[ "$(id -u)" -ne 0 ]]; then
    ROOT_CMD=(sudo)
  fi

  if prefix_needs_root; then
    if [[ "${#ROOT_CMD[@]}" -eq 0 ]]; then
      die "installing to ${INSTALL_PREFIX} requires root; run as root, install sudo, or use --user"
    fi
    INSTALL_CMD=("${ROOT_CMD[@]}")
  fi
}

run_root() {
  if [[ "$(id -u)" -ne 0 && "${#ROOT_CMD[@]}" -eq 0 ]]; then
    die "installing dependencies requires root; run as root, install sudo, or pass --skip-deps"
  fi

  "${ROOT_CMD[@]}" "$@"
}

run_install() {
  "${INSTALL_CMD[@]}" "$@"
}

detect_pkg_manager() {
  local candidate
  for candidate in apt-get dnf pacman zypper; do
    if command -v "$candidate" >/dev/null 2>&1; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

install_deps() {
  local pkgm

  if [[ "$INSTALL_DEPS" -eq 0 ]]; then
    log "skipping dependency installation"
    return 0
  fi

  pkgm="$(detect_pkg_manager || true)"
  if [[ -z "$pkgm" ]]; then
    log "no supported package manager detected; skipping dependency installation"
    return 0
  fi

  case "$pkgm" in
    apt-get)
      log "installing dependencies with apt"
      run_root apt-get update
      run_root apt-get install -y build-essential make pkg-config libgtk-3-dev libgif-dev xdg-utils git libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav
      ;;
    dnf)
      log "installing dependencies with dnf"
      run_root dnf install -y gcc make pkgconf-pkg-config gtk3-devel giflib-devel xdg-utils git gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-base gstreamer1-plugins-good gstreamer1-plugins-bad-free gstreamer1-plugins-bad-free-extras gstreamer1-plugins-ugly-free gstreamer1-plugin-libav
      ;;
    pacman)
      log "installing dependencies with pacman"
      # Avoid partial upgrades on Arch; sync + install without -u can break
      # version-locked packages when core libraries move ahead of installed plugins.
      run_root pacman -Syu --noconfirm --needed base-devel pkgconf gtk3 giflib libheif xdg-utils git gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav
      ;;
    zypper)
      log "installing dependencies with zypper"
      run_root zypper --non-interactive install gcc make pkg-config gtk3-devel giflib-devel xdg-utils git gstreamer-devel gstreamer-plugins-base-devel gstreamer-plugins-base gstreamer-plugins-good gstreamer-plugins-bad gstreamer-plugins-ugly
      # LibAV-backed decoding is provided by repositories such as Packman or
      # PackageHub on some openSUSE variants. Try it without making those
      # optional repositories a requirement for installing imgview itself.
      if run_root zypper --non-interactive install gstreamer-plugins-libav; then
        log "installed the GStreamer LibAV codec plugin"
      else
        log "GStreamer LibAV plugin was unavailable; enable a suitable repository (such as Packman) for broader codec support"
      fi
      ;;
  esac
}

fetch_repo() {
  need_cmd git

  mkdir -p "$CACHE_ROOT"

  if [[ -d "${REPO_DIR}/.git" && "$FORCE_CLONE" -eq 0 ]]; then
    log "updating cached repository"
    git -C "$REPO_DIR" fetch --depth 1 origin "$BRANCH"
    git -C "$REPO_DIR" checkout -q FETCH_HEAD
  else
    rm -rf "$REPO_DIR"
    log "cloning repository"
    git clone --depth 1 --branch "$BRANCH" "$REPO_URL" "$REPO_DIR"
  fi
}

source_dir() {
  local script_dir
  script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd -P || true)"

  if [[ "$USE_LOCAL_SOURCE" -eq 1 && -n "$script_dir" && -f "${script_dir}/Makefile" ]]; then
    printf '%s\n' "$script_dir"
    return 0
  fi

  fetch_repo
  printf '%s\n' "$REPO_DIR"
}

write_desktop_file() {
  local desktop_path exec_path
  desktop_path="$1"
  exec_path="$2"

  cat >"$desktop_path" <<EOF
[Desktop Entry]
Name=imgview
Comment=Image, video and audio viewer
Exec=${exec_path} %F
Icon=image-viewer
Terminal=false
Type=Application
MimeType=$(printf '%s;' "${MIME_TYPES[@]}")
Categories=Graphics;AudioVideo;Viewer;
EOF
}

install_desktop_entry() {
  local desktop_dir desktop_path mime
  desktop_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  desktop_path="${desktop_dir}/${APP_ID}"

  mkdir -p "$desktop_dir"
  write_desktop_file "$desktop_path" "$1"

  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$desktop_dir" >/dev/null 2>&1 || true
  fi

  if [[ "$SET_MIME_DEFAULTS" -eq 0 ]]; then
    log "desktop entry installed; default media handlers unchanged"
    return 0
  fi

  if command -v xdg-mime >/dev/null 2>&1; then
    for mime in "${MIME_TYPES[@]}"; do
      xdg-mime default "$APP_ID" "$mime"
    done
  else
    log "xdg-mime not found; desktop entry was installed but defaults were not changed"
  fi
}

main() {
  setup_privileges
  install_deps

  need_cmd make
  need_cmd pkg-config

  local src_dir bin_dir bin_path
  src_dir="$(source_dir)"
  bin_dir="${INSTALL_PREFIX}/bin"
  bin_path="${bin_dir}/${APP_NAME}"

  [[ -f "${src_dir}/Makefile" ]] || die "missing Makefile in ${src_dir}"

  log "installing into ${INSTALL_PREFIX}"
  make -C "$src_dir" clean all
  run_install mkdir -p "$bin_dir"
  run_install install -m 755 "${src_dir}/build/${APP_NAME}" "$bin_path"

  install_desktop_entry "$bin_path"

  log "installation complete"
  log "binary: ${bin_path}"
  log "desktop file: ${XDG_DATA_HOME:-$HOME/.local/share}/applications/${APP_ID}"
  if [[ ":$PATH:" != *":${bin_dir}:"* ]]; then
    log "add ${bin_dir} to PATH if the command is not found in new shells"
  fi
}

main "$@"
