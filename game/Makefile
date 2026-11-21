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

.PHONY: all test clean deps maps maps-check expected

PYTHON = python3 -I
MAP_SOURCES = $(wildcard data/maps/*.tmj)

all: $(LIB) $(CLI) $(TESTS)

# The sibling libraries decide for themselves whether they are up to date.
deps:
	$(MAKE) -C ../core build/librules.a
	$(MAKE) -C ../events build/libevents.a

$(CORE_LIB) $(EVENTS_LIB): deps

# Areas drawn in Tiled: data/maps/NAME.tmj is the source of data/areas/NAME.json.
maps:
	@for m in $(MAP_SOURCES); do \
		echo "$$m -> data/areas/$$(basename $$m .tmj).json"; \
		$(PYTHON) ../tools/maps/tiled_to_area.py $$m -o data/areas/$$(basename $$m .tmj).json || exit 1; \
	done

maps-check:
	@for m in $(MAP_SOURCES); do \
		$(PYTHON) ../tools/maps/tiled_to_area.py $$m --check data/areas/$$(basename $$m .tmj).json || exit 1; \
	done
	@echo "data/areas matches data/maps ($(words $(MAP_SOURCES)) maps)"

# Re-records the golden transcripts of the scripted playthrough, the vehicle and travel script and the Ashen Fells
# script after a deliberate change.
expected: all
	rm -rf $(BUILD)/cli_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/playthrough.txt --saves $(BUILD)/cli_saves > tests/expected/playthrough.txt
	rm -rf $(BUILD)/travel_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/travel.txt --saves $(BUILD)/travel_saves > tests/expected/travel.txt
	rm -rf $(BUILD)/world_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/world.txt --saves $(BUILD)/world_saves > tests/expected/world.txt

test: all maps-check
	./$(CLI) --check data > $(BUILD)/check.log || (cat $(BUILD)/check.log; exit 1)
	@tail -n 1 $(BUILD)/check.log
	./$(TESTS)
	$(MAKE) -C ../events build/event_cli
	./$(CLI) --registry > $(BUILD)/registry.txt
	./$(CLI) --used > $(BUILD)/used.txt
	../events/build/event_cli check --strict --registry $(BUILD)/registry.txt --glossary data/text/glossary.txt --used $(BUILD)/used.txt \
		--table data/text/en.strings --table data/text/ja.strings $(wildcard data/events/*.ev)
	rm -rf $(BUILD)/cli_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/playthrough.txt --saves $(BUILD)/cli_saves > $(BUILD)/playthrough.log
	cmp $(BUILD)/playthrough.log tests/expected/playthrough.txt
	@echo "scripted playthrough matches tests/expected/playthrough.txt"
	rm -rf $(BUILD)/travel_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/travel.txt --saves $(BUILD)/travel_saves > $(BUILD)/travel.log
	cmp $(BUILD)/travel.log tests/expected/travel.txt
	@echo "vehicle and travel script matches tests/expected/travel.txt"
	rm -rf $(BUILD)/world_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/world.txt --saves $(BUILD)/world_saves > $(BUILD)/world.log
	cmp $(BUILD)/world.log tests/expected/world.txt
	@echo "Ashen Fells script matches tests/expected/world.txt"
	./$(CLI) --bot --seed 2 > $(BUILD)/bot.log
	@cat $(BUILD)/bot.log
	./$(CLI) --bot --seed 2 --route data/routes/kiln.route --goal saltgrass.kiln_done > $(BUILD)/bot_kiln.log
	@cat $(BUILD)/bot_kiln.log

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
