.PHONY: all test clean

all test clean:
	$(MAKE) -C core $@
	$(MAKE) -C events $@
