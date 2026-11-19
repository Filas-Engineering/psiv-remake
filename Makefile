.PHONY: all test clean acceptance

all clean:
	$(MAKE) -C core $@
	$(MAKE) -C events $@
	$(MAKE) -C game $@

test:
	$(MAKE) -C core $@
	$(MAKE) -C events $@
	$(MAKE) -C game $@
	python3 -I -m unittest discover -s tools/text/tests

acceptance:
	$(MAKE) -C acceptance test
