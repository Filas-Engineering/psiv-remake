CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O1 -Iinclude
AR = ar

BUILD = build
LIB = $(BUILD)/librules.a
CLI = $(BUILD)/battle_cli
TESTS = $(BUILD)/rules_tests

LIB_OBJS = $(patsubst src/%.cpp,$(BUILD)/src/%.o,$(wildcard src/*.cpp))
TEST_OBJS = $(patsubst tests/%.cpp,$(BUILD)/tests/%.o,$(wildcard tests/*.cpp))
CLI_OBJS = $(BUILD)/cli/battle_cli.o

.PHONY: all test clean

all: $(LIB) $(CLI) $(TESTS)

test: $(TESTS) $(CLI)
	./$(TESTS)
	./$(CLI) --seed 1 --auto > $(BUILD)/cli_auto.log
	grep -q "^Outcome: " $(BUILD)/cli_auto.log
	@echo "battle_cli --seed 1 --auto completed: $$(grep '^Outcome: ' $(BUILD)/cli_auto.log)"
	./$(CLI) --seed 7 --auto --encounter warden --difficulty hard --reward 1.5 --record $(BUILD)/warden.record > $(BUILD)/cli_record.log
	./$(CLI) --replay $(BUILD)/warden.record > $(BUILD)/cli_replay.log
	cmp $(BUILD)/cli_record.log $(BUILD)/cli_replay.log
	@echo "battle_cli --replay matches the recorded run"

clean:
	rm -rf $(BUILD)

$(LIB): $(LIB_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(CLI): $(CLI_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(TESTS): $(TEST_OBJS) $(LIB)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(CLI_OBJS:.o=.d)
