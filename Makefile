.PHONY: all test clean acceptance

all test clean:
	$(MAKE) -C core $@
	$(MAKE) -C events $@
	$(MAKE) -C game $@

acceptance:
	$(MAKE) -C acceptance test
