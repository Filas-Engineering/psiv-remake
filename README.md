# psiv-remake

Scoping for a modern remake of Phantasy Star IV.

- [Handover](docs/HANDOVER.md): current state, how to build and test, what matches the original and what is left
- [Scope](docs/scope.md): what the remake is, what it is not, phases, and open questions
- [Behavior spec](docs/spec/README.md): numbered requirements per system, for the engine team to build against
- [Information needed](docs/info-needed.md): the facts about the original game still to confirm, and the data tables to deliver
- [Battle rules](core/README.md): headless C++17 battle library with placeholder data, a terminal front end and tests
- [Event scripts](events/README.md): text format for cutscenes and field events, with a resumable interpreter and tests
- [Game session](game/README.md): joins event scripts to battles, with areas, encounters, shops and saving. Playable in the terminal through a small invented adventure
- [Text tooling](tools/text/README.md): line database, name glossary, rename review and localization export for a delivered script
- [Acceptance checks](acceptance/README.md): the spec's checks that need no rendering, run against the libraries, with a report mapping every requirement to its status
- [Bindings](bindings/README.md): a C interface over the game session, a C# wrapper, and a MonoGame sample that plays the placeholder adventure

This repo holds planning documents, engine-neutral libraries and tooling. The libraries ship with invented placeholder data only. It holds no script text, art, audio or other source material. Those stay in the rights holder's own storage until asset handling is agreed (see "Source material" in the scope).

## Build and test

Needs `clang++` (or another C++17 compiler: `make CXX=g++ CC=gcc ...`), GNU Make and Python 3.

```bash
make test
```

`make bindings` builds and tests the C interface, and the C# wrapper and MonoGame sample when the .NET SDK is at `~/.dotnet`.

`make acceptance` runs the spec's acceptance checks. It fails while any check contradicts the spec.

## Platforms

Verified with `make test`, `make acceptance` and `make bindings` (C tests, C# tests and the MonoGame sample's headless run):

* macOS 26.6 on arm64 with Apple clang 21.0.0 and .NET SDK 10.0.403.
* Ubuntu 24.04.5 on aarch64 and on x86-64 in Docker (image `mcr.microsoft.com/dotnet/sdk:10.0`, .NET SDK 10.0.404, GNU Make 4.3, Python 3.12.3), once with GCC 13.3.0 and once with Ubuntu clang 18.1.3.

Windows: the core, events and game unit tests pass cross-compiled with MinGW-w64 (GCC 14) under Wine 10 (`tools/windows-check.sh`).

Not verified: Windows with MSVC, the acceptance suites and bindings on Windows, and the sample's window on Linux (only its headless mode runs in the container).

To rerun the Linux check (needs Docker):

```bash
make linux-check              # or tools/linux-check.sh gcc, tools/linux-check.sh clang
PSIV_LINUX_PLATFORM=linux/amd64 tools/linux-check.sh   # another architecture
```

It builds a small image with g++, clang, make and python3 on top of the .NET SDK image, mounts the repo read-only, copies it inside the container and runs all three targets there, so no Linux build output lands in the working tree.
