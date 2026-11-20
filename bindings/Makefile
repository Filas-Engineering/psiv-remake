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
SAMPLE = monogame/Psiv.Sample/Psiv.Sample.csproj
SAMPLE_SCRIPT = monogame/Psiv.Sample/scripts/smoke.txt
SAMPLE_LARGE_SCRIPT = monogame/Psiv.Sample/scripts/large_areas.txt
SAMPLE_ACTORS_SCRIPT = monogame/Psiv.Sample/scripts/actors.txt

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

dotnet: $(LIB)
	$(DOTNET) build csharp/Psiv.Session.Tests/Psiv.Session.Tests.csproj -nologo -v q -warnaserror
	$(DOTNET) build $(SAMPLE) -nologo -v q -warnaserror

dotnet-test: dotnet
	PSIV_DATA_DIR=$(CURDIR)/$(GAME)/data $(DOTNET) test csharp/Psiv.Session.Tests/Psiv.Session.Tests.csproj --no-build -nologo
	rm -rf $(BUILD)/sample_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_saves --script $(SAMPLE_SCRIPT) --frames 2000 > $(BUILD)/sample_headless.txt \
		|| (cat $(BUILD)/sample_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_headless.txt
	rm -rf $(BUILD)/sample_large_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_large_saves --script $(SAMPLE_LARGE_SCRIPT) --frames 1600 > $(BUILD)/sample_large_headless.txt \
		|| (cat $(BUILD)/sample_large_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_large_headless.txt
	rm -rf $(BUILD)/sample_actors_saves
	$(DOTNET) run --no-build --project $(SAMPLE) -- --data $(GAME)/data --saves $(BUILD)/sample_actors_saves --script $(SAMPLE_ACTORS_SCRIPT) --frames 1300 > $(BUILD)/sample_actors_headless.txt \
		|| (cat $(BUILD)/sample_actors_headless.txt; exit 1)
	@tail -n 1 $(BUILD)/sample_actors_headless.txt

dotnet-missing:
	@echo "no dotnet at $(DOTNET): skipped the C# tests and the sample's headless run"

clean:
	rm -rf $(BUILD) csharp/*/bin csharp/*/obj monogame/*/bin monogame/*/obj

-include $(LIB_OBJS:.o=.d) $(PLAYTHROUGH_OBJS:.o=.d) $(TEST_OBJS:.o=.d)
