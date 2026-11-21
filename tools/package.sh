#!/bin/sh
# Packages the MonoGame sample as a self-contained, runnable build per platform under dist/ (git-ignored):
# dist/psiv-sample-VERSION-RID/ holds the published sample with the psiv_session library for that platform beside it,
# game/data and core/data (game.json reaches core's data as ../../core/data), game/assets when the repo has one, and a
# README.txt; for macOS also dist/psiv-sample-VERSION-RID.app. VERSION is the library's PSIV_BINDING_VERSION.
# Native libraries: macOS with the host's clang++ (-arch per target), Linux and Windows in the Docker images that
# tools/linux-check.sh and tools/windows-check.sh build (the repo mounted read-only and copied inside; only the library
# comes back). Every build goes under dist/.build, never into bindings/build or a project's bin/ and obj/.
# Each package is checked: the headless smoke run (scripts/smoke.txt, 2000 frames, fresh saves) on macOS (the .app's
# launcher too) and in Docker for Linux (with PSIV_PACKAGE_WINDOW=1, the default, also the window under Xvfb); for
# Windows, the layout and the DLL's exports only. A failed check fails the script; a target whose native library
# cannot be built here is skipped with the reason, and never packaged without it.
# Usage: tools/package.sh [RID ...]   (default: osx-arm64 osx-x64 linux-x64 linux-arm64 win-x64)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIST=$ROOT/dist
WORK=$DIST/.build
DOTNET=${DOTNET:-$HOME/.dotnet/dotnet}
WINDOW=${PSIV_PACKAGE_WINDOW:-1}
TARGETS=${*:-osx-arm64 osx-x64 linux-x64 linux-arm64 win-x64}
SAMPLE=$ROOT/bindings/monogame/Psiv.Sample
SCRIPTS=$SAMPLE/scripts
VERSION=$(sed -n 's/^#define PSIV_BINDING_VERSION "\(.*\)"$/\1/p' "$ROOT/bindings/c/psiv_session.cpp")
APP_NAME="Psiv Sample"
SUMMARY=

if [ -z "$VERSION" ]; then
	echo "package: cannot read PSIV_BINDING_VERSION from bindings/c/psiv_session.cpp" >&2
	exit 1
fi
if [ ! -x "$DOTNET" ]; then
	echo "package: no dotnet at $DOTNET (pass DOTNET=/path/to/dotnet)" >&2
	exit 2
fi

note() {
	SUMMARY="$SUMMARY
  $1"
}

skip() {
	echo "== $1: SKIPPED: $2"
	note "$1: skipped: $2"
}

docker_ok() {
	command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1
}

# The image tools/linux-check.sh builds for a platform: psiv-linux-check<platform> with PSIV_LINUX_PLATFORM set, or
# psiv-linux-check for the host's own architecture.
linux_image() {
	named=psiv-linux-check$(echo "$1" | tr -c "a-z0-9\n" "-")
	if docker image inspect "$named" >/dev/null 2>&1; then
		echo "$named"
	elif [ "$(docker info --format '{{.Architecture}}')" = "$2" ] && docker image inspect psiv-linux-check >/dev/null 2>&1; then
		echo psiv-linux-check
	else
		return 1
	fi
}

# Prints why this machine cannot build the target's native library, or fails when it can.
cannot_build() {
	case $1 in
		osx-*)
			arch=arm64
			[ "$1" = osx-x64 ] && arch=x86_64
			if [ "$(uname -s)" != Darwin ]; then
				echo "macOS libraries are built on macOS only"
			elif ! echo "int main() { return 0; }" | clang++ -arch $arch -x c++ - -o /dev/null >/dev/null 2>&1; then
				echo "clang++ cannot build for $arch here"
			else
				return 1
			fi
			;;
		linux-*)
			platform=linux/amd64 arch=x86_64
			[ "$1" = linux-arm64 ] && platform=linux/arm64 arch=aarch64
			if ! docker_ok; then
				echo "Docker is not available"
			elif ! linux_image $platform $arch >/dev/null; then
				echo "no psiv-linux-check image for $platform (tools/linux-check.sh builds it, with PSIV_LINUX_PLATFORM=$platform for another architecture)"
			else
				return 1
			fi
			;;
		win-x64)
			if ! docker_ok; then
				echo "Docker is not available"
			elif ! docker image inspect psiv-windows-check >/dev/null 2>&1; then
				echo "no psiv-windows-check image (tools/windows-check.sh builds it)"
			else
				return 1
			fi
			;;
	esac
}

