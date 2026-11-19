# Acceptance

Automated acceptance checks from [`docs/spec/acceptance.md`](../docs/spec/acceptance.md), run against the `core/` battle library, the `events/` script library and the `tools/text/` command line. Every check in the spec gets one line of output; the ones tagged `core`, `events` or `tools` run here.

## Run

Needs `clang++`, GNU Make 3.81 or later and Python 3.

```
make clean && make test   # builds both libraries through their own Makefiles, then checks the spec and runs every check
make report               # writes report.md from docs/spec and the last run (runs the checks first if there is no run)
```

`make test` first runs the spec consistency checker (`scripts/check_spec.py`), then prints one line per check: ID, status and a short reason. The whole reason for each check is in `build/results.tsv`. It exits 1 when any check FAILs or the spec has a consistency error, and 0 otherwise.

Run one check by hand:

```
build/core_acceptance --run ACC-BAT-03 --root ..
build/events_acceptance --run ACC-EVT-04 --root ..
python3 scripts/check_spec.py --root ..
```

## Statuses

| Status | Meaning |
|---|---|
| PASS | The library implements the behavior and the check found nothing that contradicts the spec |
| FAIL | The behavior exists and contradicts the spec. The run exits 1 |
| NOT IMPLEMENTED | The library does not implement the behavior, or part of it, yet; the reason names what is missing. Also used when a check passes against the placeholder data but waits on a To measure baseline (`(baseline …)` in acceptance.md), because such a check cannot pass until that row is measured |
| NOT AUTOMATABLE | The check runs on `engine` or in a `playtest`, so this suite cannot run it |

A check that is automatable but has no test FAILs, and so does a test whose cited requirements differ from the check's Verifies column, so nothing is skipped silently.

## Layout

| Path | Contents |
|---|---|
| `core/acc_*.cpp` | One file per `core` check, linked with `../core/build/librules.a` |
| `events/acc_*.cpp` | One file per `events` check, linked with `../events/build/libevents.a` |
| `tools/acc_*.py` | One module per `tools` check; they drive `tools/text/cli.py` and `events/build/event_cli` and write only to a temporary folder outside the checkout |
| `core/support.*`, `events/support.*`, `tools/common.py` | Shared helpers |
| `harness/` | The check registry and the `--list` / `--run` entry point both C++ binaries share |
| `dump/` | `record_dump` (built three ways for ACC-BAT-15) and `save_probe` (the save payloads for ACC-SAV-01 to ACC-SAV-04: every truncation and every byte flipped with each bit mask) |
| `scripts/` | `spec.py` (spec parser), `check_spec.py`, `run.py` (the runner) and `report.py` |

## Add a check

1. Find the check's row in `docs/spec/acceptance.md`: its ID, its Verifies column and where it runs.
2. Add one file named after the ID:
	* `core`: `core/acc_bat_21.cpp` with `ACC_CHECK(acc_bat_21, "ACC-BAT-21", "BAT-xxx, BAT-yyy") { ... }`.
	* `events`: the same under `events/`.
	* `tools`: `tools/acc_txt_07.py` with `ID`, `VERIFIES` and `run(ctx)`.
3. Cite exactly the requirement IDs the Verifies column expands to, in the same order; the runner compares them.
4. Exercise the behavior through the library's public API or the tool's command line. Report with:
	* `t.expect(condition, "what contradicts the spec")` (FAIL when false),
	* `t.gap("what is not implemented, and the requirement ID")` (NOT IMPLEMENTED),
	* `t.note("what was verified")` (shown with PASS).
5. `make test`. The Makefile picks up new files by wildcard.

Any data or text a check adds must be original placeholder content in the style of `core/data/` and `events/samples/`.

## The spec checker

`scripts/check_spec.py` reads every file in `docs/spec/` and reports as errors: an ID defined twice, an ID prefix that does not match its file, a label outside Decided, Proposed (scope), Proposed (spec) and To measure, a To measure row without "To measure:", a method or a "Feeds" target, a source that is not a trace key in the README, a referenced ID, range end or link (with anchor) that does not resolve, an acceptance check that verifies something other than existing requirements, a baseline that is not a To measure row, and an automation summary that disagrees with the checks. It warns when a check verifies a requirement that a To measure row feeds without marking that row as its baseline.
