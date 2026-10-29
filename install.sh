#!/usr/bin/env bash
# Build and install namDHC for Linux (namdhc, namdhc-gui and chdman).
#
#   ./install.sh                 install for the current user into ~/.local
#   ./install.sh --system        install for all users into /usr/local (uses sudo)
#   ./install.sh --prefix DIR    install into DIR
#   ./install.sh --no-gui        command line tools only (no Qt Widgets needed)
#   ./install.sh --uninstall     remove a previous installation (same prefix options)
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

PREFIX="$HOME/.local"
SUDO=""
GUI=ON
UNINSTALL=0
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() { sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
	case "$1" in
		--system) PREFIX=/usr/local; SUDO=sudo ;;
		--prefix) PREFIX="$2"; shift ;;
		--prefix=*) PREFIX="${1#*=}" ;;
		--no-gui) GUI=OFF ;;
		--uninstall) UNINSTALL=1 ;;
		-j) JOBS="$2"; shift ;;
		-h|--help) usage; exit 0 ;;
		*) echo "Unknown option: $1"; usage; exit 2 ;;
	esac
	shift
done

[ "$(id -u)" = 0 ] && SUDO=""
BUILD_DIR="build-release"

msg() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWarning:\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mError:\033[0m %s\n' "$*" >&2; exit 1; }

if [ "$UNINSTALL" = 1 ]; then
	msg "Removing namDHC from $PREFIX"
	for f in bin/namdhc bin/namdhc-gui bin/chdman \
		share/applications/io.github.namdhc.desktop \
		share/icons/hicolor/scalable/apps/namdhc.svg; do
		if [ -e "$PREFIX/$f" ]; then
			$SUDO rm -f "$PREFIX/$f" && echo "  removed $PREFIX/$f"
		fi
	done
	command -v update-desktop-database >/dev/null && $SUDO update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
	msg "Done. Settings are kept in ~/.config/namDHC/ (delete that folder to reset them)."
	exit 0
fi

# ---------------------------------------------------------------- dependencies
missing=()
command -v cmake >/dev/null || missing+=(cmake)
command -v c++ >/dev/null || command -v g++ >/dev/null || command -v clang++ >/dev/null || missing+=("C++ compiler")

have_qt6() {
	for d in /usr/lib64/cmake/Qt6Core /usr/lib/cmake/Qt6Core /usr/lib/*/cmake/Qt6Core /usr/local/lib*/cmake/Qt6Core; do
		[ -d "$d" ] && return 0
	done
	[ -n "${CMAKE_PREFIX_PATH:-}" ] && return 0
	command -v qmake6 >/dev/null && return 0
	return 1
}
have_qt6 || missing+=("Qt6 development files")

if [ ${#missing[@]} -gt 0 ]; then
	echo "Missing: ${missing[*]}"
	echo
	echo "Install the build dependencies first:"
	if command -v dnf >/dev/null; then
		echo "  sudo dnf install cmake gcc-c++ qt6-qtbase-devel"
	elif command -v apt-get >/dev/null; then
		echo "  sudo apt install cmake g++ qt6-base-dev"
	elif command -v pacman >/dev/null; then
		echo "  sudo pacman -S --needed cmake gcc qt6-base"
	elif command -v zypper >/dev/null; then
		echo "  sudo zypper install cmake gcc-c++ qt6-base-devel"
	else
		echo "  cmake, a C++20 compiler and the Qt 6 base development package"
	fi
	echo
	echo "Optional, for .7z input: 7-Zip (7z/7zz). For .zip input: unzip or 7-Zip."
	exit 1
fi

# ---------------------------------------------------------------- build
msg "Configuring (prefix: $PREFIX, GUI: $GUI)"
# a cache generated from another source tree (e.g. after moving the project) makes cmake bail out
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] &&
	! grep -qxF "CMAKE_HOME_DIRECTORY:INTERNAL=$PWD" "$BUILD_DIR/CMakeCache.txt"; then
	msg "Removing stale build directory $BUILD_DIR"
	rm -rf "$BUILD_DIR"
fi
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DNAMDHC_BUILD_GUI="$GUI" >/dev/null

msg "Building with $JOBS jobs (chdman takes a minute the first time)"
cmake --build "$BUILD_DIR" -j "$JOBS"

msg "Installing into $PREFIX"
if [ -z "$SUDO" ] && [ ! -w "$(dirname "$PREFIX")" ] && [ ! -w "$PREFIX" ]; then
	die "$PREFIX is not writable; use --system or --prefix"
fi
$SUDO cmake --install "$BUILD_DIR" --strip

if [ "$GUI" = ON ]; then
	command -v update-desktop-database >/dev/null && $SUDO update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
	command -v gtk-update-icon-cache >/dev/null && $SUDO gtk-update-icon-cache -q -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
	# KDE keeps its own menu and icon caches; without this the launcher can show a generic icon until relogin
	$SUDO touch "$PREFIX/share/icons/hicolor" 2>/dev/null || true
	rm -f "${XDG_CACHE_HOME:-$HOME/.cache}/icon-cache.kcache"
	command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true
fi

# ---------------------------------------------------------------- PATH check
case ":$PATH:" in
	*":$PREFIX/bin:"*) ;;
	*)
		warn "$PREFIX/bin is not in your PATH."
		echo "  Add this line to ~/.bashrc (or ~/.zshrc) and open a new terminal:"
		echo "    export PATH=\"$PREFIX/bin:\$PATH\""
		;;
esac

command -v 7z >/dev/null || command -v 7zz >/dev/null || command -v 7za >/dev/null \
	|| warn "7-Zip not found: .7z input will be disabled (install p7zip or 7zip to enable it)."

echo
msg "Installed:"
echo "  $PREFIX/bin/namdhc       command line (try: namdhc --help, or namdhc --folder .)"
[ "$GUI" = ON ] && echo "  $PREFIX/bin/namdhc-gui   graphical interface (also in your applications menu)"
echo "  $PREFIX/bin/chdman       MAME chdman"