# The native library for one target into $WORK/native/RID.
build_native() {
	rid=$1
	out=$WORK/native/$rid
	rm -rf "$out"
	mkdir -p "$out"
	case $rid in
		osx-*)
			arch=arm64
			[ "$rid" = osx-x64 ] && arch=x86_64
			CXX=clang++ "$ROOT/tools/build-native.sh" macos "$out" -arch $arch -mmacosx-version-min=12.0
			;;
		linux-*)
			platform=linux/amd64 arch=x86_64
			[ "$rid" = linux-arm64 ] && platform=linux/arm64 arch=aarch64
			docker run --rm --platform $platform -v "$ROOT":/src:ro -v "$out":/out "$(linux_image $platform $arch)" sh -euc '
				mkdir -p /work
				tar -C /src -cf - core/include core/src events/include events/src game/include game/src bindings/c tools/build-native.sh | tar -C /work -xf -
				echo "== $(g++ --version | head -n 1), $(uname -m)"
				CXX=g++ /work/tools/build-native.sh linux /work/out
				cp /work/out/libpsiv_session.so /out/
			'
			;;
		win-x64)
			docker run --rm --platform linux/amd64 -v "$ROOT":/src:ro -v "$out":/out psiv-windows-check sh -euc '
				mkdir -p /work
				tar -C /src -cf - core/include core/src events/include events/src game/include game/src bindings/c tools/build-native.sh | tar -C /work -xf -
				cxx=x86_64-w64-mingw32-g++-posix
				echo "== $($cxx --version | head -n 1)"
				CXX=$cxx /work/tools/build-native.sh windows /work/out
				# The export check of tools/windows-check.sh: exactly the header'"'"'s PSIV_API functions.
				cd /work
				x86_64-w64-mingw32-objdump -p out/psiv_session.dll | sed -n "/^\[Ordinal\/Name Pointer\] Table/,/^$/p" | sed "1d" | awk "/^[[:space:]]*\\[/ {print \$NF}" | sort > out/exports.txt
				sed -n "s/^PSIV_API [^(]*[ *]\(psiv_[a-z0-9_]*\)(.*/\1/p" bindings/c/psiv_session.h | sort > out/declared.txt
				diff out/declared.txt out/exports.txt || { echo "psiv_session.dll must export exactly the PSIV_API functions of psiv_session.h"; exit 1; }
				echo "== psiv_session.dll exports exactly the $(wc -l < out/exports.txt) PSIV_API functions of psiv_session.h"
				cp out/psiv_session.dll out/exports.txt /out/
			'
			;;
	esac
}

write_readme() {
	rid=$1
	file=$2
	case $rid in
		win-*) exe=Psiv.Sample.exe run="Psiv.Sample.exe" ;;
		*) exe=Psiv.Sample run="./Psiv.Sample" ;;
	esac
	cat > "$file" <<EOF
$APP_NAME $VERSION ($rid)

The MonoGame sample of the remake's libraries: it plays the invented placeholder adventure through the psiv_session
library. Everything it needs is in this folder; no .NET install is required.

Running
  $run
  $run --version     prints the library version
  $run --help        every command-line flag, the headless script runner included

Files
  $exe and the files beside it
      the program, the .NET runtime, MonoGame and the psiv_session library
  game/data, core/data
      the game data; game/data/game.json finds core's data as ../../core/data, so keep the two folders together
  game/assets
      optional pictures and sounds that replace the generated ones (none ship)
  saves/
      created beside $exe on the first run: save files and controls.json

controls.json in the saves folder holds the key and gamepad bindings; the Options screen's Controls page edits it.

