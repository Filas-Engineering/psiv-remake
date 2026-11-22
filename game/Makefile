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
MAKE_ACTOR_DATA = $(BUILD)/make_actor_data
# The placeholder data with Wren Hollow and Wren Loft added, for data/scripts/actors.txt and data/scripts/party.txt
# (tests/actor_data.h).
ACTOR_DATA = $(BUILD)/actor_data/game/data
# The same with Wren Cellar and game.json's interaction rules turned on, for data/scripts/interact.txt.
INTERACT_DATA = $(BUILD)/interact_data/game/data
# The actor data with Wren Yard (background events and wandering actors), for data/scripts/background.txt.
BACKGROUND_DATA = $(BUILD)/background_data/game/data

.PHONY: all test clean deps maps maps-check expected

PYTHON = python3 -I
MAP_SOURCES = $(wildcard data/maps/*.tmj)

all: $(LIB) $(CLI) $(TESTS) $(MAKE_ACTOR_DATA)

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

# Re-records the golden transcripts of the scripted playthrough, the vehicle and travel script, the Ashen Fells
# script, the macro script, the actor script, the party script, the interaction script and the background script after a
# deliberate change.
expected: all
	rm -rf $(BUILD)/cli_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/playthrough.txt --saves $(BUILD)/cli_saves > tests/expected/playthrough.txt
	rm -rf $(BUILD)/travel_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/travel.txt --saves $(BUILD)/travel_saves > tests/expected/travel.txt
	rm -rf $(BUILD)/world_saves
	./$(CLI) --seed 6 --auto-battle --script data/scripts/world.txt --saves $(BUILD)/world_saves > tests/expected/world.txt
	rm -rf $(BUILD)/macros_saves
	./$(CLI) --seed 6 --script data/scripts/macros.txt --saves $(BUILD)/macros_saves > tests/expected/macros.txt
	./$(MAKE_ACTOR_DATA) data ../core/data tests/fixtures/actors $(BUILD)/actor_data
	rm -rf $(BUILD)/actors_saves
	./$(CLI) --data $(ACTOR_DATA) --seed 6 --script data/scripts/actors.txt --saves $(BUILD)/actors_saves > tests/expected/actors.txt
	rm -rf $(BUILD)/party_saves
	./$(CLI) --data $(ACTOR_DATA) --seed 6 --script data/scripts/party.txt --saves $(BUILD)/party_saves > tests/expected/party.txt
	./$(MAKE_ACTOR_DATA) data ../core/data tests/fixtures/actors $(BUILD)/interact_data interact
	rm -rf $(BUILD)/interact_saves
	./$(CLI) --data $(INTERACT_DATA) --seed 6 --script data/scripts/interact.txt --saves $(BUILD)/interact_saves > tests/expected/interact.txt
	./$(MAKE_ACTOR_DATA) data ../core/data tests/fixtures/actors $(BUILD)/background_data background
	rm -rf $(BUILD)/background_saves
	./$(CLI) --data $(BACKGROUND_DATA) --seed 6 --script data/scripts/background.txt --saves $(BUILD)/background_saves > tests/expected/background.txt

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
	rm -rf $(BUILD)/macros_saves
	./$(CLI) --seed 6 --script data/scripts/macros.txt --saves $(BUILD)/macros_saves > $(BUILD)/macros.log
	cmp $(BUILD)/macros.log tests/expected/macros.txt
	@echo "macro script matches tests/expected/macros.txt"
	./$(MAKE_ACTOR_DATA) data ../core/data tests/fixtures/actors $(BUILD)/actor_data
	./$(CLI) --check $(ACTOR_DATA) > $(BUILD)/check_actors.log || (cat $(BUILD)/check_actors.log; exit 1)
	@tail -n 1 $(BUILD)/check_actors.log
	rm -rf $(BUILD)/actors_saves
	./$(CLI) --data $(ACTOR_DATA) --seed 6 --script data/scripts/actors.txt --saves $(BUILD)/actors_saves > $(BUILD)/actors.log
	cmp $(BUILD)/actors.log tests/expected/actors.txt
	@echo "actor script matches tests/expected/actors.txt"
	rm -rf $(BUILD)/party_saves
	./$(CLI) --data $(ACTOR_DATA) --seed 6 --script data/scripts/party.txt --saves $(BUILD)/party_saves > $(BUILD)/party.log
	cmp $(BUILD)/party.log tests/expected/party.txt
	@echo "party script matches tests/expected/party.txt"
	./$(MAKE_ACTOR_DATA) data ../core/data tests/fixtures/actors $(BUILD)/interact_data interact
	./$(CLI) --check $(INTERACT_DATA) > $(BUILD)/check_interact.log || (cat $(BUILD)/check_interact.log; exit 1)
	@tail -n 1 $(BUILD)/check_interact.log
	rm -rf $(BUILD)/interact_saves
	./$(CLI) --data $(INTERACT_DATA) --seed 6 --script data/scripts/interact.txt --saves $(BUILD)/interact_saves > $(BUILD)/interact.log
	cmp $(BUILD)/interact.log tests/expected/interact.txt
	@echo "interaction script matches tests/expected/interact.txt"
	./$(MAKE_ACTOR_DATA) data ../core/data tests/fixtures/actors $(BUILD)/background_data background
	./$(CLI) --check $(BACKGROUND_DATA) > $(BUILD)/check_background.log || (cat $(BUILD)/check_background.log; exit 1)
	@tail -n 1 $(BUILD)/check_background.log
	rm -rf $(BUILD)/background_saves
	./$(CLI) --data $(BACKGROUND_DATA) --seed 6 --script data/scripts/background.txt --saves $(BUILD)/background_saves > $(BUILD)/background.log
	cmp $(BUILD)/background.log tests/expected/background.txt
	@echo "background script matches tests/expected/background.txt"
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

$(MAKE_ACTOR_DATA): $(BUILD)/tools/make_actor_data.o $(LIB) $(CORE_LIB) $(EVENTS_LIB)
	$(CXX) $(CXXFLAGS) $(BUILD)/tools/make_actor_data.o $(LIB) $(CORE_LIB) $(EVENTS_LIB) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(LIB_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(CLI_OBJS:.o=.d) $(BUILD)/tools/make_actor_data.d
