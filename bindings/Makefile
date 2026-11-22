CXX = clang++
CC = clang
DOTNET ?= $(HOME)/.dotnet/dotnet

GAME = ../game
CORE = ../core
EVENTS = ../events
BUILD = build

ifeq ($(OS),Windows_NT)
PLATFORM = windows
else ifeq ($(shell uname -s),Darwin)
PLATFORM = macos
else
PLATFORM = linux
endif

ifeq ($(PLATFORM),macos)
LIB_NAME = libpsiv_session.dylib
SHARED_FLAGS = -dynamiclib -install_name @rpath/$(LIB_NAME) -Wl,-exported_symbols_list,c/exports_macos.txt
RPATH = -Wl,-rpath,@loader_path
LINK_LIB = $(BUILD)/$(LIB_NAME)
EXE =
endif
ifeq ($(PLATFORM),linux)
LIB_NAME = libpsiv_session.so
SHARED_FLAGS = -shared -Wl,-soname,$(LIB_NAME) -Wl,--version-script=c/exports_linux.map
RPATH = -Wl,-rpath,'$$ORIGIN'
LINK_LIB = $(BUILD)/$(LIB_NAME)
EXE =
endif
ifeq ($(PLATFORM),windows)
LIB_NAME = psiv_session.dll
SHARED_FLAGS = -shared -Wl,--out-implib,$(BUILD)/libpsiv_session.dll.a
RPATH =
LINK_LIB = $(BUILD)/libpsiv_session.dll.a
EXE = .exe
endif

CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O1 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -DPSIV_BUILDING \
	-Ic -I$(GAME)/include -I$(CORE)/include -I$(EVENTS)/include -MMD -MP
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O1 -Ic -MMD -MP

LIB = $(BUILD)/$(LIB_NAME)
DEP_LIBS = $(GAME)/build/libgame.a $(EVENTS)/build/libevents.a $(CORE)/build/librules.a
GAME_CLI = $(GAME)/build/game_cli$(EXE)
PLAYTHROUGH = $(BUILD)/c_playthrough$(EXE)
TESTS = $(BUILD)/c_tests$(EXE)

LIB_OBJS = $(BUILD)/c/psiv_session.o
PLAYTHROUGH_OBJS = $(BUILD)/c/tests/playthrough.o
TEST_OBJS = $(BUILD)/c/tests/test_api.o

