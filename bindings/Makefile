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

.PHONY: all test native-test dotnet dotnet-test dotnet-missing clean deps

all: $(LIB) $(PLAYTHROUGH) $(TESTS)
ifneq ($(wildcard $(DOTNET)),)
all: dotnet
endif

# The sibling libraries decide for themselves whether they are up to date.
deps:
	$(MAKE) -C $(CORE) build/librules.a
	$(MAKE) -C $(EVENTS) build/libevents.a
	$(MAKE) -C $(GAME) build/libgame.a build/game_cli$(EXE)

$(DEP_LIBS) $(GAME_CLI): deps

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

native-test: $(LIB) $(PLAYTHROUGH) $(TESTS) $(GAME_CLI)
	./$(TESTS) --data $(GAME)/data --scratch $(BUILD)/scratch
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

dotnet: $(LIB)
	$(DOTNET) build csharp/Psiv.Session.Tests/Psiv.Session.Tests.csproj -nologo -v q -warnaserror
	$(DOTNET) build $(SAMPLE) -nologo -v q -warnaserror

dotnet-test: dotnet
	PSIV_DATA_DIR=$(CURDIR)/$(GAME)/data $(DOTNET) test csharp/Psiv.Session.Tests/Psiv.Session.Tests.csproj --no-build -nologo
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
	python3 -I $(SAMPLE_SCRIPTS)/make_test_assets.py $(BUILD)/export $(BUILD)/test_assets $(BUILD)/broken_assets
	$(SAMPLE_RUN) --check-assets --assets $(BUILD)/test_assets
	! $(SAMPLE_RUN) --check-assets --assets $(BUILD)/broken_assets > $(BUILD)/check_assets_broken.txt
	diff $(SAMPLE_SCRIPTS)/check_assets_broken.expected $(BUILD)/check_assets_broken.txt
	rm -rf $(BUILD)/sample_assets_saves
	$(SAMPLE_RUN) --saves $(BUILD)/sample_assets_saves --assets $(BUILD)/test_assets --script $(SAMPLE_ASSETS_SCRIPT) --frames 2000 > $(BUILD)/sample_assets_headless.txt \
		|| (cat $(BUILD)/sample_assets_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_assets_headless.txt
	rm -rf $(BUILD)/sample_reload_saves $(BUILD)/reload_assets
	cp -R $(BUILD)/test_assets $(BUILD)/reload_assets
	$(SAMPLE_RUN) --saves $(BUILD)/sample_reload_saves --assets $(BUILD)/reload_assets --script $(SAMPLE_RELOAD_SCRIPT) --frames 400 > $(BUILD)/sample_reload_headless.txt \
		|| (cat $(BUILD)/sample_reload_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_reload_headless.txt
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
