CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O1 -fPIC -Iinclude -I../core/include -I../events/include -MMD -MP

BUILD = build
CORE_LIB = ../core/build/librules.a
EVENTS_LIB = ../events/build/libevents.a
LIB = $(BUILD)/libgame.a
CLI = $(BUILD)/game_cli
TESTS = $(BUILD)/game_tests

LIB_OBJS = $(patsubst src/%.cpp,$(BUILD)/src/%.o,$(wildcard src/*.cpp))
TEST_OBJS = $(patsubst tests/%.cpp,$(BUILD)/tests/%.o,$(wildcard tests/*.cpp))
CLI_OBJS = $(BUILD)/cli/game_cli.o

.PHONY: all test clean deps

all: $(LIB) $(CLI) $(TESTS)

# The sibling libraries decide for themselves whether they are up to date.
deps:
	$(MAKE) -C ../core build/librules.a
	$(MAKE) -C ../events build/libevents.a

$(CORE_LIB) $(EVENTS_LIB): deps

test: all
	./$(TESTS)
	$(MAKE) -C ../events build/event_cli
	./$(CLI) --registry > $(BUILD)/registry.txt
	./$(CLI) --used > $(BUILD)/used.txt
	../events/build/event_cli check --strict --registry $(BUILD)/registry.txt --glossary data/text/glossary.txt --used $(BUILD)/used.txt \
		--table data/text/en.strings --table data/text/ja.strings $(wildcard data/events/*.ev)
	rm -rf $(BUILD)/cli_saves
	./$(CLI) --seed 10 --auto-battle --script data/scripts/playthrough.txt --saves $(BUILD)/cli_saves > $(BUILD)/playthrough.log
	cmp $(BUILD)/playthrough.log tests/expected/playthrough.txt
	@echo "scripted playthrough matches tests/expected/playthrough.txt"
	./$(CLI) --bot --seed 2 > $(BUILD)/bot.log
	@cat $(BUILD)/bot.log

clean:
	rm -rf $(BUILD)

$(LIB): $(LIB_OBJS)
	rm -f $@
	ar rcs $@ $^

$(CLI): $(CLI_OBJS) $(LIB) $(CORE_LIB) $(EVENTS_LIB)
	$(CXX) $(CXXFLAGS) $(CLI_OBJS) $(LIB) $(CORE_LIB) $(EVENTS_LIB) -o $@

$(TESTS): $(TEST_OBJS) $(LIB) $(CORE_LIB) $(EVENTS_LIB)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) $(LIB) $(CORE_LIB) $(EVENTS_LIB) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(CLI_OBJS:.o=.d)
