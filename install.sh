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
      run_root apt-get install -y python3 python3-gi python3-cairo gir1.2-gtk-3.0 libgdk-pixbuf-2.0-0 xdg-utils git
      ;;
    dnf)
      log "installing dependencies with dnf"
      run_root dnf install -y python3 python3-gobject python3-cairo gtk3 gdk-pixbuf2 xdg-utils git
      ;;
    pacman)
      log "installing dependencies with pacman"
      run_root pacman -Sy --noconfirm python python-gobject python-cairo gtk3 gdk-pixbuf2 xdg-utils git
      ;;
    zypper)
      log "installing dependencies with zypper"
      run_root zypper --non-interactive install python3 python3-gobject python3-cairo gtk3 gdk-pixbuf-loader-rsvg xdg-utils git
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
  need_cmd curl

  install_deps

  local install_dir bin_dir bin_path app_path
  fetch_repo
  install_dir="${INSTALL_PREFIX}/lib/${APP_NAME}"
  bin_dir="${INSTALL_PREFIX}/bin"
  bin_path="${bin_dir}/${APP_NAME}"
  app_path="${install_dir}/${APP_NAME}"

  log "installing into ${INSTALL_PREFIX}"
  run_install mkdir -p "$install_dir" "$bin_dir"
  run_install install -m 755 "${XDG_CACHE_HOME:-$HOME/.cache}/imgview-installer/repo/imgview" "$app_path"
  run_install ln -sf "$app_path" "$bin_path"

  set_defaults "$app_path"

  log "installation complete"
  log "binary: ${bin_path}"
  log "desktop file: ${XDG_DATA_HOME:-$HOME/.local/share}/applications/${APP_ID}"
  if [[ ":$PATH:" != *":${bin_dir}:"* ]]; then
    log "add ${bin_dir} to PATH if the command is not found in new shells"
  fi
}

main "$@"
