.PHONY: all test clean acceptance bindings linux-check

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

acceptance:
	$(MAKE) -C acceptance test

bindings:
	$(MAKE) -C bindings test

# Linux in Docker, with g++ and clang++ (see "Platforms" in README.md).
linux-check:
	tools/linux-check.sh
