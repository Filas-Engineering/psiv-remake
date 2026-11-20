#!/bin/sh
# Cross-compiles the three C++ libraries and their unit tests for 64-bit Windows with MinGW-w64
# and runs the tests under Wine, in Docker. It does not cover MSVC, the CLIs' Makefile checks,
# the acceptance suites or the bindings.
# The repo is mounted read-only and copied inside the container.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=psiv-windows-check

docker build -q --platform linux/amd64 -t "$IMAGE" - >/dev/null <<EOF
FROM debian:trixie-slim
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends g++-mingw-w64-x86-64-posix wine wine64 \
	&& rm -rf /var/lib/apt/lists/*
EOF

docker run --rm --platform linux/amd64 -v "$ROOT":/src:ro "$IMAGE" sh -euc '
	cxx=x86_64-w64-mingw32-g++-posix
	echo "== $($cxx --version | head -n 1), $(wine --version)"
	export WINEDEBUG=-all WINEPREFIX=/tmp/wine
	mkdir -p /work
	tar -C /src --exclude=./source-material --exclude=./.git --exclude="*/build" --exclude="*/bin" --exclude="*/obj" -cf - . | tar -C /work -xf -
	flags="-std=c++17 -Wall -Wextra -Werror -O1 -static"
	cd /work/core
	$cxx $flags -Iinclude src/*.cpp tests/*.cpp -o rules_tests.exe
	wine rules_tests.exe
	cd /work/events
	$cxx $flags -Iinclude -Isrc src/*.cpp tests/*.cpp -o event_tests.exe
	wine event_tests.exe
	cd /work/game
	$cxx $flags -Iinclude -I../core/include -I../events/include src/*.cpp ../core/src/*.cpp ../events/src/*.cpp tests/*.cpp -o game_tests.exe
	wine game_tests.exe
	echo "== windows (MinGW-w64, Wine): core, events and game unit tests passed"
'
