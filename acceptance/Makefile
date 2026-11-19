CXX = clang++
WARNINGS = -std=c++17 -Wall -Wextra -Werror
CXXFLAGS = $(WARNINGS) -O1 -MMD -MP
PYTHON = python3

ROOT = ..
CORE = $(ROOT)/core
EVENTS = $(ROOT)/events
BUILD = build

CORE_LIB = $(CORE)/build/librules.a
EVENTS_LIB = $(EVENTS)/build/libevents.a

CORE_CHECK = $(BUILD)/core_acceptance
EVENTS_CHECK = $(BUILD)/events_acceptance
DUMPS = $(BUILD)/record_dump_core $(BUILD)/record_dump_O0 $(BUILD)/record_dump_O3 $(BUILD)/save_probe

HARNESS_OBJ = $(BUILD)/harness/acc_main.o
CORE_OBJS = $(patsubst core/%.cpp,$(BUILD)/core/%.o,$(wildcard core/*.cpp))
EVENTS_OBJS = $(patsubst events/%.cpp,$(BUILD)/events/%.o,$(wildcard events/*.cpp))
CORE_SOURCES = $(wildcard $(CORE)/src/*.cpp)

.PHONY: all libs test report clean

all: libs $(CORE_CHECK) $(EVENTS_CHECK) $(DUMPS)

# Each library builds through its own Makefile; the sub-make decides what is out of date.
libs:
	$(MAKE) -C $(CORE) all
	$(MAKE) -C $(EVENTS) all

test: all
	$(PYTHON) scripts/run.py --root $(ROOT) --build $(BUILD)

report:
	@test -f $(BUILD)/results.tsv || { $(MAKE) all && $(PYTHON) scripts/run.py --root $(ROOT) --build $(BUILD) || true; }
	$(PYTHON) scripts/report.py --root $(ROOT) --results $(BUILD)/results.tsv --out report.md

clean:
	rm -rf $(BUILD)

$(CORE_CHECK): $(CORE_OBJS) $(HARNESS_OBJ) $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(EVENTS_CHECK): $(EVENTS_OBJS) $(HARNESS_OBJ) $(EVENTS_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(CORE_LIB) $(EVENTS_LIB): libs

$(BUILD)/core/%.o: core/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iharness -I$(CORE)/include -c $< -o $@

$(BUILD)/events/%.o: events/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iharness -I$(EVENTS)/include -c $< -o $@

$(BUILD)/harness/%.o: harness/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ACC-BAT-15 compares event streams across builds: the core Makefile's library and two copies of the
# core sources compiled here at other optimization levels.
DUMP_OBJS = $(BUILD)/dump/record_dump.o $(BUILD)/core/support.o

$(BUILD)/dump/record_dump.o: dump/record_dump.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iharness -Icore -I$(CORE)/include -c $< -o $@

$(BUILD)/record_dump_core: $(DUMP_OBJS) $(CORE_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/record_dump_O0: $(DUMP_OBJS) $(patsubst $(CORE)/src/%.cpp,$(BUILD)/rules_O0/%.o,$(CORE_SOURCES))
	$(CXX) $(WARNINGS) $^ -o $@

$(BUILD)/record_dump_O3: $(DUMP_OBJS) $(patsubst $(CORE)/src/%.cpp,$(BUILD)/rules_O3/%.o,$(CORE_SOURCES))
	$(CXX) $(WARNINGS) $^ -o $@

# The ACC-SAV tools checks probe the save payloads both libraries offer.
$(BUILD)/dump/save_probe.o: dump/save_probe.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iharness -Icore -I$(CORE)/include -I$(EVENTS)/include -c $< -o $@

$(BUILD)/save_probe: $(BUILD)/dump/save_probe.o $(BUILD)/core/support.o $(CORE_LIB) $(EVENTS_LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/rules_O0/%.o: $(CORE)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(WARNINGS) -O0 -I$(CORE)/include -I$(CORE)/src -c $< -o $@

$(BUILD)/rules_O3/%.o: $(CORE)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(WARNINGS) -O3 -I$(CORE)/include -I$(CORE)/src -c $< -o $@

-include $(CORE_OBJS:.o=.d) $(EVENTS_OBJS:.o=.d) $(HARNESS_OBJ:.o=.d) $(BUILD)/dump/record_dump.d $(BUILD)/dump/save_probe.d
