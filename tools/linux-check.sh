#!/bin/sh
# Builds and tests the repo on Linux in Docker, once with g++/gcc and once with clang++/clang.
# The repo is mounted read-only and copied inside the container, so no Linux build output lands in it.
# Then the MonoGame sample runs in its window on a virtual display (Xvfb, Mesa llvmpipe software rendering) through
# scripts/smoke.txt; the step fails unless the run exits 0 and its final screenshot is a PNG of more than one color.
# The screenshot is the only file written back: bindings/build/shots/linux_window.png (linux_window_<platform>.png
# with PSIV_LINUX_PLATFORM set).
# Usage: tools/linux-check.sh [gcc|clang ...]   (default: gcc clang)
# PSIV_LINUX_PLATFORM picks the architecture, for example linux/amd64 (default: the host's).
# PSIV_LINUX_WINDOW=0 skips the window step.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE=${PSIV_LINUX_BASE:-mcr.microsoft.com/dotnet/sdk:10.0}
PLATFORM=${PSIV_LINUX_PLATFORM:-}
WINDOW=${PSIV_LINUX_WINDOW:-1}
IMAGE=psiv-linux-check$(echo "$PLATFORM" | tr -c "a-z0-9\n" "-")
COMPILERS=${*:-gcc clang}
SHOTS=$ROOT/bindings/build/shots
SHOT=linux_window$(echo "${PLATFORM:+_$PLATFORM}" | tr -c "a-z0-9_\n" "_").png

# MonoGame's package carries SDL2 and OpenAL for x86-64 only, so other architectures take them from the system; SDL2
# loads the X11 and GL libraries at run time. The sample draws its text with a system font (DejaVu).
docker build -q ${PLATFORM:+--platform "$PLATFORM"} -t "$IMAGE" - >/dev/null <<EOF
FROM $BASE
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends g++ clang make python3 \
	xvfb xauth libgl1 libglx-mesa0 libegl1 libgl1-mesa-dri libx11-6 libxext6 libxcursor1 libxi6 libxfixes3 libxrandr2 libxss1 libsdl2-2.0-0 libopenal1 fonts-dejavu-core \
	&& rm -rf /var/lib/apt/lists/*
EOF

mkdir -p "$SHOTS"
rm -f "$SHOTS/$SHOT"
docker run --rm ${PLATFORM:+--platform "$PLATFORM"} -v "$ROOT":/src:ro -v "$SHOTS":/shots -e COMPILERS="$COMPILERS" -e WINDOW="$WINDOW" -e SHOT="$SHOT" "$IMAGE" sh -euc '
	. /etc/os-release
	echo "== $PRETTY_NAME, $(uname -m), dotnet $(dotnet --version), $(make --version | head -n 1), $(python3 --version)"
	for compiler in $COMPILERS; do
		case $compiler in
			gcc) cxx=g++ cc=gcc ;;
			clang) cxx=clang++ cc=clang ;;
			*) echo "unknown compiler $compiler"; exit 2 ;;
		esac
		echo "== $compiler: $($cxx --version | head -n 1)"
		work=/work/$compiler
		mkdir -p "$work"
		tar -C /src --exclude=./source-material --exclude=./.git --exclude="*/build" --exclude="*/bin" --exclude="*/obj" -cf - . | tar -C "$work" -xf -
		# An empty repository, so the text tooling test of its git-ignore guard runs instead of skipping.
		git init -q "$work"
		make -C "$work" CXX=$cxx CC=$cc DOTNET="$(command -v dotnet)" test acceptance bindings
		echo "== $compiler: make test acceptance bindings passed"
	done
	if [ "$WINDOW" = 0 ]; then
		echo "== window: skipped (PSIV_LINUX_WINDOW=0)"
		exit 0
	fi
	# The sample the last compiler built, in its window on a virtual display with Mesa software rendering.
	cd "$work/bindings"
	shot=build/shots/$SHOT
	export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=null
	timeout 900 xvfb-run -a -s "-screen 0 1280x720x24" dotnet run --no-build --project monogame/Psiv.Sample/Psiv.Sample.csproj -- \
		--data ../game/data --saves build/window_saves --window --script monogame/Psiv.Sample/scripts/smoke.txt --screenshot "$shot" \
		> build/sample_window.txt 2>&1 || { status=$?; cat build/sample_window.txt; echo "== window: the sample exited with $status"; exit 1; }
	tail -n 2 build/sample_window.txt
	python3 /src/tools/check_png.py "$shot"
	cp "$shot" /shots/
	echo "== window: the sample ran smoke.txt in its window under Xvfb and llvmpipe; screenshot bindings/build/shots/$SHOT"
'
