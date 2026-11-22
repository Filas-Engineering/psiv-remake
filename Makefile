.PHONY: all test clean acceptance bindings linux-check perf

all clean:
	$(MAKE) -C core $@
	$(MAKE) -C events $@
	$(MAKE) -C game $@

test:
	$(MAKE) -C core $@
	$(MAKE) -C events $@
	$(MAKE) -C game $@
	python3 -I -m unittest discover -s tools/text/tests
	python3 -I -m unittest discover -s tools/maps/tests

# Times the full-size stress world in game_cli (game/Makefile); not part of `make test`.
perf:
	$(MAKE) -C game perf

acceptance:
	$(MAKE) -C acceptance test

bindings:
	$(MAKE) -C bindings test

# Linux in Docker, with g++ and clang++ (see "Platforms" in README.md).
linux-check:
	tools/linux-check.sh

# Self-contained builds of the MonoGame sample per platform under dist/ (see "Packaging" in bindings/README.md).
.PHONY: package
package:
	tools/package.sh
