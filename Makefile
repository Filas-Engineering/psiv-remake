.PHONY: all test clean

all test clean:
	$(MAKE) -C core $@
	$(MAKE) -C events $@
	$(MAKE) -C game $@