Command-line flags
  --saves DIR          the saves folder (and controls.json) somewhere else
  --data DIR           another data directory (default: game/data found beside the program)
  --assets DIR         another asset directory; --watch-assets reloads it when its files change (F5 does it by hand)
  --seed N             the random seed (default 1)
  --window-size WxH    the window's starting size; --fullscreen starts in borderless fullscreen (F11 toggles it)
  --mute               no sound
  --font FILE          a font to draw text with before the system fonts

Keys: arrows move, Enter, Z or Space confirm, Escape, X or Backspace go back, M opens the field menu, P pauses, Q the
quest log, L the text log, T party talk. A gamepad works too. A mouse click on the field walks there.
EOF
	case $rid in
		osx-*) cat >> "$file" <<EOF

macOS
  Neither this folder nor $(basename "$(app_path "$rid")") is signed or notarized. If macOS refuses to open it, open it
  once with Control-click > Open, or clear the quarantine flag: xattr -dr com.apple.quarantine <folder or .app>.
  The .app keeps its saves and controls.json in ~/Library/Application Support/$APP_NAME (a --saves given to its
  launcher, Contents/MacOS/psiv-sample, wins). Text uses a system font (Arial, Verdana or Geneva).
EOF
			;;
		linux-x64) cat >> "$file" <<EOF

Linux (x86-64)
  SDL2 and OpenAL are included. It needs an X11 session (or XWayland), an OpenGL driver, the ICU library (libicu) that
  .NET uses, and a font: DejaVu, Liberation or Noto (Debian and Ubuntu: fonts-dejavu-core).
EOF
			;;
		linux-arm64) cat >> "$file" <<EOF

Linux (ARM64)
  MonoGame ships SDL2 and OpenAL for x86-64 only, so this build takes them from the system. On Debian or Ubuntu:
    sudo apt install libsdl2-2.0-0 libopenal1 fonts-dejavu-core
  It also needs an X11 session (or XWayland), an OpenGL driver and the ICU library (libicu) that .NET uses.
EOF
			;;
		win-*) cat >> "$file" <<EOF

Windows
  Not signed, so SmartScreen may ask before it starts. This build was cross-compiled and checked on its files only:
  it has not been run on Windows. Text uses a system font (Segoe UI, Arial or Tahoma).
EOF
			;;
	esac
}

app_path() {
	echo "$DIST/psiv-sample-$VERSION-$1.app"
}

publish() {
	rid=$1
	pkg=$2
	lib=$3
	"$DOTNET" publish "$SAMPLE/Psiv.Sample.csproj" -nologo -v q -warnaserror -c Release -r "$rid" --self-contained -p:DebugType=none \
		-p:PsivNativeDir="$WORK/native/$rid/" --artifacts-path "$WORK/dotnet/$rid" -o "$pkg"
	cmp "$WORK/native/$rid/$lib" "$pkg/$lib"
	tar -C "$ROOT" --exclude=.DS_Store -cf - game/data core/data | tar -C "$pkg" -xf -
	if [ -d "$ROOT/game/assets" ]; then
		tar -C "$ROOT" --exclude=.DS_Store -cf - game/assets | tar -C "$pkg" -xf -
	fi
	write_readme "$rid" "$pkg/README.txt"
}

make_app() {
	rid=$1
	pkg=$2
	app=$(app_path "$rid")
	rm -rf "$app"
	mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
	for f in "$pkg"/*; do
		case ${f##*/} in
			game|core|README.txt) cp -R "$f" "$app/Contents/Resources/" ;;
			*) cp -R "$f" "$app/Contents/MacOS/" ;;
		esac
	done
	cat > "$app/Contents/MacOS/psiv-sample" <<EOF
#!/bin/sh
# Starts the sample with its saves in the user's Application Support folder; a --saves given here wins, and the
# process serial number argument older macOS versions add is dropped.
dir=\$(cd "\$(dirname "\$0")" && pwd)
for arg; do
	shift
	case \$arg in
		-psn_*) ;;
		*) set -- "\$@" "\$arg" ;;
	esac
