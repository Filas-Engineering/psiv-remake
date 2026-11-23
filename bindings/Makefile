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
TROUBLE_SCRIPT = $(GAME)/data/scripts/trouble.txt
INTERACT_SCRIPT = $(GAME)/data/scripts/interact.txt
BACKGROUND_SCRIPT = $(GAME)/data/scripts/background.txt
APPROACH_SCRIPT = $(GAME)/data/scripts/approach.txt
SCENE_SCRIPT = $(GAME)/data/scripts/scene.txt
MAKE_ACTOR_DATA = $(GAME)/build/make_actor_data$(EXE)
# The placeholder data with Wren Hollow added, for the actor script and the tests that play it.
ACTOR_DATA = $(BUILD)/actor_data/game/data
# The same with Wren Cellar and the interaction rules turned on, for the interaction script and its tests.
INTERACT_DATA = $(BUILD)/interact_data/game/data
# The actor data with Wren Yard (background events and wandering actors), for the background script and its tests.
BACKGROUND_DATA = $(BUILD)/background_data/game/data
# The actor data with Wren Gallery (story pictures, the scene camera, music fades, an exit's sound), for the scene script.
SCENE_DATA = $(BUILD)/scene_data/game/data
# The actor data with Wren Mill (wandering, patrols, pixel paths, moving platforms), for the motion script.
MOTION_DATA = $(BUILD)/motion_data/game/data
MOTION_SCRIPT = $(GAME)/data/scripts/motion.txt
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

# A run of the narration scene with --dev on its own copy of the narration assets: $(1) the script's name, $(2) more
# options.
define narration_run
	rm -rf $(BUILD)/sample_$(1)_saves $(BUILD)/$(1)_run_assets
	cp -R $(BUILD)/narration_assets $(BUILD)/$(1)_run_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/narration_data/game/data --dev $(2) --saves $(BUILD)/sample_$(1)_saves \
		--assets $(BUILD)/$(1)_run_assets --script $(SAMPLE_SCRIPTS)/$(1).txt --frames 900 > $(BUILD)/sample_$(1)_headless.txt || (cat $(BUILD)/sample_$(1)_headless.txt; exit 1)
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

$(SCENE_DATA): $(MAKE_ACTOR_DATA)
	./$(MAKE_ACTOR_DATA) $(GAME)/data $(CORE)/data $(GAME)/tests/fixtures/actors $(BUILD)/scene_data scene

$(MOTION_DATA): $(MAKE_ACTOR_DATA)
	./$(MAKE_ACTOR_DATA) $(GAME)/data $(CORE)/data $(GAME)/tests/fixtures/actors $(BUILD)/motion_data motion

.PHONY: $(ACTOR_DATA) $(INTERACT_DATA) $(BACKGROUND_DATA) $(SCENE_DATA) $(MOTION_DATA)

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

native-test: $(LIB) $(PLAYTHROUGH) $(TESTS) $(GAME_CLI) $(ACTOR_DATA) $(INTERACT_DATA) $(BACKGROUND_DATA) $(SCENE_DATA) $(MOTION_DATA)
	./$(TESTS) --data $(GAME)/data --actor-data $(ACTOR_DATA) --interact-data $(INTERACT_DATA) --background-data $(BACKGROUND_DATA) --scene-data $(SCENE_DATA) --motion-data $(MOTION_DATA) --scratch $(BUILD)/scratch
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
	rm -rf $(BUILD)/trouble_saves_cpp $(BUILD)/trouble_saves_c
	$(GAME_CLI) --data $(ACTOR_DATA) --seed 6 --script $(TROUBLE_SCRIPT) --saves $(BUILD)/trouble_saves_cpp > $(BUILD)/trouble_cpp.txt
	./$(PLAYTHROUGH) --data $(ACTOR_DATA) --seed 6 --script $(TROUBLE_SCRIPT) --saves $(BUILD)/trouble_saves_c > $(BUILD)/trouble_c.txt
	cmp $(BUILD)/trouble_c.txt $(BUILD)/trouble_cpp.txt
	cmp $(BUILD)/trouble_c.txt $(GAME)/tests/expected/trouble.txt
	diff -r $(BUILD)/trouble_saves_c $(BUILD)/trouble_saves_cpp
	@echo "C trouble script matches the C++ session: transcript and every save file"
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
	rm -rf $(BUILD)/approach_saves_cpp $(BUILD)/approach_saves_c
	$(GAME_CLI) --data $(BACKGROUND_DATA) --seed 6 --script $(APPROACH_SCRIPT) --saves $(BUILD)/approach_saves_cpp > $(BUILD)/approach_cpp.txt
	./$(PLAYTHROUGH) --data $(BACKGROUND_DATA) --seed 6 --script $(APPROACH_SCRIPT) --saves $(BUILD)/approach_saves_c > $(BUILD)/approach_c.txt
	cmp $(BUILD)/approach_c.txt $(BUILD)/approach_cpp.txt
	cmp $(BUILD)/approach_c.txt $(GAME)/tests/expected/approach.txt
	diff -r $(BUILD)/approach_saves_c $(BUILD)/approach_saves_cpp
	@echo "C approach script matches the C++ session: transcript and every save file"
	rm -rf $(BUILD)/scene_saves_cpp $(BUILD)/scene_saves_c
	$(GAME_CLI) --data $(SCENE_DATA) --seed 6 --script $(SCENE_SCRIPT) --saves $(BUILD)/scene_saves_cpp > $(BUILD)/scene_cpp.txt
	./$(PLAYTHROUGH) --data $(SCENE_DATA) --seed 6 --script $(SCENE_SCRIPT) --saves $(BUILD)/scene_saves_c > $(BUILD)/scene_c.txt
	cmp $(BUILD)/scene_c.txt $(BUILD)/scene_cpp.txt
	cmp $(BUILD)/scene_c.txt $(GAME)/tests/expected/scene.txt
	diff -r $(BUILD)/scene_saves_c $(BUILD)/scene_saves_cpp
	@echo "C scene script matches the C++ session: transcript and every save file"
	rm -rf $(BUILD)/motion_saves_cpp $(BUILD)/motion_saves_c
	$(GAME_CLI) --data $(MOTION_DATA) --seed 6 --script $(MOTION_SCRIPT) --saves $(BUILD)/motion_saves_cpp > $(BUILD)/motion_cpp.txt
	./$(PLAYTHROUGH) --data $(MOTION_DATA) --seed 6 --script $(MOTION_SCRIPT) --saves $(BUILD)/motion_saves_c > $(BUILD)/motion_c.txt
	cmp $(BUILD)/motion_c.txt $(BUILD)/motion_cpp.txt
	cmp $(BUILD)/motion_c.txt $(GAME)/tests/expected/motion.txt
	diff -r $(BUILD)/motion_saves_c $(BUILD)/motion_saves_cpp
	@echo "C motion script matches the C++ session: transcript and every save file"

