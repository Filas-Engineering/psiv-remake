# psiv-remake

Scoping for a modern remake of Phantasy Star IV.

- [Scope](docs/scope.md): what the remake is, what it is not, phases, and open questions
- [Behavior spec](docs/spec/README.md): numbered requirements per system, for the engine team to build against
- [Information needed](docs/info-needed.md): the facts about the original game still to confirm, and the data tables to deliver
- [Battle rules](core/README.md): headless C++17 battle library with placeholder data, a terminal front end and tests
- [Event scripts](events/README.md): text format for cutscenes and field events, with a resumable interpreter and tests

This repo holds planning documents, engine-neutral libraries and tooling. The libraries ship with invented placeholder data only. It holds no script text, art, audio or other source material. Those stay in the rights holder's own storage until asset handling is agreed (see "Source material" in the scope).

## Build and test

Needs `clang++` and `make`.

```bash
make test
```
