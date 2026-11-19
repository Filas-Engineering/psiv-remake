#!/bin/sh
# Builds and tests the repo on Linux in Docker, once with g++/gcc and once with clang++/clang.
# The repo is mounted read-only and copied inside the container, so no Linux build output lands in it.
# Usage: tools/linux-check.sh [gcc|clang ...]   (default: gcc clang)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE=${PSIV_LINUX_BASE:-mcr.microsoft.com/dotnet/sdk:10.0}
IMAGE=psiv-linux-check
COMPILERS=${*:-gcc clang}

docker build -q -t "$IMAGE" - >/dev/null <<EOF
FROM $BASE
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends g++ clang make python3 \
	&& rm -rf /var/lib/apt/lists/*
EOF

docker run --rm -v "$ROOT":/src:ro -e COMPILERS="$COMPILERS" "$IMAGE" sh -euc '
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
'