dotnet: $(LIB)
	$(DOTNET) build csharp/Psiv.Session.Tests/Psiv.Session.Tests.csproj -nologo -v q -warnaserror
	$(DOTNET) build $(SAMPLE) -nologo -v q -warnaserror

dotnet-test: dotnet $(ACTOR_DATA) $(INTERACT_DATA) $(BACKGROUND_DATA) $(SCENE_DATA) $(MOTION_DATA)
	PSIV_DATA_DIR=$(CURDIR)/$(GAME)/data PSIV_ACTOR_DATA_DIR=$(CURDIR)/$(ACTOR_DATA) PSIV_INTERACT_DATA_DIR=$(CURDIR)/$(INTERACT_DATA) PSIV_BACKGROUND_DATA_DIR=$(CURDIR)/$(BACKGROUND_DATA) PSIV_SCENE_DATA_DIR=$(CURDIR)/$(SCENE_DATA) PSIV_MOTION_DATA_DIR=$(CURDIR)/$(MOTION_DATA) $(DOTNET) test csharp/Psiv.Session.Tests/Psiv.Session.Tests.csproj --no-build -nologo
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
	# The capabilities report lists every field the broken copy's check exercises.
	$(SAMPLE_RUN) --capabilities > $(BUILD)/capabilities.json
	python3 -I $(SAMPLE_SCRIPTS)/check_capabilities.py $(BUILD)/capabilities.json $(BUILD)/check_assets_broken.txt
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
	# The last animation additions (make_anim_assets.py --leftovers: number windows with a row per side, a padding pair, the
	# battle frame and a close, the member choosing or acting alone, palettes by index, idle states as data, a backdrop
	# restored inside an attack, six enemies in two ranks in both layouts, a projectile's trail, direction frames and
	# `flip: auto`, a shake on script time, a mosaic, a layout key; the previewer's enemy actors, item keys, dummy and target
	# counts, ruler toggle and windows after a seek), on a copy since the run swaps a layout in.
	python3 -I $(SAMPLE_SCRIPTS)/make_anim_assets.py --leftovers $(BUILD)/export $(BUILD)/anim_leftovers_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --check-assets --assets $(BUILD)/anim_leftovers_assets
	rm -rf $(BUILD)/sample_anim_leftovers_saves $(BUILD)/anim_leftovers_run
	cp -R $(BUILD)/anim_leftovers_assets $(BUILD)/anim_leftovers_run
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_anim_leftovers_saves --assets $(BUILD)/anim_leftovers_run \
		--script $(SAMPLE_SCRIPTS)/battle_anim_leftovers.txt --frames 6000 > $(BUILD)/sample_anim_leftovers_headless.txt || (cat $(BUILD)/sample_anim_leftovers_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_anim_leftovers_headless.txt
	# The example set in the side layout, and the combination and the two-weapon variants played live through the `equip`
	# and `combo` verbs in both layouts.
	rm -rf $(BUILD)/sample_anim_side_saves $(BUILD)/anim_side_run
	cp -R $(BUILD)/anim_assets $(BUILD)/anim_side_run
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_anim_side_saves --assets $(BUILD)/anim_side_run \
		--script $(SAMPLE_SCRIPTS)/battle_anim_side.txt --frames 9500 > $(BUILD)/sample_anim_side_headless.txt || (cat $(BUILD)/sample_anim_side_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_anim_side_headless.txt
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
	# Narration gathered in a window without a box over stacked still pictures, in English and Japanese.
	python3 -I $(SAMPLE_SCRIPTS)/make_narration_data.py $(GAME)/data $(CORE)/data $(BUILD)/narration_data
	python3 -I $(SAMPLE_SCRIPTS)/make_narration_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/narration_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/narration_data/game/data --check-assets --assets $(BUILD)/narration_assets
	$(call narration_run,narration,)
	$(call narration_run,narration_ja,--language ja --strict-text)
	$(call narration_run,narration_auto,)
	# Every newer data shape the library accepts (object flags, pictures, music fade and area lists, field status, name
	# lists, counters, exit sounds and closing flags, wanderers, patrols, paths, platforms) read with no data problem.
	python3 -I $(SAMPLE_SCRIPTS)/make_shapes_data.py $(MOTION_DATA)/../.. $(BUILD)/shapes_data
	rm -rf $(BUILD)/sample_shapes_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/shapes_data/game/data --dev --saves $(BUILD)/sample_shapes_saves \
		--script $(SAMPLE_SCRIPTS)/shapes.txt --frames 1200 > $(BUILD)/sample_shapes_headless.txt || (cat $(BUILD)/sample_shapes_headless.txt; exit 1)
	! grep "data problem" $(BUILD)/sample_shapes_headless.txt
	@tail -n 1 $(BUILD)/sample_shapes_headless.txt
	# The line tag {narration:nowait} under the press mode, on a data copy with an event that uses it.
	python3 -I $(SAMPLE_SCRIPTS)/make_narration_data.py $(GAME)/data $(CORE)/data $(BUILD)/nowait_data --nowait
	rm -rf $(BUILD)/sample_narration_nowait_saves $(BUILD)/narration_nowait_run_assets
	cp -R $(BUILD)/narration_assets $(BUILD)/narration_nowait_run_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/nowait_data/game/data --dev --saves $(BUILD)/sample_narration_nowait_saves \
		--assets $(BUILD)/narration_nowait_run_assets --script $(SAMPLE_SCRIPTS)/narration_nowait.txt --frames 900 > $(BUILD)/sample_narration_nowait_headless.txt \
		|| (cat $(BUILD)/sample_narration_nowait_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_narration_nowait_headless.txt
	# The text and quest logs laid out by the built-in files at 320x224 and 192x144, after the tags scene.
	rm -rf $(BUILD)/sample_small_logs_saves $(BUILD)/small_logs_assets
	cp -R $(BUILD)/small_assets $(BUILD)/small_logs_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/tag_data/game/data --dev --saves $(BUILD)/sample_small_logs_saves --assets $(BUILD)/small_logs_assets \
		--script $(SAMPLE_SCRIPTS)/small_logs.txt --frames 600 > $(BUILD)/sample_small_logs_headless.txt || (cat $(BUILD)/sample_small_logs_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_small_logs_headless.txt
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
	rm -rf $(BUILD)/sample_field_trail_saves
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_field_trail_saves --script $(SAMPLE_SCRIPTS)/field_trail.txt --frames 800 > $(BUILD)/sample_field_trail_headless.txt \
		|| (cat $(BUILD)/sample_field_trail_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_field_trail_headless.txt
	# A step time with a fraction and two belts on the same cells that reverse with a flag, which the sample reads
	# without a problem, under the real-time field input.
	rm -rf $(BUILD)/sample_belts_saves
	python3 -I $(SAMPLE_SCRIPTS)/make_belt_data.py $(GAME)/data $(CORE)/data $(BUILD)/belt_data
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/belt_data/game/data --dev --saves $(BUILD)/sample_belts_saves \
		--script $(SAMPLE_SCRIPTS)/belts.txt --frames 700 > $(BUILD)/sample_belts_headless.txt || (cat $(BUILD)/sample_belts_headless.txt; exit 1)
	! grep "data problem" $(BUILD)/sample_belts_headless.txt
	@tail -n 1 $(BUILD)/sample_belts_headless.txt
	# The real-time glide landing with the session on Belt Yard's 262.5 ms steps.
	rm -rf $(BUILD)/sample_glide_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/belt_data/game/data --dev --saves $(BUILD)/sample_glide_saves \
		--script $(SAMPLE_SCRIPTS)/glide.txt --frames 500 > $(BUILD)/sample_glide_headless.txt || (cat $(BUILD)/sample_glide_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_glide_headless.txt
	# Scene presentation: story pictures, the scene camera, music fades, an exit's sound, the ambient talk option and the
	# keeper's line with its parameters on the hall data copy; Macro and Auto aboard, the game over's return and followers arriving behind on the default data, each
	# on its own copy of the scene assets (they swap files in); the above picture over everything on the background data.
	python3 -I $(SAMPLE_SCRIPTS)/make_scene_data.py $(SCENE_DATA)/../.. $(BUILD)/hall_data
	python3 -I $(SAMPLE_SCRIPTS)/make_scene_assets.py monogame/Psiv.Sample/ui $(BUILD)/scene_assets $(BUILD)/above_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/hall_data/game/data --check-assets --assets $(BUILD)/scene_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BACKGROUND_DATA) --check-assets --assets $(BUILD)/above_assets
	@for run in scene:$(BUILD)/hall_data/game/data:2400 shop_voice:$(BUILD)/hall_data/game/data:1200 vehicle_commands:$(GAME)/data:5000 game_over:$(GAME)/data:3000 arrive_behind:$(GAME)/data:1400 \
		scene_overlay:$(BUILD)/hall_data/game/data:600; do \
		name=$${run%%:*}; rest=$${run#*:}; data=$${rest%%:*}; frames=$${rest#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves $(BUILD)/$${name}_run_assets; \
		cp -R $(BUILD)/scene_assets $(BUILD)/$${name}_run_assets; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $$data --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/$${name}_run_assets \
			--script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	rm -rf $(BUILD)/sample_above_cover_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BACKGROUND_DATA) --dev --saves $(BUILD)/sample_above_cover_saves --assets $(BUILD)/above_assets \
		--script $(SAMPLE_SCRIPTS)/above_cover.txt --frames 1400 > $(BUILD)/sample_above_cover_headless.txt || (cat $(BUILD)/sample_above_cover_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_above_cover_headless.txt
	# World motion from the manifest on the background data (make_world_motion_assets.py, played on a copy since the hot reload
	# swaps files in): Wren Yard's palette tracks, frame runs and layers, Wren Loft's tileset palette.
	python3 -I $(SAMPLE_SCRIPTS)/make_world_motion_assets.py $(BUILD)/world_motion_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BACKGROUND_DATA) --check-assets --assets $(BUILD)/world_motion_assets
	rm -rf $(BUILD)/sample_world_motion_saves $(BUILD)/world_motion_run
	cp -R $(BUILD)/world_motion_assets $(BUILD)/world_motion_run
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BACKGROUND_DATA) --dev --saves $(BUILD)/sample_world_motion_saves --assets $(BUILD)/world_motion_run \
		--script $(SAMPLE_SCRIPTS)/world_motion.txt --frames 700 > $(BUILD)/sample_world_motion_headless.txt || (cat $(BUILD)/sample_world_motion_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_world_motion_headless.txt
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
	# The library state the sample adopts (make_adopt_assets.py, make_adopt_data.py), each run from saves of the scripted
	# playthrough on its data copy: moving platforms, path and patrol actors in Wren Mill on the motion data; the title's
	# Load, an area's name variant, the status page's detail fields, a skill's maximum uses, a refused ally greyed out and
	# floating numbers applied or rolled on the adopt data.
	python3 -I $(SAMPLE_SCRIPTS)/make_adopt_assets.py monogame/Psiv.Sample/ui $(BUILD)/adopt_assets
	python3 -I $(SAMPLE_SCRIPTS)/make_adopt_data.py $(GAME)/data $(CORE)/data $(BUILD)/adopt_data
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/adopt_data/game/data --check-assets --assets $(BUILD)/adopt_assets
	@for run in motion:$(MOTION_DATA):1200 adopt:$(BUILD)/adopt_data/game/data:4500; do \
		name=$${run%%:*}; rest=$${run#*:}; data=$${rest%%:*}; frames=$${rest#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves $(BUILD)/$${name}_run_assets; \
		cp -R $(BUILD)/adopt_assets $(BUILD)/$${name}_run_assets; \
		$(GAME_CLI) --data $$data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/sample_$${name}_saves > /dev/null || exit 1; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $$data --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/$${name}_run_assets \
			--script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
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
	# The menu items (make_menu_assets.py): the field menu's own entries with the technique, skill, ability and system pages
	# and party talk, the Extras menu from the title, the field, the field menu and a battle, the Controls page listing it,
	# and the battle's command rows by step in both layouts.
	python3 -I $(SAMPLE_SCRIPTS)/make_menu_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/menu_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/menu_assets
	@for run in menus_layout:1700 menus_battle:2500; do \
		name=$${run%%:*}; frames=$${run##*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves; \
		$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/menu_assets --script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames \
			> $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	# The menu follow-ups (make_menus_more_assets.py): a tab's children, the tabs' sizes, abilities not usable in the field
	# hidden, the party talk on its page and the System action; each problem the children can have, on a copy.
	python3 -I $(SAMPLE_SCRIPTS)/make_menus_more_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/menus_more_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/menus_more_assets
	rm -rf $(BUILD)/menus_more_broken_assets
	cp -R $(BUILD)/menus_more_assets $(BUILD)/menus_more_broken_assets
	cp $(BUILD)/menus_more_broken_assets/alt/menu_broken.json $(BUILD)/menus_more_broken_assets/ui/menu.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/menus_more_broken_assets > $(BUILD)/check_assets_menus_more.txt
	grep -qF "entries[0].children: must be a non-empty list of entries" $(BUILD)/check_assets_menus_more.txt
	grep -qF "entries[1].children[1]: field 'page' must be one of" $(BUILD)/check_assets_menus_more.txt
	grep -qF "entries[1].children[2]: 'save' is listed twice" $(BUILD)/check_assets_menus_more.txt
	grep -qF "entries[1].children[3]: missing field 'page'" $(BUILD)/check_assets_menus_more.txt
	grep -qF "entries[1].children[4]: unknown field 'children'" $(BUILD)/check_assets_menus_more.txt
	rm -rf $(BUILD)/sample_menus_more_saves $(BUILD)/menus_more_run
	cp -R $(BUILD)/menus_more_assets $(BUILD)/menus_more_run
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_menus_more_saves --assets $(BUILD)/menus_more_run --script $(SAMPLE_SCRIPTS)/menus_more.txt --frames 900 \
		> $(BUILD)/sample_menus_more_headless.txt || (cat $(BUILD)/sample_menus_more_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_menus_more_headless.txt
	# The layout choices (make_choices_assets.py): the battle's Item list order and the auto macro first, the Items page's
	# actions with their outcomes on the page, and the title's Continue opening the Load list; a copy with a bad
	# `continue` and `actions` reports both.
	python3 -I $(SAMPLE_SCRIPTS)/make_choices_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/choices_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/choices_assets
	rm -rf $(BUILD)/choices_broken_assets
	cp -R $(BUILD)/choices_assets $(BUILD)/choices_broken_assets
	cp $(BUILD)/choices_broken_assets/alt/title_broken.json $(BUILD)/choices_broken_assets/ui/title.json
	cp $(BUILD)/choices_broken_assets/alt/menu_items_broken.json $(BUILD)/choices_broken_assets/ui/menu_items.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/choices_broken_assets > $(BUILD)/check_assets_choices.txt
	grep -qF "params.continue: must be \"newest\" or \"choose\"" $(BUILD)/check_assets_choices.txt
	grep -qF "params.actions: field 'actions' reads 'yes'" $(BUILD)/check_assets_choices.txt
	rm -rf $(BUILD)/sample_layout_choices_saves $(BUILD)/choices_run
	cp -R $(BUILD)/choices_assets $(BUILD)/choices_run
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_layout_choices_saves --assets $(BUILD)/choices_run \
		--script $(SAMPLE_SCRIPTS)/layout_choices.txt --frames 1800 > $(BUILD)/sample_layout_choices_headless.txt || (cat $(BUILD)/sample_layout_choices_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_layout_choices_headless.txt
	# The library state of the last rules passes (make_library_data.py, make_library_assets.py): followers drawn from the
	# session's trail under game.json's followers_arrive, the exits' rule on a 2x2 block of exits with keys and clicks in
	# both field inputs, shop and inn names with the bag's slots and its refusals, no autosaves, the two field-status
	# notices from a save field_status_cli.txt writes with game_cli, and the macro options, step checks, flow, failed
	# steps and auto modes; a copy with a bad `macro_failed`, `macro_flow` and `options` reports each; followers placed by
	# the exit taken and a wanderer whose talk point answers where she stands (moving_talk.txt on the --moving copy).
	python3 -I $(SAMPLE_SCRIPTS)/make_library_data.py $(GAME)/data $(CORE)/data $(BUILD)/library_data
	python3 -I $(SAMPLE_SCRIPTS)/make_library_data.py $(GAME)/data $(CORE)/data $(BUILD)/library_moving_data --moving
	python3 -I $(SAMPLE_SCRIPTS)/make_library_data.py $(GAME)/data $(CORE)/data $(BUILD)/library_bag_data --bag
	python3 -I $(SAMPLE_SCRIPTS)/make_library_data.py $(GAME)/data $(CORE)/data $(BUILD)/library_noauto_data --no-autosave
	python3 -I $(SAMPLE_SCRIPTS)/make_library_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/library_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/library_assets
	rm -rf $(BUILD)/library_broken_assets
	cp -R $(BUILD)/library_assets $(BUILD)/library_broken_assets
	cp $(BUILD)/library_broken_assets/alt/battle_front_broken.json $(BUILD)/library_broken_assets/ui/battle_front.json
	cp $(BUILD)/library_broken_assets/alt/menu_macros_broken.json $(BUILD)/library_broken_assets/ui/menu_macros.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/library_broken_assets > $(BUILD)/check_assets_library.txt
	grep -qF "params.macro_failed: must be \"automatic\", \"attack\" or \"lose_turn\"" $(BUILD)/check_assets_library.txt
	grep -qF "params.macro_flow: must be \"reset\" or \"remember\"" $(BUILD)/check_assets_library.txt
	grep -qF "params.options: field 'options' reads 'yes'" $(BUILD)/check_assets_library.txt
	rm -rf $(BUILD)/sample_library_saves
	$(GAME_CLI) --data $(BUILD)/library_data/game/data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/sample_library_saves > /dev/null
	$(GAME_CLI) --data $(BUILD)/library_data/game/data --seed 6 --script $(SAMPLE_SCRIPTS)/field_status_cli.txt --saves $(BUILD)/sample_library_saves \
		> $(BUILD)/field_status_cli.txt
	grep -qF "Saved to manual_2." $(BUILD)/field_status_cli.txt
	@for run in library_followers:library_data:400:saves field_status:library_data:600:saves library_autosave:library_noauto_data:1000:saves \
		exit_block:library_data:1500:- library_shop:library_bag_data:1500:- moving_talk:library_moving_data:600:saves; do \
		name=$${run%%:*}; rest=$${run#*:}; data=$${rest%%:*}; rest=$${rest#*:}; frames=$${rest%%:*}; saves=$${rest#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves; \
		if [ $$saves = saves ]; then cp -R $(BUILD)/sample_library_saves $(BUILD)/sample_$${name}_saves; fi; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/$$data/game/data --dev --saves $(BUILD)/sample_$${name}_saves \
			--script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	rm -rf $(BUILD)/sample_library_macros_saves $(BUILD)/library_macros_run
	cp -R $(BUILD)/library_assets $(BUILD)/library_macros_run
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_library_macros_saves --assets $(BUILD)/library_macros_run --script $(SAMPLE_SCRIPTS)/library_macros.txt --frames 2400 \
		> $(BUILD)/sample_library_macros_headless.txt || (cat $(BUILD)/sample_library_macros_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_library_macros_headless.txt
	# The newer menu slots at 320x224 (make_small_more_assets.py on the small set): the party talk's box by the talk params
	# and text styles, `immediate` entries (the talk at once, a single child in place of its list) and the children list
	# by the menu_children params; a copy with a bad `immediate` reports both problems.
	python3 -I $(SAMPLE_SCRIPTS)/make_small_more_assets.py $(BUILD)/export $(BUILD)/small_assets $(BUILD)/small_more_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/small_more_assets
	rm -rf $(BUILD)/small_more_broken_assets
	cp -R $(BUILD)/small_more_assets $(BUILD)/small_more_broken_assets
	cp $(BUILD)/small_more_broken_assets/alt/menu_broken.json $(BUILD)/small_more_broken_assets/ui/menu.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/small_more_broken_assets > $(BUILD)/check_assets_small_more.txt
	grep -qF "entries[0]: 'immediate' acts only on the field menu's party_talk and extras pages and on an entry with children" $(BUILD)/check_assets_small_more.txt
	grep -qF "entries[1]: field 'immediate' must be true or false" $(BUILD)/check_assets_small_more.txt
	rm -rf $(BUILD)/sample_small_more_saves $(BUILD)/small_more_run
	cp -R $(BUILD)/small_more_assets $(BUILD)/small_more_run
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_small_more_saves --assets $(BUILD)/small_more_run --script $(SAMPLE_SCRIPTS)/small_more.txt --frames 800 \
		> $(BUILD)/sample_small_more_headless.txt || (cat $(BUILD)/sample_small_more_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_small_more_headless.txt
	# More motion (make_motion_more_assets.py: a sprite's and the generated tilesets' palette tracks, a layer placed with
	# `at`, idle bob and pulse, an encounter's key in the previewer) on the background data, on a copy.
	python3 -I $(SAMPLE_SCRIPTS)/make_motion_more_assets.py $(BUILD)/motion_more_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BACKGROUND_DATA) --check-assets --assets $(BUILD)/motion_more_assets
	rm -rf $(BUILD)/sample_motion_more_saves $(BUILD)/motion_more_run
	cp -R $(BUILD)/motion_more_assets $(BUILD)/motion_more_run
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BACKGROUND_DATA) --dev --saves $(BUILD)/sample_motion_more_saves --assets $(BUILD)/motion_more_run \
		--script $(SAMPLE_SCRIPTS)/motion_more.txt --frames 800 > $(BUILD)/sample_motion_more_headless.txt || (cat $(BUILD)/sample_motion_more_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_motion_more_headless.txt
	# The host calls the sample adopts (make_host_data.py, make_host_assets.py), from saves of the scripted playthrough on
	# the host data copy: the newest save preselected, menu children by `when` and `enabled` with `immediate`, Discard,
	# the party talk page from PartyTalkLines, `combo` on the console, allies out of reach greyed before the pick, exit
	# markers by how the exit fires and the shadow's palette in battle (host_menus); control tags in text outside the
	# dialogue box (host_tags); allies out of reach under `item_no_reach` "consume" (host_reach); a copy with bad
	# children `when` and `enabled` reports both.
	python3 -I $(SAMPLE_SCRIPTS)/make_host_data.py $(GAME)/data $(CORE)/data $(BUILD)/host_data
	python3 -I $(SAMPLE_SCRIPTS)/make_host_data.py $(GAME)/data $(CORE)/data $(BUILD)/host_consume_data --consume
	python3 -I $(SAMPLE_SCRIPTS)/make_host_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/host_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/host_data/game/data --check-assets --assets $(BUILD)/host_assets
	rm -rf $(BUILD)/host_broken_assets
	cp -R $(BUILD)/host_assets $(BUILD)/host_broken_assets
	cp $(BUILD)/host_broken_assets/alt/menu_broken.json $(BUILD)/host_broken_assets/ui/menu.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/host_broken_assets > $(BUILD)/check_assets_host.txt
	grep -qF "children[0]: field 'when' reads 'nonsense', which is not a field of the menu screen" $(BUILD)/check_assets_host.txt
	grep -qF "children[1]: field 'enabled' must be an expression (a number or a string)" $(BUILD)/check_assets_host.txt
	rm -rf $(BUILD)/sample_host_saves
	$(GAME_CLI) --data $(BUILD)/host_data/game/data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/sample_host_saves > /dev/null
	@for run in host_menus:host_data:3000 host_tags:host_data:1200 host_reach:host_consume_data:1500; do \
		name=$${run%%:*}; rest=$${run#*:}; data=$${rest%%:*}; frames=$${rest#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves $(BUILD)/$${name}_run_assets; \
		cp -R $(BUILD)/sample_host_saves $(BUILD)/sample_$${name}_saves; \
		cp -R $(BUILD)/host_assets $(BUILD)/$${name}_run_assets; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/$$data/game/data --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/$${name}_run_assets \
			--script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	# The party talk one speaker at a time (make_talk_data.py, make_talk_assets.py), from saves of the scripted playthrough
	# on the talk data copy: three speakers, one in two pages and one whose tag selects a portrait expression, at
	# 1280x720 (talk_speakers: `immediate`, the reveal and Confirm, Back, `talk_hide_menu`, the Extras menu, nobody to
	# talk) and on the small set (small_talk: a long line in two boxes of the window's rows); a copy with a `party_talk`
	# word that is none of the three reports it.
	python3 -I $(SAMPLE_SCRIPTS)/make_talk_data.py $(GAME)/data $(CORE)/data $(BUILD)/talk_data
	python3 -I $(SAMPLE_SCRIPTS)/make_talk_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/talk_assets
	python3 -I $(SAMPLE_SCRIPTS)/make_talk_assets.py $(BUILD)/export $(BUILD)/small_assets $(BUILD)/small_talk_assets --small
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/talk_data/game/data --check-assets --assets $(BUILD)/talk_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/talk_data/game/data --check-assets --assets $(BUILD)/small_talk_assets
	rm -rf $(BUILD)/talk_broken_assets
	cp -R $(BUILD)/talk_assets $(BUILD)/talk_broken_assets
	cp $(BUILD)/talk_broken_assets/alt/menu_options_broken.json $(BUILD)/talk_broken_assets/ui/menu_options.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/talk_broken_assets > $(BUILD)/check_assets_talk.txt
	grep -qF 'params.party_talk: must be "close", "page" or "speakers"' $(BUILD)/check_assets_talk.txt
	rm -rf $(BUILD)/sample_talk_saves
	$(GAME_CLI) --data $(BUILD)/talk_data/game/data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/sample_talk_saves > /dev/null
	@for run in talk_speakers:talk_assets:1800 small_talk:small_talk_assets:1500; do \
		name=$${run%%:*}; rest=$${run#*:}; set=$${rest%%:*}; frames=$${rest#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves $(BUILD)/$${name}_run_assets; \
		cp -R $(BUILD)/sample_talk_saves $(BUILD)/sample_$${name}_saves; \
		cp -R $(BUILD)/$$set $(BUILD)/$${name}_run_assets; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/talk_data/game/data --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/$${name}_run_assets \
			--script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	# Screens over screens and screens at small sizes (make_screens_assets.py): the notices drawn once by the topmost
	# screen with a toasts slot, a page the field menu leaves out opened from Extras, title entries by `when`, Controls
	# without Extras; the name pad's sizes, the built-in pause at 320x224, the developer badge's place and the music cut.
	python3 -I $(SAMPLE_SCRIPTS)/make_screens_assets.py $(BUILD)/export $(BUILD)/menu_assets $(BUILD)/small_assets $(BUILD)/stack_assets $(BUILD)/small_screens_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/stack_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/small_screens_assets
	@for run in stacked_screens:stack_assets small_screens:small_screens_assets; do \
		name=$${run%%:*}; set=$${run#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves $(BUILD)/$${name}_run; \
		cp -R $(BUILD)/$$set $(BUILD)/$${name}_run; \
		$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/$${name}_run --script $(SAMPLE_SCRIPTS)/$$name.txt --frames 1500 \
			> $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	# Where narration goes by `narration_when`, the picture stack's hide and clear, the page arrow and shown_ms.
	python3 -I $(SAMPLE_SCRIPTS)/make_narration_data.py $(GAME)/data $(CORE)/data $(BUILD)/route_data --routing
	python3 -I $(SAMPLE_SCRIPTS)/make_narration_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/route_assets --route
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/route_data/game/data --check-assets --assets $(BUILD)/route_assets
	rm -rf $(BUILD)/sample_narration_route_saves $(BUILD)/route_run
	cp -R $(BUILD)/route_assets $(BUILD)/route_run
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/route_data/game/data --dev --saves $(BUILD)/sample_narration_route_saves --assets $(BUILD)/route_run \
		--script $(SAMPLE_SCRIPTS)/narration_route.txt --frames 2000 > $(BUILD)/sample_narration_route_headless.txt || (cat $(BUILD)/sample_narration_route_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_narration_route_headless.txt
	# The shop in steps (make_shop_assets.py): choice, list, quantity with its total, result, keeper lines and outcomes in
	# result_text; then the tab layout with a result line.
	python3 -I $(SAMPLE_SCRIPTS)/make_shop_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/shop_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/shop_assets
	rm -rf $(BUILD)/sample_shop_steps_saves $(BUILD)/shop_run
	cp -R $(BUILD)/shop_assets $(BUILD)/shop_run
	$(SAMPLE_RUN) --dev --saves $(BUILD)/sample_shop_steps_saves --assets $(BUILD)/shop_run --script $(SAMPLE_SCRIPTS)/shop_steps.txt --frames 1500 \
		> $(BUILD)/sample_shop_steps_headless.txt || (cat $(BUILD)/sample_shop_steps_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_shop_steps_headless.txt
	# The layout flows (make_flows_assets.py) on the front data copy: the title's intro, the field menu's entries as a list,
	# Items rows one per copy with their counts apart, the shop's and inn's flows, travel at once and its messages, a delayed
	# first command window and the result in steps, a vehicle battle's own layout, and the intro after a defeat (flows); the
	# same at 320x224 on the small set (small_flows); copies with a bad `after_defeat` and intro, `after_defeat` "intro"
	# without an intro, and a bad `navigation` report them.
	python3 -I $(SAMPLE_SCRIPTS)/make_flows_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/flows_assets
	python3 -I $(SAMPLE_SCRIPTS)/make_flows_assets.py $(BUILD)/export $(BUILD)/small_assets $(BUILD)/small_flows_assets --small
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --check-assets --assets $(BUILD)/flows_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --check-assets --assets $(BUILD)/small_flows_assets
	rm -rf $(BUILD)/flows_broken_assets
	cp -R $(BUILD)/flows_assets $(BUILD)/flows_broken_assets
	cp $(BUILD)/flows_broken_assets/alt/title_broken.json $(BUILD)/flows_broken_assets/ui/title.json
	cp $(BUILD)/flows_broken_assets/alt/menu_broken.json $(BUILD)/flows_broken_assets/ui/menu.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/flows_broken_assets > $(BUILD)/check_assets_flows.txt
	grep -qF 'params.after_defeat: must be "menu", "start", "intro" or "intro:STEP"' $(BUILD)/check_assets_flows.txt
	grep -qF "intro[0]: field 'ms' must be a whole number from 1 to 60000" $(BUILD)/check_assets_flows.txt
	grep -qF "intro[1]: 'studio' is listed twice" $(BUILD)/check_assets_flows.txt
	grep -qF "intro[2]: missing field 'name'" $(BUILD)/check_assets_flows.txt
	grep -qF 'params.navigation: must be "tabs" or "list"' $(BUILD)/check_assets_flows.txt
	cp $(BUILD)/flows_broken_assets/alt/title_no_intro.json $(BUILD)/flows_broken_assets/ui/title.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/flows_broken_assets > $(BUILD)/check_assets_flows.txt
	grep -qF "params.after_defeat: \"intro\" needs the file's 'intro'" $(BUILD)/check_assets_flows.txt
	@for run in flows:flows_assets:2200 small_flows:small_flows_assets:1500; do \
		name=$${run%%:*}; rest=$${run#*:}; set=$${rest%%:*}; frames=$${rest#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves $(BUILD)/$${name}_run; \
		cp -R $(BUILD)/$$set $(BUILD)/$${name}_run; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/front_data/game/data --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/$${name}_run \
			--script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
	# The second layout flows (make_more_flows_data.py, make_more_flows_assets.py), from saves of the scripted playthrough on
	# their data copy: the intro's image elements with `src` and `repeat` over manifest pictures, the Load screen's slot
	# blocks in a grid and a row, the inn's decline step, the shop's question step, travel over the menu by thing and kind,
	# the level-ups by page, a vehicle's grouped Weapons row and the intro from a step after a defeat (more_flows); the same
	# at 320x224 on the small set but travel and the vehicle battle (small_more_flows); copies with an `after_defeat` step
	# the intro lacks, a question without a confirm, a decline after the farewell and a bad Load `navigation` report them.
	python3 -I $(SAMPLE_SCRIPTS)/make_more_flows_data.py $(GAME)/data $(CORE)/data $(BUILD)/more_flows_data
	python3 -I $(SAMPLE_SCRIPTS)/make_more_flows_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/more_flows_assets
	python3 -I $(SAMPLE_SCRIPTS)/make_more_flows_assets.py $(BUILD)/export $(BUILD)/small_assets $(BUILD)/small_more_flows_assets --small
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/more_flows_data/game/data --check-assets --assets $(BUILD)/more_flows_assets
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/more_flows_data/game/data --check-assets --assets $(BUILD)/small_more_flows_assets
	rm -rf $(BUILD)/more_flows_broken_assets
	cp -R $(BUILD)/more_flows_assets $(BUILD)/more_flows_broken_assets
	cp $(BUILD)/more_flows_broken_assets/alt/title_broken.json $(BUILD)/more_flows_broken_assets/ui/title.json
	cp $(BUILD)/more_flows_broken_assets/alt/shop_broken.json $(BUILD)/more_flows_broken_assets/ui/shop.json
	cp $(BUILD)/more_flows_broken_assets/alt/inn_broken.json $(BUILD)/more_flows_broken_assets/ui/inn.json
	cp $(BUILD)/more_flows_broken_assets/alt/load_broken.json $(BUILD)/more_flows_broken_assets/ui/load_game.json
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/more_flows_broken_assets > $(BUILD)/check_assets_more_flows.txt
	grep -qF "params.after_defeat: \"intro:credits\" names no step of the file's 'intro'" $(BUILD)/check_assets_more_flows.txt
	grep -qF "flow: 'question' needs the step 'confirm' after it" $(BUILD)/check_assets_more_flows.txt
	grep -qF "flow: must keep the order welcome, price, choice, result, decline, farewell" $(BUILD)/check_assets_more_flows.txt
	grep -qF 'params.navigation: must be "list", "grid" or "row"' $(BUILD)/check_assets_more_flows.txt
	rm -rf $(BUILD)/sample_more_flows_base_saves
	$(GAME_CLI) --data $(BUILD)/more_flows_data/game/data --seed 6 --auto-battle --script $(SCRIPT) --saves $(BUILD)/sample_more_flows_base_saves > /dev/null
	@for run in more_flows:more_flows_assets:1800 small_more_flows:small_more_flows_assets:1000; do \
		name=$${run%%:*}; rest=$${run#*:}; set=$${rest%%:*}; frames=$${rest#*:}; \
		rm -rf $(BUILD)/sample_$${name}_saves $(BUILD)/$${name}_run; \
		cp -R $(BUILD)/sample_more_flows_base_saves $(BUILD)/sample_$${name}_saves; \
		cp -R $(BUILD)/$$set $(BUILD)/$${name}_run; \
		$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(BUILD)/more_flows_data/game/data --dev --saves $(BUILD)/sample_$${name}_saves --assets $(BUILD)/$${name}_run \
			--script $(SAMPLE_SCRIPTS)/$$name.txt --frames $$frames > $(BUILD)/sample_$${name}_headless.txt || { cat $(BUILD)/sample_$${name}_headless.txt; exit 1; }; \
		tail -n 1 $(BUILD)/sample_$${name}_headless.txt; \
	done
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
