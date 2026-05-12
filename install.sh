#!/usr/bin/env bash

set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/artturihhaavisto-lang/imgview.git}"
BRANCH="${BRANCH:-master}"
APP_NAME="imgview"
APP_ID="imgview.desktop"
DEFAULT_PREFIX="${HOME}/.local"
SYSTEM_PREFIX="/usr/local"
INSTALL_PREFIX="${INSTALL_PREFIX:-$DEFAULT_PREFIX}"
INSTALL_MODE="user"
FORCE_CLONE=0

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
Usage: bash <(curl -fsSL URL) [options]

Options:
  --user             Install for the current user under ~/.local (default)
  --system           Install system-wide under /usr/local
  --prefix PATH      Install under a custom prefix
  --branch NAME      Install from a different git branch
  --repo URL         Install from a different repository URL
  --force-clone      Re-download even if a cached clone already exists
  -h, --help         Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      INSTALL_MODE="user"
      INSTALL_PREFIX="$DEFAULT_PREFIX"
      shift
      ;;
    --system)
      INSTALL_MODE="system"
      INSTALL_PREFIX="$SYSTEM_PREFIX"
      shift
      ;;
    --prefix)
      [[ $# -ge 2 ]] || die "--prefix requires a value"
      INSTALL_PREFIX="$2"
      INSTALL_MODE="custom"
      shift 2
      ;;
    --branch)
      [[ $# -ge 2 ]] || die "--branch requires a value"
      BRANCH="$2"
      shift 2
      ;;
    --repo)
      [[ $# -ge 2 ]] || die "--repo requires a value"
      REPO_URL="$2"
      shift 2
      ;;
    --force-clone)
      FORCE_CLONE=1
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

if command -v sudo >/dev/null 2>&1 && [[ "$(id -u)" -ne 0 ]]; then
  ROOT_CMD=(sudo)
else
  ROOT_CMD=()
fi

if [[ "$INSTALL_MODE" == "system" || "$INSTALL_PREFIX" != "$DEFAULT_PREFIX" ]]; then
  if command -v sudo >/dev/null 2>&1 && [[ "$(id -u)" -ne 0 ]]; then
    INSTALL_CMD=(sudo)
  else
    INSTALL_CMD=()
  fi
else
  INSTALL_CMD=()
fi

run_root() {
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
  pkgm="$(detect_pkg_manager || true)"
  if [[ -z "$pkgm" ]]; then
    log "no supported package manager detected; skipping dependency installation"
    return 0
  fi

  case "$pkgm" in
    apt-get)
      log "installing dependencies with apt"
      run_root apt-get update
      run_root apt-get install -y build-essential make pkg-config libgtk-3-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good xdg-utils git
      ;;
    dnf)
      log "installing dependencies with dnf"
      run_root dnf install -y gcc make pkgconf-pkg-config gtk3-devel gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-good xdg-utils git
      ;;
    pacman)
      log "installing dependencies with pacman"
      run_root pacman -Sy --noconfirm base-devel pkgconf gtk3 gstreamer gst-plugins-base gst-plugins-good xdg-utils git
      ;;
    zypper)
      log "installing dependencies with zypper"
      run_root zypper --non-interactive install gcc make pkg-config gtk3-devel gstreamer-devel gstreamer-plugins-base-devel gstreamer-plugins-good xdg-utils git
      ;;
  esac
}

fetch_repo() {
  need_cmd git

  local cache_root repo_dir
  cache_root="${XDG_CACHE_HOME:-$HOME/.cache}/imgview-installer"
  repo_dir="${cache_root}/repo"

  mkdir -p "$cache_root"

  if [[ -d "${repo_dir}/.git" && "$FORCE_CLONE" -eq 0 ]]; then
    log "updating cached repository"
    git -C "$repo_dir" fetch --depth 1 origin "$BRANCH"
    git -C "$repo_dir" checkout -q FETCH_HEAD
  else
    rm -rf "$repo_dir"
    log "cloning repository"
    git clone --depth 1 --branch "$BRANCH" "$REPO_URL" "$repo_dir"
  fi

}

write_desktop_file() {
  local desktop_path exec_path
  desktop_path="$1"
  exec_path="$2"

  cat >"$desktop_path" <<EOF
[Desktop Entry]
Name=imgview
Comment=Pan/zoom image viewer
Exec=${exec_path} %F
Icon=image-viewer
Terminal=false
Type=Application
MimeType=$(printf '%s;' "${MIME_TYPES[@]}")
Categories=Graphics;Viewer;
EOF
}

set_defaults() {
  local desktop_dir desktop_path mime
  desktop_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
  desktop_path="${desktop_dir}/${APP_ID}"

  mkdir -p "$desktop_dir"
  write_desktop_file "$desktop_path" "$1"

  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$desktop_dir" >/dev/null 2>&1 || true
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
  install_deps

  need_cmd make
  need_cmd pkg-config

  local repo_dir bin_dir bin_path vid_bin_path
  fetch_repo
  repo_dir="${XDG_CACHE_HOME:-$HOME/.cache}/imgview-installer/repo"
  bin_dir="${INSTALL_PREFIX}/bin"
  bin_path="${bin_dir}/${APP_NAME}"
  vid_bin_path="${bin_dir}/vidview"

  log "installing into ${INSTALL_PREFIX}"
  make -C "$repo_dir" clean all
  run_install mkdir -p "$bin_dir"
  run_install install -m 755 "${repo_dir}/build/${APP_NAME}" "$bin_path"
  run_install install -m 755 "${repo_dir}/build/vidview" "$vid_bin_path"

  set_defaults "$bin_path"

  log "installation complete"
  log "binary: ${bin_path}"
  log "video player: ${vid_bin_path}"
  log "desktop file: ${XDG_DATA_HOME:-$HOME/.local/share}/applications/${APP_ID}"
  if [[ ":$PATH:" != *":${bin_dir}:"* ]]; then
    log "add ${bin_dir} to PATH if the command is not found in new shells"
  fi
}

main "$@"