done
exec "\$dir/Psiv.Sample" --saves "\$HOME/Library/Application Support/$APP_NAME" "\$@"
EOF
	chmod +x "$app/Contents/MacOS/psiv-sample"
	cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleDisplayName</key>
	<string>$APP_NAME</string>
	<key>CFBundleExecutable</key>
	<string>psiv-sample</string>
	<key>CFBundleIdentifier</key>
	<string>local.psiv.sample</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>$APP_NAME</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>$VERSION</string>
	<key>CFBundleVersion</key>
	<string>$VERSION</string>
	<key>LSMinimumSystemVersion</key>
	<string>12.0</string>
	<key>NSHighResolutionCapable</key>
	<true/>
</dict>
</plist>
EOF
	plutil -lint "$app/Contents/Info.plist" >/dev/null
}

# What every package must hold, and game.json's core_data reaching core/data from the data directory.
check_layout() {
	rid=$1
	pkg=$2
	lib=$3
	exe=Psiv.Sample
	media=
	case $rid in
		osx-*) media="libSDL2.dylib libopenal.1.dylib" ;;
		linux-x64) media="libSDL2-2.0.so.0 libopenal.so.1" ;;
		win-*) exe=Psiv.Sample.exe media="SDL2.dll soft_oal.dll" ;;
	esac
	for f in "$exe" "$lib" $media README.txt game/data/game.json core/data/rules.json; do
		[ -f "$pkg/$f" ] || { echo "== $rid: the package has no $f"; return 1; }
	done
	python3 -I - "$pkg/game/data" <<'EOF'
import json, os, sys
data = sys.argv[1]
core = json.load(open(os.path.join(data, "game.json"), encoding="utf-8"))["core_data"]
if not os.path.isfile(os.path.join(data, core, "rules.json")):
	sys.exit(f"core_data {core} does not reach core's data from {data}")
EOF
	case $rid in
		osx-arm64) want="Mach-O 64-bit.*arm64" ;;
		osx-x64) want="Mach-O 64-bit.*x86_64" ;;
		linux-x64) want="ELF 64-bit.*x86-64" ;;
		linux-arm64) want="ELF 64-bit.*aarch64" ;;
		win-x64) want="PE32\+.*x86-64" ;;
	esac
	for f in "$exe" "$lib"; do
		file -b "$pkg/$f" | grep -Eq "$want" || { echo "== $rid: $f is $(file -b "$pkg/$f")"; return 1; }
	done
}

# The headless smoke run from a copy outside the repo, so the data it finds can only be the package's.
check_macos() {
	rid=$1
	pkg=$2
	if [ "$rid" = osx-x64 ] && [ "$(uname -m)" = arm64 ] && ! arch -x86_64 /usr/bin/true 2>/dev/null; then
		echo "== $rid: not run: no Rosetta"
		note "$rid: packaged; layout checked; not run (no Rosetta)"
		return 0
	fi
	scratch=$(mktemp -d)
	cp -R "$pkg" "$scratch/pkg"
	cp -R "$(app_path "$rid")" "$scratch/$APP_NAME.app"
	mkdir -p "$WORK/check/$rid"
	(cd / && "$scratch/pkg/Psiv.Sample" --version)
	(cd / && "$scratch/pkg/Psiv.Sample" --saves "$scratch/saves" --script "$SCRIPTS/smoke.txt" --frames 2000) > "$WORK/check/$rid/headless.txt" \
		|| { cat "$WORK/check/$rid/headless.txt"; rm -rf "$scratch"; return 1; }
	tail -n 1 "$WORK/check/$rid/headless.txt"
	launcher="$scratch/$APP_NAME.app/Contents/MacOS/psiv-sample"
	(cd / && "$launcher" --saves "$scratch/app_saves" --script "$SCRIPTS/smoke.txt" --frames 2000) > "$WORK/check/$rid/app_headless.txt" \
		|| { cat "$WORK/check/$rid/app_headless.txt"; rm -rf "$scratch"; return 1; }
	tail -n 1 "$WORK/check/$rid/app_headless.txt"
	rm -rf "$scratch"
	how=natively
	[ "$rid" = osx-x64 ] && [ "$(uname -m)" = arm64 ] && how="under Rosetta"
	note "$rid: $(basename "$pkg") and $(basename "$(app_path "$rid")"): headless smoke run passed $how, from the package and the .app's launcher"
}