SCRIPT = $(GAME)/data/scripts/playthrough.txt
TRAVEL_SCRIPT = $(GAME)/data/scripts/travel.txt
MACROS_SCRIPT = $(GAME)/data/scripts/macros.txt
ACTORS_SCRIPT = $(GAME)/data/scripts/actors.txt
PARTY_SCRIPT = $(GAME)/data/scripts/party.txt
INTERACT_SCRIPT = $(GAME)/data/scripts/interact.txt
BACKGROUND_SCRIPT = $(GAME)/data/scripts/background.txt
MAKE_ACTOR_DATA = $(GAME)/build/make_actor_data$(EXE)
# The placeholder data with Wren Hollow added, for the actor script and the tests that play it.
ACTOR_DATA = $(BUILD)/actor_data/game/data
# The same with Wren Cellar and the interaction rules turned on, for the interaction script and its tests.
INTERACT_DATA = $(BUILD)/interact_data/game/data
# The actor data with Wren Yard (background events and wandering actors), for the background script and its tests.
BACKGROUND_DATA = $(BUILD)/background_data/game/data
SAMPLE = monogame/Psiv.Sample/Psiv.Sample.csproj
SAMPLE_SCRIPT = monogame/Psiv.Sample/scripts/smoke.txt
SAMPLE_LARGE_SCRIPT = monogame/Psiv.Sample/scripts/large_areas.txt
SAMPLE_ACTORS_SCRIPT = monogame/Psiv.Sample/scripts/actors.txt
SAMPLE_PARTY_SCRIPT = monogame/Psiv.Sample/scripts/party.txt
SAMPLE_INPUT_SCRIPT = monogame/Psiv.Sample/scripts/input.txt
SAMPLE_MENUS_SCRIPT = monogame/Psiv.Sample/scripts/menus.txt
SAMPLE_MACROS_SCRIPT = monogame/Psiv.Sample/scripts/macros.txt
SAMPLE_SAVES_SCRIPT = monogame/Psiv.Sample/scripts/saves.txt
SAMPLE_OPTIONS_SCRIPT = monogame/Psiv.Sample/scripts/options.txt
SAMPLE_NAME_PAD_SCRIPT = monogame/Psiv.Sample/scripts/name_pad.txt
SAMPLE_PANELS_SCRIPT = monogame/Psiv.Sample/scripts/panels.txt
SAMPLE_AUDIO_SCRIPT = monogame/Psiv.Sample/scripts/audio.txt
SAMPLE_WINDOW_SCRIPT = monogame/Psiv.Sample/scripts/window.txt
SAMPLE_BATTLE_SCRIPT = monogame/Psiv.Sample/scripts/battle.txt
SAMPLE_BATTLE_FRONT_SCRIPT = monogame/Psiv.Sample/scripts/battle_front.txt
SAMPLE_BATTLE_CHANGES_SCRIPT = monogame/Psiv.Sample/scripts/battle_changes.txt
SAMPLE_FELLS_SCRIPT = monogame/Psiv.Sample/scripts/fells.txt
SAMPLE_COGWAY_SCRIPT = monogame/Psiv.Sample/scripts/cogway.txt
SAMPLE_VEHICLES_SCRIPT = monogame/Psiv.Sample/scripts/vehicles.txt
SAMPLE_ASSETS_SCRIPT = monogame/Psiv.Sample/scripts/assets.txt
SAMPLE_RELOAD_SCRIPT = monogame/Psiv.Sample/scripts/hot_reload.txt
SAMPLE_DEV_SCRIPTS = dev_console dev_overlays dev_preview dev_speed
SAMPLE_SCRIPTS = monogame/Psiv.Sample/scripts
SAMPLE_RUN = $(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data

# The round trip: a headless run with the exported placeholders as the asset directory must draw every frame as the
# generated run did (same digests) and print the same transcript. $(1) names the run, $(2) its transcript, $(3) the
# script, $(4) the frames.
define round_trip
	rm -rf $(BUILD)/exported_saves
	$(SAMPLE_RUN) --saves $(BUILD)/exported_saves --script $(3) --frames $(4) --assets $(BUILD)/export --frame-digests $(BUILD)/digests_$(1)_exported.txt \
		> $(BUILD)/$(2)_exported.txt || (cat $(BUILD)/$(2)_exported.txt; exit 1)
	cmp $(BUILD)/digests_$(1).txt $(BUILD)/digests_$(1)_exported.txt
	cmp $(BUILD)/$(2).txt $(BUILD)/$(2)_exported.txt
endef

# A run of the example layout set with --dev, on its own copy of the directory: $(1) the script's name, $(2) the data,
# $(3) the frames.
define layout_run
	rm -rf $(BUILD)/sample_$(1)_saves $(BUILD)/$(1)_assets
	cp -R $(BUILD)/layout_assets $(BUILD)/$(1)_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(2) --dev --saves $(BUILD)/sample_$(1)_saves --assets $(BUILD)/$(1)_assets \
		--script $(SAMPLE_SCRIPTS)/$(1).txt --frames $(3) > $(BUILD)/sample_$(1)_headless.txt || (cat $(BUILD)/sample_$(1)_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_$(1)_headless.txt
endef

# A run of the small example layout set with --dev, on its own copy of the directory: $(1) the script's name, $(2) the
# frames.
define small_run
	rm -rf $(BUILD)/sample_$(1)_saves $(BUILD)/$(1)_assets
	cp -R $(BUILD)/small_assets $(BUILD)/$(1)_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --dev --saves $(BUILD)/sample_$(1)_saves --assets $(BUILD)/$(1)_assets \
		--script $(SAMPLE_SCRIPTS)/$(1).txt --frames $(2) > $(BUILD)/sample_$(1)_headless.txt || (cat $(BUILD)/sample_$(1)_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_$(1)_headless.txt
endef

# A run of the control tags scene with --dev: $(1) the script's name, $(2) more options.
define tag_run
	rm -rf $(BUILD)/sample_$(1)_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/tag_data/game/data --dev $(2) --saves $(BUILD)/sample_$(1)_saves \
		--script $(SAMPLE_SCRIPTS)/$(1).txt --frames 3400 > $(BUILD)/sample_$(1)_headless.txt || (cat $(BUILD)/sample_$(1)_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_$(1)_headless.txt
endef

.PHONY: all test native-test dotnet dotnet-test dotnet-missing clean deps

all: $(LIB) $(PLAYTHROUGH) $(TESTS)
ifneq ($(wildcard $(DOTNET)),)
all: dotnet
endif

# The sibling libraries decide for themselves whether they are up to date.
deps:
	$(MAKE) -C $(CORE) build/librules.a
	$(MAKE) -C $(EVENTS) build/libevents.a
	$(MAKE) -C $(GAME) build/libgame.a build/game_cli$(EXE) build/make_actor_data$(EXE)

$(DEP_LIBS) $(GAME_CLI) $(MAKE_ACTOR_DATA): deps

$(ACTOR_DATA): $(MAKE_ACTOR_DATA)
	./$(MAKE_ACTOR_DATA) $(GAME)/data $(CORE)/data $(GAME)/tests/fixtures/actors $(BUILD)/actor_data

$(INTERACT_DATA): $(MAKE_ACTOR_DATA)
	./$(MAKE_ACTOR_DATA) $(GAME)/data $(CORE)/data $(GAME)/tests/fixtures/actors $(BUILD)/interact_data interact

$(BACKGROUND_DATA): $(MAKE_ACTOR_DATA)
	./$(MAKE_ACTOR_DATA) $(GAME)/data $(CORE)/data $(GAME)/tests/fixtures/actors $(BUILD)/background_data background

.PHONY: $(ACTOR_DATA) $(INTERACT_DATA) $(BACKGROUND_DATA)

$(LIB): $(LIB_OBJS) $(DEP_LIBS)
	$(CXX) $(SHARED_FLAGS) $(LIB_OBJS) $(DEP_LIBS) -o $@

$(PLAYTHROUGH): $(PLAYTHROUGH_OBJS) $(LIB)
	$(CC) $(PLAYTHROUGH_OBJS) $(LINK_LIB) $(RPATH) -o $@

$(TESTS): $(TEST_OBJS) $(LIB)
	$(CC) $(TEST_OBJS) $(LINK_LIB) $(RPATH) -o $@

$(BUILD)/c/%.o: c/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/c/tests/%.o: c/tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: native-test
ifneq ($(wildcard $(DOTNET)),)
test: dotnet-test
else
test: dotnet-missing
endif

native-test: $(LIB) $(PLAYTHROUGH) $(TESTS) $(GAME_CLI) $(ACTOR_DATA) $(INTERACT_DATA) $(BACKGROUND_DATA)
	./$(TESTS) --data $(GAME)/data --actor-data $(ACTOR_DATA) --interact-data $(INTERACT_DATA) --background-data $(BACKGROUND_DATA) --scratch $(BUILD)/scratch
	rm -rf $(BUILD)/saves_cpp $(BUILD)/saves_c
	$(GAME_CLI) --data $(GAME)/data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/saves_cpp > $(BUILD)/playthrough_cpp.txt
	./$(PLAYTHROUGH) --data $(GAME)/data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/saves_c > $(BUILD)/playthrough_c.txt
	cmp $(BUILD)/playthrough_c.txt $(BUILD)/playthrough_cpp.txt
	cmp $(BUILD)/playthrough_c.txt $(GAME)/tests/expected/playthrough.txt
	diff -r $(BUILD)/saves_c $(BUILD)/saves_cpp
	@echo "C playthrough matches the C++ session: transcript and every save file"
	rm -rf $(BUILD)/travel_saves_cpp $(BUILD)/travel_saves_c
	$(GAME_CLI) --data $(GAME)/data --seed 6 --auto-battle --script $(TRAVEL_SCRIPT) --saves $(BUILD)/travel_saves_cpp > $(BUILD)/travel_cpp.txt
	./$(PLAYTHROUGH) --data $(GAME)/data --seed 6 --auto-battle --script $(TRAVEL_SCRIPT) --saves $(BUILD)/travel_saves_c > $(BUILD)/travel_c.txt
	cmp $(BUILD)/travel_c.txt $(BUILD)/travel_cpp.txt
	cmp $(BUILD)/travel_c.txt $(GAME)/tests/expected/travel.txt
	diff -r $(BUILD)/travel_saves_c $(BUILD)/travel_saves_cpp
	@echo "C vehicle and travel script matches the C++ session: transcript and every save file"
	rm -rf $(BUILD)/macros_saves_cpp $(BUILD)/macros_saves_c
	$(GAME_CLI) --data $(GAME)/data --seed 6 --script $(MACROS_SCRIPT) --saves $(BUILD)/macros_saves_cpp > $(BUILD)/macros_cpp.txt
	./$(PLAYTHROUGH) --data $(GAME)/data --seed 6 --script $(MACROS_SCRIPT) --saves $(BUILD)/macros_saves_c > $(BUILD)/macros_c.txt
	cmp $(BUILD)/macros_c.txt $(BUILD)/macros_cpp.txt
	cmp $(BUILD)/macros_c.txt $(GAME)/tests/expected/macros.txt
	diff -r $(BUILD)/macros_saves_c $(BUILD)/macros_saves_cpp
	@echo "C macro script matches the C++ session: transcript and every save file"
	rm -rf $(BUILD)/actors_saves_cpp $(BUILD)/actors_saves_c
	$(GAME_CLI) --data $(ACTOR_DATA) --seed 6 --script $(ACTORS_SCRIPT) --saves $(BUILD)/actors_saves_cpp > $(BUILD)/actors_cpp.txt
	./$(PLAYTHROUGH) --data $(ACTOR_DATA) --seed 6 --script $(ACTORS_SCRIPT) --saves $(BUILD)/actors_saves_c > $(BUILD)/actors_c.txt
	cmp $(BUILD)/actors_c.txt $(BUILD)/actors_cpp.txt
	cmp $(BUILD)/actors_c.txt $(GAME)/tests/expected/actors.txt
	diff -r $(BUILD)/actors_saves_c $(BUILD)/actors_saves_cpp
	@echo "C actor script matches the C++ session: transcript and every save file"
	rm -rf $(BUILD)/party_saves_cpp $(BUILD)/party_saves_c
	$(GAME_CLI) --data $(ACTOR_DATA) --seed 6 --script $(PARTY_SCRIPT) --saves $(BUILD)/party_saves_cpp > $(BUILD)/party_cpp.txt
	./$(PLAYTHROUGH) --data $(ACTOR_DATA) --seed 6 --script $(PARTY_SCRIPT) --saves $(BUILD)/party_saves_c > $(BUILD)/party_c.txt
	cmp $(BUILD)/party_c.txt $(BUILD)/party_cpp.txt
	cmp $(BUILD)/party_c.txt $(GAME)/tests/expected/party.txt
	diff -r $(BUILD)/party_saves_c $(BUILD)/party_saves_cpp
	@echo "C party script matches the C++ session: transcript and every save file"
	rm -rf $(BUILD)/interact_saves_cpp $(BUILD)/interact_saves_c
	$(GAME_CLI) --data $(INTERACT_DATA) --seed 6 --script $(INTERACT_SCRIPT) --saves $(BUILD)/interact_saves_cpp > $(BUILD)/interact_cpp.txt
	./$(PLAYTHROUGH) --data $(INTERACT_DATA) --seed 6 --script $(INTERACT_SCRIPT) --saves $(BUILD)/interact_saves_c > $(BUILD)/interact_c.txt
	cmp $(BUILD)/interact_c.txt $(BUILD)/interact_cpp.txt
	cmp $(BUILD)/interact_c.txt $(GAME)/tests/expected/interact.txt
	diff -r $(BUILD)/interact_saves_c $(BUILD)/interact_saves_cpp
	@echo "C interaction script matches the C++ session: transcript and every save file"
	rm -rf $(BUILD)/background_saves_cpp $(BUILD)/background_saves_c
	$(GAME_CLI) --data $(BACKGROUND_DATA) --seed 6 --script $(BACKGROUND_SCRIPT) --saves $(BUILD)/background_saves_cpp > $(BUILD)/background_cpp.txt
	./$(PLAYTHROUGH) --data $(BACKGROUND_DATA) --seed 6 --script $(BACKGROUND_SCRIPT) --saves $(BUILD)/background_saves_c > $(BUILD)/background_c.txt
	cmp $(BUILD)/background_c.txt $(BUILD)/background_cpp.txt
	cmp $(BUILD)/background_c.txt $(GAME)/tests/expected/background.txt
	diff -r $(BUILD)/background_saves_c $(BUILD)/background_saves_cpp
	@echo "C background script matches the C++ session: transcript and every save file"

dotnet: $(LIB)
	$(DOTNET) build csharp/Psiv.Session.Tests/Psiv.Session.Tests.csproj -nologo -v q -warnaserror
	$(DOTNET) build $(SAMPLE) -nologo -v q -warnaserror

dotnet-test: dotnet $(ACTOR_DATA) $(INTERACT_DATA) $(BACKGROUND_DATA)
	PSIV_DATA_DIR=$(CURDIR)/$(GAME)/data PSIV_ACTOR_DATA_DIR=$(CURDIR)/$(ACTOR_DATA) PSIV_INTERACT_DATA_DIR=$(CURDIR)/$(INTERACT_DATA) PSIV_BACKGROUND_DATA_DIR=$(CURDIR)/$(BACKGROUND_DATA) $(DOTNET) test csharp/Psiv.Session.Tests/Psiv.Session.Tests.csproj --no-build -nologo
	rm -rf $(BUILD)/sample_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_saves --script $(SAMPLE_SCRIPT) --frames 2000 --frame-digests $(BUILD)/digests_smoke.txt > $(BUILD)/sample_headless.txt \
		|| (cat $(BUILD)/sample_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_headless.txt
	rm -rf $(BUILD)/sample_large_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_large_saves --script $(SAMPLE_LARGE_SCRIPT) --frames 1600 > $(BUILD)/sample_large_headless.txt \
		|| (cat $(BUILD)/sample_large_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_large_headless.txt
	rm -rf $(BUILD)/sample_actors_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_actors_saves --script $(SAMPLE_ACTORS_SCRIPT) --frames 1300 --frame-digests $(BUILD)/digests_actors.txt > $(BUILD)/sample_actors_headless.txt \
		|| (cat $(BUILD)/sample_actors_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_actors_headless.txt
	rm -rf $(BUILD)/sample_party_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_party_saves --script $(SAMPLE_PARTY_SCRIPT) --frames 1100 > $(BUILD)/sample_party_headless.txt \
		|| (cat $(BUILD)/sample_party_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_party_headless.txt
	rm -rf $(BUILD)/sample_input_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_input_saves --script $(SAMPLE_INPUT_SCRIPT) --frames 600 > $(BUILD)/sample_input_headless.txt \
		|| (cat $(BUILD)/sample_input_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_input_headless.txt
	rm -rf $(BUILD)/sample_menus_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_menus_saves --script $(SAMPLE_MENUS_SCRIPT) --frames 900 > $(BUILD)/sample_menus_headless.txt \
		|| (cat $(BUILD)/sample_menus_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_menus_headless.txt
	rm -rf $(BUILD)/sample_macros_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_macros_saves --script $(SAMPLE_MACROS_SCRIPT) --frames 760 > $(BUILD)/sample_macros_headless.txt \
		|| (cat $(BUILD)/sample_macros_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_macros_headless.txt
	rm -rf $(BUILD)/sample_saves_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_saves_saves --script $(SAMPLE_SAVES_SCRIPT) --frames 550 > $(BUILD)/sample_saves_headless.txt \
		|| (cat $(BUILD)/sample_saves_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_saves_headless.txt
	rm -rf $(BUILD)/sample_options_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_options_saves --script $(SAMPLE_OPTIONS_SCRIPT) --frames 1200 > $(BUILD)/sample_options_headless.txt \
		|| (cat $(BUILD)/sample_options_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_options_headless.txt
	rm -rf $(BUILD)/sample_name_pad_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_name_pad_saves --script $(SAMPLE_NAME_PAD_SCRIPT) --frames 500 > $(BUILD)/sample_name_pad_headless.txt \
		|| (cat $(BUILD)/sample_name_pad_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_name_pad_headless.txt
	rm -rf $(BUILD)/sample_panels_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_panels_saves --script $(SAMPLE_PANELS_SCRIPT) --frames 1200 --frame-digests $(BUILD)/digests_panels.txt > $(BUILD)/sample_panels_headless.txt \
		|| (cat $(BUILD)/sample_panels_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_panels_headless.txt
	rm -rf $(BUILD)/sample_audio_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_audio_saves --script $(SAMPLE_AUDIO_SCRIPT) --frames 1000 > $(BUILD)/sample_audio_headless.txt \
		|| (cat $(BUILD)/sample_audio_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_audio_headless.txt
	rm -rf $(BUILD)/sample_window_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_window_saves --script $(SAMPLE_WINDOW_SCRIPT) --frames 540 > $(BUILD)/sample_window_headless.txt \
		|| (cat $(BUILD)/sample_window_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_window_headless.txt
	rm -rf $(BUILD)/sample_battle_saves
	$(SAMPLE_RUN) --saves $(BUILD)/sample_battle_saves --script $(SAMPLE_BATTLE_SCRIPT) --frames 5200 --frame-digests $(BUILD)/digests_battle.txt > $(BUILD)/sample_battle_headless.txt \
		|| (cat $(BUILD)/sample_battle_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_battle_headless.txt
	# The front layout's run needs an encounter of six enemies and the changes run a lone Twin Ooze, so they play a copy
	# of the data with those added, and --dev for the commands that reach their moments.
	rm -rf $(BUILD)/sample_battle_front_saves
	python3 -I $(SAMPLE_SCRIPTS)/make_front_data.py $(GAME)/data $(CORE)/data $(BUILD)/front_data
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_battle_front_saves \
		--script $(SAMPLE_BATTLE_FRONT_SCRIPT) --frames 5600 > $(BUILD)/sample_battle_front_headless.txt || (cat $(BUILD)/sample_battle_front_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_battle_front_headless.txt
	rm -rf $(BUILD)/sample_battle_changes_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_battle_changes_saves \
		--script $(SAMPLE_BATTLE_CHANGES_SCRIPT) --frames 9600 > $(BUILD)/sample_battle_changes_headless.txt || (cat $(BUILD)/sample_battle_changes_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_battle_changes_headless.txt
	rm -rf $(BUILD)/sample_fells_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_fells_saves \
		--script $(SAMPLE_FELLS_SCRIPT) --frames 5600 > $(BUILD)/sample_fells_headless.txt || (cat $(BUILD)/sample_fells_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_fells_headless.txt
	rm -rf $(BUILD)/sample_cogway_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --dev --saves $(BUILD)/sample_cogway_saves \
		--script $(SAMPLE_COGWAY_SCRIPT) --frames 2400 > $(BUILD)/sample_cogway_headless.txt || (cat $(BUILD)/sample_cogway_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_cogway_headless.txt
	rm -rf $(BUILD)/sample_magic_defense_saves
	python3 -I $(SAMPLE_SCRIPTS)/make_front_data.py $(GAME)/data $(CORE)/data $(BUILD)/magic_data --magic-defense
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/magic_data/game/data --dev --saves $(BUILD)/sample_magic_defense_saves \
		--script $(SAMPLE_SCRIPTS)/magic_defense.txt --frames 1200 > $(BUILD)/sample_magic_defense_headless.txt || (cat $(BUILD)/sample_magic_defense_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_magic_defense_headless.txt
	rm -rf $(BUILD)/sample_vehicles_saves
	$(SAMPLE_RUN) --saves $(BUILD)/sample_vehicles_saves --script $(SAMPLE_VEHICLES_SCRIPT) --frames 3200 --frame-digests $(BUILD)/digests_vehicles.txt > $(BUILD)/sample_vehicles_headless.txt \
		|| (cat $(BUILD)/sample_vehicles_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_vehicles_headless.txt
	rm -rf $(BUILD)/sample_language_ja_saves
	$(SAMPLE_RUN) --saves $(BUILD)/sample_language_ja_saves --script $(SAMPLE_SCRIPTS)/language_ja.txt --frames 3200 --language ja --strict-text > $(BUILD)/sample_language_ja_headless.txt \
		|| (cat $(BUILD)/sample_language_ja_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_language_ja_headless.txt
	rm -rf $(BUILD)/sample_cleared_saves
	$(GAME_CLI) --data $(GAME)/data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/sample_cleared_saves > /dev/null
	$(SAMPLE_RUN) --saves $(BUILD)/sample_cleared_saves --script $(SAMPLE_SCRIPTS)/cleared.txt --frames 900 --dev > $(BUILD)/sample_cleared_headless.txt \
		|| (cat $(BUILD)/sample_cleared_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_cleared_headless.txt
	rm -rf $(BUILD)/export
	$(SAMPLE_RUN) --export-placeholders $(BUILD)/export
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/export
	$(call round_trip,smoke,sample_headless,$(SAMPLE_SCRIPT),2000)
	$(call round_trip,actors,sample_actors_headless,$(SAMPLE_ACTORS_SCRIPT),1300)
	$(call round_trip,panels,sample_panels_headless,$(SAMPLE_PANELS_SCRIPT),1200)
	$(call round_trip,battle,sample_battle_headless,$(SAMPLE_BATTLE_SCRIPT),5200)
	$(call round_trip,vehicles,sample_vehicles_headless,$(SAMPLE_VEHICLES_SCRIPT),3200)
	@echo "the exported placeholders draw every frame of the smoke, actors, panels, battle and vehicles runs as the generated ones do"
	python3 -I $(SAMPLE_SCRIPTS)/make_test_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/broken_assets $(BUILD)/font_assets $(BUILD)/layout_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/test_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/font_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/layout_assets
	python3 -I $(SAMPLE_SCRIPTS)/make_anim_assets.py --broken $(BUILD)/broken_assets
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/broken_assets > $(BUILD)/check_assets_broken.txt
	diff $(SAMPLE_SCRIPTS)/check_assets_broken.expected $(BUILD)/check_assets_broken.txt
	# The broken copy's layouts (a screen file that does not parse, one with every kind of problem, a missing one, broken
	# styles, and a hostile but valid pause layout) must leave the sample running: the saves run passes on them.
	rm -rf $(BUILD)/sample_broken_layouts_saves
	$(SAMPLE_RUN) --saves $(BUILD)/sample_broken_layouts_saves --assets $(BUILD)/broken_assets --script $(SAMPLE_SAVES_SCRIPT) --frames 550 \
		> $(BUILD)/sample_broken_layouts_headless.txt || (cat $(BUILD)/sample_broken_layouts_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_broken_layouts_headless.txt
	# The example layout set (make_layout_assets.py: 640x480, integer scaling, the bitmap font, other places and list sizes).
	$(call layout_run,layout_field,$(GAME)/data,900)
	$(call layout_run,layout_menus,$(GAME)/data,1800)
	$(call layout_run,layout_battle_front,$(BUILD)/front_data/game/data,3600)
	$(call layout_run,layout_battle_side,$(GAME)/data,1800)
	# The set's window frames as a nine-slice picture and through the UI skin, on saves with a cleared story (the sound test).
	rm -rf $(BUILD)/sample_layout_frames_saves $(BUILD)/layout_frames_assets
	cp -R $(BUILD)/layout_assets $(BUILD)/layout_frames_assets
	$(GAME_CLI) --data $(GAME)/data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/sample_layout_frames_saves > /dev/null
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_layout_frames_saves --assets $(BUILD)/layout_frames_assets --script $(SAMPLE_SCRIPTS)/layout_frames.txt --frames 900 \
		> $(BUILD)/sample_layout_frames_headless.txt || (cat $(BUILD)/sample_layout_frames_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_layout_frames_headless.txt
	# The small example set (make_layout_assets.py --small: 320x224 at 16 pixels a field cell, on a copy of the test assets).
	python3 -I $(SAMPLE_SCRIPTS)/make_layout_assets.py --small $(BUILD)/export $(BUILD)/font_assets $(BUILD)/test_assets $(BUILD)/small_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/small_assets
	# At a 20-pixel cell the test assets' tileset of 48-pixel tiles at scale 3 draws 24 pixels a cell: a problem.
	rm -rf $(BUILD)/small_cell20_assets
	cp -R $(BUILD)/small_assets $(BUILD)/small_cell20_assets
	cp $(BUILD)/small_cell20_assets/alt/styles_cell20.json $(BUILD)/small_cell20_assets/ui/styles.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/small_cell20_assets > $(BUILD)/check_assets_cell20.txt
	grep -q "draws 24 screen pixels a cell, not the field's 20" $(BUILD)/check_assets_cell20.txt
	$(call small_run,small_field,1100)
	$(call small_run,small_menus,700)
	$(call small_run,small_battle,1100)
	# The example battle animation set (make_anim_assets.py: every track type on the placeholder abilities and figures) on
	# the front data copy with --dev, on a copy of its directory since the run swaps in the 59.92 Hz file; then over the
	# 640x480 and 320x224 layout sets.
	python3 -I $(SAMPLE_SCRIPTS)/make_anim_assets.py $(BUILD)/export $(BUILD)/anim_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --check-assets --assets $(BUILD)/anim_assets
	rm -rf $(BUILD)/sample_battle_anim_saves $(BUILD)/battle_anim_assets
	cp -R $(BUILD)/anim_assets $(BUILD)/battle_anim_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_battle_anim_saves --assets $(BUILD)/battle_anim_assets \
		--script $(SAMPLE_SCRIPTS)/battle_anim.txt --frames 6000 > $(BUILD)/sample_battle_anim_headless.txt || (cat $(BUILD)/sample_battle_anim_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_battle_anim_headless.txt
	@for set in layout_assets small_assets; do \
		rm -rf $(BUILD)/sample_anim_$${set}_saves; \
		python3 -I $(SAMPLE_SCRIPTS)/make_anim_assets.py $(BUILD)/export $(BUILD)/anim_$$set $(BUILD)/$$set || exit 1; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_anim_$${set}_saves --assets $(BUILD)/anim_$$set \
			--script $(SAMPLE_SCRIPTS)/battle_anim_layouts.txt --frames 3000 > $(BUILD)/sample_anim_$${set}_headless.txt || { cat $(BUILD)/sample_anim_$${set}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_anim_$${set}_headless.txt; \
	done
	# The animation additions (make_anim_assets.py --more: logical-pixel distances under a 50% motion layout, party_others,
	# a lifted hidden pose, backdrop color light, reduced flash drops and replacements, the flash limit, release cues
	# off, a defeat outlasting its step, item:PIECE, actor qualifiers, a vertical split; the layout's slots out of order
	# with and without the step-aside, and without effectiveness marks), on a copy since the run swaps layouts.
	python3 -I $(SAMPLE_SCRIPTS)/make_anim_assets.py --more $(BUILD)/export $(BUILD)/anim_more_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --check-assets --assets $(BUILD)/anim_more_assets
	rm -rf $(BUILD)/sample_anim_more_saves $(BUILD)/anim_more_run
	cp -R $(BUILD)/anim_more_assets $(BUILD)/anim_more_run
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_anim_more_saves --assets $(BUILD)/anim_more_run \
		--script $(SAMPLE_SCRIPTS)/battle_anim_more.txt --frames 3500 > $(BUILD)/sample_anim_more_headless.txt || (cat $(BUILD)/sample_anim_more_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_anim_more_headless.txt
	grep -q "animation: more.blade: flash dropped, more than 3 in 60 frames" $(BUILD)/sample_anim_more_headless.txt
	# The small set's notices on the menu, the shop and the Load screen take the styles' small text size.
	rm -rf $(BUILD)/sample_small_toasts_saves $(BUILD)/small_toasts_assets
	cp -R $(BUILD)/small_assets $(BUILD)/small_toasts_assets
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_small_toasts_saves --assets $(BUILD)/small_toasts_assets --script $(SAMPLE_SCRIPTS)/small_toasts.txt --frames 600 \
		> $(BUILD)/sample_small_toasts_headless.txt || (cat $(BUILD)/sample_small_toasts_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_small_toasts_headless.txt
	# The dialogue's control tags in English and Japanese, on a data copy with a scene that uses every one.
	python3 -I $(SAMPLE_SCRIPTS)/make_tag_data.py $(GAME)/data $(CORE)/data $(BUILD)/tag_data
	$(call tag_run,dialogue_tags,)
	$(call tag_run,dialogue_tags_ja,--language ja --strict-text)
	# The dialogue's fx, music_pause and music_resume tags with the example animation set, on a copy since the run swaps in
	# a dialogue layout; and a layout whose fx tag names a script that is not a screen effect, or none, as problems.
	rm -rf $(BUILD)/sample_dialogue_fx_saves $(BUILD)/dialogue_fx_assets $(BUILD)/dialogue_fx_broken_assets
	cp -R $(BUILD)/anim_assets $(BUILD)/dialogue_fx_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/tag_data/game/data --dev --saves $(BUILD)/sample_dialogue_fx_saves --assets $(BUILD)/dialogue_fx_assets \
		--script $(SAMPLE_SCRIPTS)/dialogue_fx.txt --frames 900 > $(BUILD)/sample_dialogue_fx_headless.txt || (cat $(BUILD)/sample_dialogue_fx_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_dialogue_fx_headless.txt
	cp -R $(BUILD)/anim_assets $(BUILD)/dialogue_fx_broken_assets
	cp $(BUILD)/dialogue_fx_broken_assets/alt/dialogue_fx_broken.json $(BUILD)/dialogue_fx_broken_assets/ui/dialogue.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/dialogue_fx_broken_assets > $(BUILD)/check_assets_fx.txt
	grep -q "tags.glow: fx script 'turn.default' has phases other than step" $(BUILD)/check_assets_fx.txt
	grep -q "tags.glow: no fx script 'no.such_fx'" $(BUILD)/check_assets_fx.txt
	# The real-time field input (fieldinput realtime): walking, run and the stick with --dev, interaction and its prompt,
	# the prompt in the example layout set, and script actors through the built-in animations on a data copy with an
	# actor named after an encounter.
	rm -rf $(BUILD)/sample_field_walk_saves $(BUILD)/sample_field_interact_saves $(BUILD)/sample_field_prompt_saves $(BUILD)/field_prompt_assets \
		$(BUILD)/sample_field_actors_saves
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_field_walk_saves --script $(SAMPLE_SCRIPTS)/field_walk.txt --frames 1100 > $(BUILD)/sample_field_walk_headless.txt \
		|| (cat $(BUILD)/sample_field_walk_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_field_walk_headless.txt
	$(SAMPLE_RUN) --saves $(BUILD)/sample_field_interact_saves --script $(SAMPLE_SCRIPTS)/field_interact.txt --frames 700 > $(BUILD)/sample_field_interact_headless.txt \
		|| (cat $(BUILD)/sample_field_interact_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_field_interact_headless.txt
	cp -R $(BUILD)/layout_assets $(BUILD)/field_prompt_assets
	$(SAMPLE_RUN) --saves $(BUILD)/sample_field_prompt_saves --assets $(BUILD)/field_prompt_assets --script $(SAMPLE_SCRIPTS)/field_prompt.txt --frames 400 \
		> $(BUILD)/sample_field_prompt_headless.txt || (cat $(BUILD)/sample_field_prompt_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_field_prompt_headless.txt
	python3 -I $(SAMPLE_SCRIPTS)/make_field_data.py $(GAME)/data $(CORE)/data $(BUILD)/field_data
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/field_data/game/data --saves $(BUILD)/sample_field_actors_saves \
		--script $(SAMPLE_SCRIPTS)/field_actors.txt --frames 900 > $(BUILD)/sample_field_actors_headless.txt || (cat $(BUILD)/sample_field_actors_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_field_actors_headless.txt
	# The library's newer field state (scripts/make_rules_data.py's copy of the interaction data and of the test assets):
	# actors drawn from its state, the nearby prompt, running, closed exits, arrival notices, exact step times, music by
	# flag and by vehicle, the macro editor's vehicle and piece steps and battle options, the title's Erase; the keepers' copy
	# (profile rows, per-member rest, keeper voices, an enemy's level); then background events, wanderers, a visible enemy
	# as an actor and the credits on the background data copy.
	rm -rf $(BUILD)/sample_rules_cellar_saves $(BUILD)/sample_rules_macros_saves $(BUILD)/sample_rules_title_saves $(BUILD)/sample_rules_yard_saves \
		$(BUILD)/sample_rules_keepers_saves
	python3 -I $(SAMPLE_SCRIPTS)/make_rules_data.py $(BUILD)/interact_data $(BUILD)/rules_data --assets $(BUILD)/test_assets $(BUILD)/rules_assets \
		--keepers $(BUILD)/keepers_data $(BUILD)/keepers_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/rules_data/game/data --dev --assets $(BUILD)/rules_assets --saves $(BUILD)/sample_rules_cellar_saves \
		--script $(SAMPLE_SCRIPTS)/rules_cellar.txt --frames 1600 > $(BUILD)/sample_rules_cellar_headless.txt || (cat $(BUILD)/sample_rules_cellar_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_rules_cellar_headless.txt
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/rules_data/game/data --dev --saves $(BUILD)/sample_rules_macros_saves \
		--script $(SAMPLE_SCRIPTS)/rules_macros.txt --frames 3000 > $(BUILD)/sample_rules_macros_headless.txt || (cat $(BUILD)/sample_rules_macros_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_rules_macros_headless.txt
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/rules_data/game/data --assets $(BUILD)/rules_assets --saves $(BUILD)/sample_rules_title_saves \
		--script $(SAMPLE_SCRIPTS)/rules_title.txt --frames 900 > $(BUILD)/sample_rules_title_headless.txt || (cat $(BUILD)/sample_rules_title_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_rules_title_headless.txt
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/keepers_data/game/data --dev --assets $(BUILD)/keepers_assets --saves $(BUILD)/sample_rules_keepers_saves \
		--script $(SAMPLE_SCRIPTS)/rules_keepers.txt --frames 2000 > $(BUILD)/sample_rules_keepers_headless.txt || (cat $(BUILD)/sample_rules_keepers_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_rules_keepers_headless.txt
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BACKGROUND_DATA) --dev --saves $(BUILD)/sample_rules_yard_saves \
		--script $(SAMPLE_SCRIPTS)/rules_yard.txt --frames 2400 > $(BUILD)/sample_rules_yard_headless.txt || (cat $(BUILD)/sample_rules_yard_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_rules_yard_headless.txt
	rm -rf $(BUILD)/sample_assets_saves
	$(SAMPLE_RUN) --saves $(BUILD)/sample_assets_saves --assets $(BUILD)/test_assets --script $(SAMPLE_ASSETS_SCRIPT) --frames 2000 > $(BUILD)/sample_assets_headless.txt \
		|| (cat $(BUILD)/sample_assets_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_assets_headless.txt
	rm -rf $(BUILD)/sample_reload_saves $(BUILD)/reload_assets
	cp -R $(BUILD)/test_assets $(BUILD)/reload_assets
	$(SAMPLE_RUN) --saves $(BUILD)/sample_reload_saves --assets $(BUILD)/reload_assets --script $(SAMPLE_RELOAD_SCRIPT) --frames 400 > $(BUILD)/sample_reload_headless.txt \
		|| (cat $(BUILD)/sample_reload_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_reload_headless.txt
	# The newer entries (whole-area pictures, objects, scales, animated tiles, terrain backdrops, vehicle overlays) on a
	# copy of the override directory, since the run swaps a file there, with --dev for the commands that reach them.
	rm -rf $(BUILD)/sample_assets_field_saves $(BUILD)/field_assets
	cp -R $(BUILD)/test_assets $(BUILD)/field_assets
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_assets_field_saves --assets $(BUILD)/field_assets --script $(SAMPLE_SCRIPTS)/assets_field.txt --frames 2600 \
		> $(BUILD)/sample_assets_field_headless.txt || (cat $(BUILD)/sample_assets_field_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_assets_field_headless.txt
	# The Cogway's picture with belts and a tile step hook on a data copy that has one, and formations with free places
	# under scaled and natural-size figures, on a copy of the override directory, since the run swaps its manifest.
	rm -rf $(BUILD)/sample_assets_cogway_saves $(BUILD)/cogway_assets
	python3 -I $(SAMPLE_SCRIPTS)/make_front_data.py $(GAME)/data $(CORE)/data $(BUILD)/tile_data --tile-hook
	cp -R $(BUILD)/test_assets $(BUILD)/cogway_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/tile_data/game/data --dev --saves $(BUILD)/sample_assets_cogway_saves \
		--assets $(BUILD)/cogway_assets --script $(SAMPLE_SCRIPTS)/assets_cogway.txt --frames 2600 > $(BUILD)/sample_assets_cogway_headless.txt \
		|| (cat $(BUILD)/sample_assets_cogway_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_assets_cogway_headless.txt
	# The battle's own window frame, extra poses on sheets and figures, and the party slots' status looks, on a copy of the
	# override directory, since the run swaps files there, with --dev for the battles.
	rm -rf $(BUILD)/sample_assets_battle_saves $(BUILD)/battle_assets
	cp -R $(BUILD)/test_assets $(BUILD)/battle_assets
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_assets_battle_saves --assets $(BUILD)/battle_assets --script $(SAMPLE_SCRIPTS)/assets_battle.txt --frames 5000 \
		> $(BUILD)/sample_assets_battle_headless.txt || (cat $(BUILD)/sample_assets_battle_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_assets_battle_headless.txt
	# The bitmap fonts in English and Japanese, on a copy of the font directory, since the run swaps a metrics file.
	rm -rf $(BUILD)/sample_assets_font_saves $(BUILD)/font_run_assets
	cp -R $(BUILD)/font_assets $(BUILD)/font_run_assets
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_assets_font_saves --assets $(BUILD)/font_run_assets --script $(SAMPLE_SCRIPTS)/assets_font.txt --frames 2000 \
		> $(BUILD)/sample_assets_font_headless.txt || (cat $(BUILD)/sample_assets_font_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_assets_font_headless.txt
	# Backdrops by cell and by flag, the guide's sheet with two facings and its portraits by expression, and the title art (also at its natural size),
	# on a copy of the override directory with alt/assets_more.json as its manifest, since the run swaps a file there; the
	# portraits on the control tags data copy.
	rm -rf $(BUILD)/sample_assets_more_saves $(BUILD)/sample_assets_portraits_saves $(BUILD)/more_assets
	cp -R $(BUILD)/test_assets $(BUILD)/more_assets
	cp $(BUILD)/more_assets/alt/assets_more.json $(BUILD)/more_assets/assets.json
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/more_assets
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_assets_more_saves --assets $(BUILD)/more_assets --script $(SAMPLE_SCRIPTS)/assets_more.txt --frames 1800 \
		> $(BUILD)/sample_assets_more_headless.txt || (cat $(BUILD)/sample_assets_more_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_assets_more_headless.txt
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/tag_data/game/data --dev --saves $(BUILD)/sample_assets_portraits_saves \
		--assets $(BUILD)/more_assets --script $(SAMPLE_SCRIPTS)/assets_portraits.txt --frames 700 > $(BUILD)/sample_assets_portraits_headless.txt \
		|| (cat $(BUILD)/sample_assets_portraits_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_assets_portraits_headless.txt
	# The dialogue's still pictures and a tag's values mapped to cues, on the control tags data copy with a copy of the
	# override directory and alt/assets_more.json as its manifest, since the run swaps in a dialogue layout.
	rm -rf $(BUILD)/sample_dialogue_pictures_saves $(BUILD)/pictures_assets
	cp -R $(BUILD)/test_assets $(BUILD)/pictures_assets
	cp $(BUILD)/pictures_assets/alt/assets_more.json $(BUILD)/pictures_assets/assets.json
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/tag_data/game/data --dev --saves $(BUILD)/sample_dialogue_pictures_saves \
		--assets $(BUILD)/pictures_assets --script $(SAMPLE_SCRIPTS)/dialogue_pictures.txt --frames 1200 > $(BUILD)/sample_dialogue_pictures_headless.txt \
		|| (cat $(BUILD)/sample_dialogue_pictures_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_dialogue_pictures_headless.txt
	# The layout items (make_items_assets.py): battle fields, command axis and top-level step, result pages, the title's
	# start state and entries, row models, tag cases, the picture stack, a line over a fade, no arrival notice or banner;
	# the cue and track only the tags play pass the check, and without the tags table they are problems.
	python3 -I $(SAMPLE_SCRIPTS)/make_items_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/items_assets
	python3 -I $(SAMPLE_SCRIPTS)/make_items_data.py $(GAME)/data $(CORE)/data $(BUILD)/items_data
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/items_data/game/data --check-assets --assets $(BUILD)/items_assets
	rm -rf $(BUILD)/items_untagged_assets
	cp -R $(BUILD)/items_assets $(BUILD)/items_untagged_assets
	rm $(BUILD)/items_untagged_assets/ui/dialogue.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/items_untagged_assets > $(BUILD)/check_assets_untagged.txt
	grep -q "unknown cue 'tag_gong'" $(BUILD)/check_assets_untagged.txt
	grep -q "unknown music 'tag_theme'" $(BUILD)/check_assets_untagged.txt
	@for run in items_battle:$(BUILD)/front_data/game/data:8000 items_menus:$(GAME)/data:3000 items_dialogue:$(BUILD)/items_data/game/data:2500; do \
		name=$${run%%:*}; rest=$${run#*:}; data=$${rest%%:*}; frames=$${rest##*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves $(BUILD)/$${name}_assets; \
		cp -R $(BUILD)/items_assets $(BUILD)/$${name}_assets; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $$data --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/$${name}_assets \
			--script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	$(call small_run,small_dev,1200)
	# Flashes by their effect (make_flash_assets.py): a stepped stage strobe past the limit is dropped, a two-step whiteout
	# is held to a fifth under reduced flash, a slow tint is untouched.
	python3 -I $(SAMPLE_SCRIPTS)/make_flash_assets.py $(BUILD)/export $(BUILD)/flash_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/flash_assets
	rm -rf $(BUILD)/sample_flash_safety_saves
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_flash_safety_saves --assets $(BUILD)/flash_assets --script $(SAMPLE_SCRIPTS)/flash_safety.txt --frames 7000 \
		> $(BUILD)/sample_flash_safety_headless.txt || (cat $(BUILD)/sample_flash_safety_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_flash_safety_headless.txt
	grep -q "animation: flash.strobe: flash dropped, more than 3 in 60 frames" $(BUILD)/sample_flash_safety_headless.txt
	# The flashing level of a new profile in normal play (--play-defaults): reduced with the title's notice on the shipped
	# data; with a data copy whose default is original, the photosensitivity notice first, Standard chosen, or kept.
	python3 -I $(SAMPLE_SCRIPTS)/make_flash_data.py $(GAME)/data $(CORE)/data $(BUILD)/flash_bad_data bright
	! $(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/flash_bad_data/game/data --check-assets --assets $(BUILD)/export > $(BUILD)/check_flash_data.txt 2>&1
	grep -q "flash_default 'bright' must be reduced, standard or original" $(BUILD)/check_flash_data.txt
	python3 -I $(SAMPLE_SCRIPTS)/make_flash_data.py $(GAME)/data $(CORE)/data $(BUILD)/flash_data original
	@for run in flashing_new:$(GAME)/data flashing_notice:$(BUILD)/flash_data/game/data flashing_notice_keep:$(BUILD)/flash_data/game/data; do \
		name=$${run%%:*}; data=$${run#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $$data --play-defaults --saves $(BUILD)/sample_$${name}_saves --script $(SAMPLE_SCRIPTS)/$$name.txt \
			--frames 300 > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	@for run in $(SAMPLE_DEV_SCRIPTS); do \
		rm -rf $(BUILD)/sample_$${run}_saves; \
		$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_$${run}_saves --script $(SAMPLE_SCRIPTS)/$$run.txt --frames 1500 > $(BUILD)/sample_$${run}_headless.txt \
			|| { cat $(BUILD)/sample_$${run}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${run}_headless.txt; \
	done
	# The data reload changes files, so it runs on a copy, with core's data beside it where game.json's core_data looks.
	rm -rf $(BUILD)/dev_reload $(BUILD)/sample_dev_reload_saves
	mkdir -p $(BUILD)/dev_reload/game $(BUILD)/dev_reload/core
	cp -R $(GAME)/data $(BUILD)/dev_reload/game/data
	cp -R $(CORE)/data $(BUILD)/dev_reload/core/data
	sed 's/"area.tidewater": "Tidewater Landing"/"area.tidewater": "Tidewater Quay"/' $(GAME)/data/text/en.json > $(BUILD)/dev_reload/game/data/text/en_renamed.json
	printf '{ "text": {\n' > $(BUILD)/dev_reload/game/data/text/en_broken.json
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/dev_reload/game/data --dev --saves $(BUILD)/sample_dev_reload_saves \
		--script $(SAMPLE_SCRIPTS)/dev_reload.txt --frames 700 > $(BUILD)/sample_dev_reload_headless.txt || (cat $(BUILD)/sample_dev_reload_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_dev_reload_headless.txt
	rm -rf $(BUILD)/sample_dev_off_saves
	$(SAMPLE_RUN) --saves $(BUILD)/sample_dev_off_saves --script $(SAMPLE_SCRIPTS)/dev_off.txt --frames 400 > $(BUILD)/sample_dev_off_headless.txt \
		|| (cat $(BUILD)/sample_dev_off_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_dev_off_headless.txt
	rm -rf $(BUILD)/audio
	$(DOTNET) run --no-build --project $(SAMPLE) -- --dump-audio $(BUILD)/audio
	python3 -I monogame/Psiv.Sample/scripts/check_wav.py $(BUILD)/audio

dotnet-missing:
	@echo "no dotnet at $(DOTNET): skipped the C# tests and the sample's headless run"

clean:
	rm -rf $(BUILD) csharp/*/bin csharp/*/obj monogame/*/bin monogame/*/obj

# Self-contained builds of the sample per platform under ../dist, built apart from build/ and the projects' bin/ and obj/.
.PHONY: package
package:
	DOTNET="$(DOTNET)" ../tools/package.sh

-include $(LIB_OBJS:.o=.d) $(PLAYTHROUGH_OBJS:.o=.d) $(TEST_OBJS:.o=.d)
