#!/bin/sh
# Cross-compiles for 64-bit Windows with MinGW-w64 and runs the results under Wine, in Docker:
# * the three C++ libraries' unit tests;
# * the C API as a DLL (bindings/c), checked to export exactly the header's PSIV_API functions, with the C tests
#   linked against it;
# * the C playthrough and game_cli.exe on the scripted playthrough and the macro script, compared the way
#   bindings/Makefile does: the two transcripts byte for byte, the golden transcript (line ends aside: standard output
#   writes CR LF on Windows), and every save file the two runs wrote.
# It does not cover MSVC, the acceptance suites, the C# wrapper or the MonoGame sample.
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
	cc=x86_64-w64-mingw32-gcc-posix
	echo "== $($cxx --version | head -n 1), $(wine --version)"
	# Wine names files on the Linux side in the encoding of the locale; a game test uses a non-ASCII directory name.
	export WINEDEBUG=-all WINEPREFIX=/tmp/wine LANG=C.UTF-8
	mkdir -p /work
	tar -C /src --exclude=./source-material --exclude=./.git --exclude="*/build" --exclude="*/bin" --exclude="*/obj" -cf - . | tar -C /work -xf -
	flags="-std=c++17 -Wall -Wextra -Werror -O1 -static"
	libs="../core/src/*.cpp ../events/src/*.cpp"
	cd /work/core
	$cxx $flags -Iinclude src/*.cpp tests/*.cpp -o rules_tests.exe
	wine rules_tests.exe
	cd /work/events
	$cxx $flags -Iinclude -Isrc src/*.cpp tests/*.cpp -o event_tests.exe
	wine event_tests.exe
	cd /work/game
	$cxx $flags -Iinclude -I../core/include -I../events/include src/*.cpp $libs tests/*.cpp -o game_tests.exe
	wine game_tests.exe
	$cxx $flags -Iinclude -I../core/include -I../events/include cli/*.cpp src/*.cpp $libs -o game_cli.exe
	echo "== windows: core, events and game unit tests passed"

	cd /work/bindings
	mkdir -p build
	# The DLL carries the C++ runtime inside it (-static), so the tests need nothing but the DLL beside them.
	$cxx $flags -fvisibility=hidden -DPSIV_BUILDING -shared -Ic -I../game/include -I../core/include -I../events/include \
		c/psiv_session.cpp ../game/src/*.cpp $libs -o build/psiv_session.dll -Wl,--out-implib,build/libpsiv_session.dll.a
	x86_64-w64-mingw32-objdump -p build/psiv_session.dll | sed -n "/^\[Ordinal\/Name Pointer\] Table/,/^$/p" | sed "1d" | awk "/^[[:space:]]*\\[/ {print \$NF}" | sort > build/exports.txt
	sed -n "s/^PSIV_API [^(]*[ *]\(psiv_[a-z0-9_]*\)(.*/\1/p" c/psiv_session.h | sort > build/declared.txt
	diff build/declared.txt build/exports.txt || { echo "psiv_session.dll must export exactly the PSIV_API functions of psiv_session.h"; exit 1; }
	exports=$(wc -l < build/exports.txt)
	echo "== psiv_session.dll exports exactly the $exports PSIV_API functions of psiv_session.h"
	cflags="-std=c11 -Wall -Wextra -Werror -pedantic -O1 -Ic"
	$cc $cflags c/tests/test_api.c build/libpsiv_session.dll.a -o build/c_tests.exe
	$cc $cflags c/tests/playthrough.c build/libpsiv_session.dll.a -o build/c_playthrough.exe
	wine build/c_tests.exe --data ../game/data --scratch build/scratch
	script=../game/data/scripts/playthrough.txt
	wine ../game/game_cli.exe --data ../game/data --seed 6 --auto-battle --script $script --saves build/saves_cpp > build/playthrough_cpp.txt
	wine build/c_playthrough.exe --data ../game/data --seed 6 --auto-battle --script $script --saves build/saves_c > build/playthrough_c.txt
	cmp build/playthrough_c.txt build/playthrough_cpp.txt
	# Standard output is in text mode on Windows, so both programs end their lines with CR LF.
	tr -d "\r" < build/playthrough_c.txt | cmp - ../game/tests/expected/playthrough.txt
	diff -r build/saves_c build/saves_cpp
	script=../game/data/scripts/macros.txt
	wine ../game/game_cli.exe --data ../game/data --seed 6 --script $script --saves build/macros_saves_cpp > build/macros_cpp.txt
	wine build/c_playthrough.exe --data ../game/data --seed 6 --script $script --saves build/macros_saves_c > build/macros_c.txt
	cmp build/macros_c.txt build/macros_cpp.txt
	tr -d "\r" < build/macros_c.txt | cmp - ../game/tests/expected/macros.txt
	diff -r build/macros_saves_c build/macros_saves_cpp
	echo "== windows: C API tests passed; the C playthrough and macro script match game_cli.exe and their golden transcripts, saves included"
'