check_linux() {
	rid=$1
	pkg=$2
	platform=linux/amd64 arch=x86_64
	[ "$rid" = linux-arm64 ] && platform=linux/arm64 arch=aarch64
	image=$(linux_image $platform $arch)
	shot=package_$rid.png
	mkdir -p "$WORK/shots"
	rm -f "$WORK/shots/$shot"
	docker run --rm --platform $platform -v "$pkg":/pkg:ro -v "$SCRIPTS":/scripts:ro -v "$ROOT/tools":/tools:ro -v "$WORK/shots":/shots \
		-e WINDOW="$WINDOW" -e SHOT="$shot" "$image" sh -euc '
		cd /
		/pkg/Psiv.Sample --version
		/pkg/Psiv.Sample --saves /tmp/saves --script /scripts/smoke.txt --frames 2000 > /tmp/headless.txt 2>&1 \
			|| { status=$?; cat /tmp/headless.txt; echo "== the headless run exited with $status"; exit 1; }
		tail -n 1 /tmp/headless.txt
		[ "$WINDOW" = 1 ] || exit 0
		# Unlike tools/linux-check.sh, no ALSOFT_DRIVERS=null: the OpenAL that MonoGame ships for x86-64 crashes with it.
		# With no sound device the sample logs it and plays silently.
		export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe SDL_AUDIODRIVER=dummy
		timeout 900 xvfb-run -a -s "-screen 0 1280x720x24" /pkg/Psiv.Sample --saves /tmp/window_saves --window --script /scripts/smoke.txt \
			--screenshot /tmp/shot.png > /tmp/window.txt 2>&1 || { status=$?; cat /tmp/window.txt; echo "== the window exited with $status"; exit 1; }
		grep "^audio:" /tmp/window.txt || true
		tail -n 1 /tmp/window.txt
		python3 /tools/check_png.py /tmp/shot.png
		cp /tmp/shot.png "/shots/$SHOT"
	'
	if [ "$WINDOW" = 1 ]; then
		note "$rid: $(basename "$pkg"): headless smoke run and the window under Xvfb passed in Docker ($platform); screenshot dist/.build/shots/$shot"
	else
		note "$rid: $(basename "$pkg"): headless smoke run passed in Docker ($platform); window not run (PSIV_PACKAGE_WINDOW=0)"
	fi
}

for rid in $TARGETS; do
	case $rid in
		osx-arm64|osx-x64|linux-x64|linux-arm64|win-x64) ;;
		*) echo "package: unknown target $rid (use osx-arm64, osx-x64, linux-x64, linux-arm64 or win-x64)" >&2; exit 2 ;;
	esac
done
mkdir -p "$WORK"
echo "== packaging the sample, library version $VERSION: $TARGETS"
for rid in $TARGETS; do
	pkg=$DIST/psiv-sample-$VERSION-$rid
	rm -rf "$pkg"
	case $rid in
		osx-*) lib=libpsiv_session.dylib; rm -rf "$(app_path "$rid")" ;;
		linux-*) lib=libpsiv_session.so ;;
		win-*) lib=psiv_session.dll ;;
	esac
	if why=$(cannot_build "$rid"); then
		skip "$rid" "$why"
		continue
	fi
	echo "== $rid: native library"
	build_native "$rid"
	echo "== $rid: dotnet publish"
	publish "$rid" "$pkg" "$lib"
	case $rid in osx-*) make_app "$rid" "$pkg" ;; esac
	check_layout "$rid" "$pkg" "$lib"
	echo "== $rid: check"
	case $rid in
		osx-*) check_macos "$rid" "$pkg" ;;
		linux-*) check_linux "$rid" "$pkg" ;;
		win-*) note "$rid: $(basename "$pkg"): layout checked and the DLL exports exactly the header's PSIV_API functions; not run (no Windows here)" ;;
	esac
done
echo "== packages (dist/):$SUMMARY"
