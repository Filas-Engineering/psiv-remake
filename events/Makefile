CXX := clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -Iinclude -Isrc -MMD -MP

BUILD := build
LIB_SOURCES := $(wildcard src/*.cpp)
TEST_SOURCES := $(wildcard tests/*.cpp)
LIB_OBJECTS := $(LIB_SOURCES:%.cpp=$(BUILD)/%.o)
TEST_OBJECTS := $(TEST_SOURCES:%.cpp=$(BUILD)/%.o)
CLI_OBJECTS := $(BUILD)/tools/event_cli.o

LIB := $(BUILD)/libevents.a
CLI := $(BUILD)/event_cli
TESTS := $(BUILD)/event_tests

.PHONY: all test check-samples clean

all: $(CLI) $(TESTS)

test: $(TESTS) $(CLI)
	./$(TESTS)
	$(MAKE) check-samples

# Every tool on the samples. Tables, glossary and timing data live in
# sample_text/ so that samples/* holds scripts only.
check-samples: $(CLI)
	./$(CLI) check samples/*.ev
	./$(CLI) lint --entries sample_text/entries.txt samples/*.ev
	./$(CLI) strings check --table sample_text/en.strings --table sample_text/ja.strings samples/*.ev
	./$(CLI) strings extract samples/*.ev > /dev/null
	./$(CLI) names sample_text/glossary.txt --table sample_text/en.strings --table sample_text/ja.strings samples/*.ev
	./$(CLI) time --config sample_text/timing.cfg --table sample_text/en.strings samples/harbor.ev harbor_arrival > /dev/null
	./$(CLI) run samples/market.ev samples/harbor.ev market_haggle > /dev/null

$(LIB): $(LIB_OBJECTS)
	ar rcs $@ $^

$(CLI): $(CLI_OBJECTS) $(LIB)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TESTS): $(TEST_OBJECTS) $(LIB)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(LIB_OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d) $(CLI_OBJECTS:.o=.d)
