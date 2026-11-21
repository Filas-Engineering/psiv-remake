#!/bin/sh
# Builds the psiv_session shared library (core, events, game and the C API) from the sources, without the Makefiles,
# into OUT, with its objects in OUT/obj. Nothing is written outside OUT, so tools/package.sh can build one library
# per target next to the development builds without touching them.
# Usage: tools/build-native.sh macos|linux|windows OUT [flags for both the compiler and the linker...]
# CXX picks the compiler (default c++), JOBS the parallel compiles.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
KIND=$1
OUT=$2
shift 2
CXX=${CXX:-c++}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
EXTRA="$*"
FLAGS="-std=c++17 -Wall -Wextra -Werror -O2 -fvisibility=hidden -fvisibility-inlines-hidden -DPSIV_BUILDING"
case $KIND in
	macos|linux) FLAGS="$FLAGS -fPIC" ;;
	windows) ;;
	*) echo "build-native.sh: unknown kind $KIND" >&2; exit 2 ;;
esac

mkdir -p "$OUT/obj"
export ROOT OUT CXX FLAGS EXTRA
# Each directory's include paths as its own Makefile has them; objects are named after their directory so that
# sources with the same name in two libraries do not collide.
(cd "$ROOT" && ls core/src/*.cpp events/src/*.cpp game/src/*.cpp bindings/c/psiv_session.cpp) | xargs -P "$JOBS" -n 1 sh -euc '
	case $0 in
		core/*) inc="-Icore/include" ;;
		events/*) inc="-Ievents/include -Ievents/src" ;;
		game/*) inc="-Igame/include -Icore/include -Ievents/include" ;;
		bindings/*) inc="-Ibindings/c -Igame/include -Icore/include -Ievents/include" ;;
	esac
	obj=$OUT/obj/$(echo "$0" | tr / _ | sed "s/\.cpp$/.o/")
	cd "$ROOT"
	$CXX $FLAGS $EXTRA $inc -c "$0" -o "$obj"
'

case $KIND in
	macos)
		$CXX $EXTRA -dynamiclib -install_name @rpath/libpsiv_session.dylib -Wl,-exported_symbols_list,"$ROOT/bindings/c/exports_macos.txt" \
			"$OUT"/obj/*.o -o "$OUT/libpsiv_session.dylib"
		;;
	linux)
		# The C++ runtime goes inside the library, so it needs nothing newer than the system's C library.
		$CXX $EXTRA -shared -Wl,-soname,libpsiv_session.so -Wl,--version-script="$ROOT/bindings/c/exports_linux.map" -static-libstdc++ -static-libgcc \
			"$OUT"/obj/*.o -o "$OUT/libpsiv_session.so"
		;;
	windows)
		# As tools/windows-check.sh builds it: the C++ runtime inside the DLL (-static), so it needs nothing beside it.
		$CXX $EXTRA -shared -static "$OUT"/obj/*.o -o "$OUT/psiv_session.dll"
		;;
esac
