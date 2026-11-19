# Text tools

Command-line tools for the script: import and validate a delivery, count the localization workload, manage the name glossary, propose and review the rename and typo passes, prove the locked text was changed only as approved, export localization tables, check translations, and compare two deliveries.

The rules come from the scope ("Script") and [dialogue-and-text.md](../../docs/spec/dialogue-and-text.md): dialogue is locked as shipped (TXT-002). It changes only where it mentions a renamed name, or to fix a plain typo, and every such change is listed (TXT-003, TXT-004).

Python 3.10 or later, standard library only.

```bash
python3 -I tools/text/cli.py <command> --help
python3 -I -m unittest discover -s tools/text/tests
```

## Where files go

The tools read the delivery and write every database, sheet and report to paths you give. Nothing has a default output path. Script text must never be committed, so the tools refuse any output path inside this checkout that git does not ignore, and refuse to overwrite a file unless you pass `--force`. Keep real deliveries and everything made from them in `source-material/` or another ignored folder.

`examples/placeholder/` holds a small invented script (a harbor town, a guide, a traveler, made-up item names) for trying the commands. It is not from any game.

## Input format

A delivery is one JSON file, or a folder of `.json` files read recursively in name order. Each file is an array of message records:

```json
{
	"tree": "harbor_guide",
	"index": 2,
	"speaker": "FINCH",
	"text": "{portrait:FINCH}Feeling seasick? A Brine{br}Pot settles the stomach.{page}The shop is by the boats.",
	"codes": ["portrait:FINCH", "br", "page"],
	"branches": [3, "traveler_osk:0"],
	"map": "saltmere_docks",
	"trigger": "talk_finch",
	"kind": "dialogue",
	"context": "Choice: ask about the shop",
	"limit": null
}
```

| Field | Required | Meaning |
|---|---|---|
| `tree` | yes | Message tree ID: letters, digits, `_`, `.`, `-` |
| `index` | yes | Message index in the tree. The message ID is `tree:index` and is the stable string ID everywhere |
| `speaker` | yes | Speaker or portrait ID, or `null` for narration and system text |
| `text` | yes | The text with control codes inline as `{name}` or `{name:argument}`. Braces are reserved for codes |
| `codes` | yes | The control codes in the text, in order, without braces. Must match the text exactly |
| `branches` | yes | Branch targets: an index in the same tree, or `"tree:index"` |
| `map`, `trigger` | yes | Where and what shows the message, or `null` |
| `kind` | no | `dialogue` (default) or `system` |
| `context` | no | A note for translators |
| `limit` | no | Character limit per display line, checked on translations |

Validation is strict and reports every problem in one run: missing or unknown fields, wrong types, duplicate IDs, dangling branch targets, unknown control codes or bad code arguments, stray braces, a `codes` list that does not match the text, and empty text (including text that is only codes and spaces).

### Control codes

Built in:

| Code | Argument | Shown as | Translators may move it |
|---|---|---|---|
| `br` | none | line break | yes |
| `page` | none | page pause | no |
| `portrait` | id | nothing | no |
| `var` | id | a runtime value such as a name | no |
| `wait` | integer | nothing | no |
| `end` | none | nothing | no |

The real code set is still to be measured (TXT-006). `import --codes FILE` replaces the built-in set with a JSON registry in the shape of `examples/placeholder/codes.json`. The database stores the registry, so later commands use the same one.

## Commands

### import

```bash
cli.py import --delivery DIR --db script.db [--codes codes.json] [--check] [--force]
```

Validates the delivery and builds an SQLite line database. Each message keeps `original`, the locked shipped text, and `current`, the working text. A trigger rejects any update to `original`. `--check` validates without writing.

### stats

```bash
cli.py stats --db script.db [--out report.txt] [--format text|json] [--top 10]
```

Message, line, word and character counts in total and per kind, speaker, tree and map; the longest display lines; counts per control code by name and by full token. Counts follow what the player reads: codes are not characters, a line is a non-empty display line between line breaks and page pauses, and runtime values count as nothing.

### glossary

```bash
cli.py glossary init --glossary glossary.csv
cli.py glossary add --glossary glossary.csv --id ITM-001 --category item --shipped Tonc --restored Tonic --reason "..." [--pronunciation ...] [--signoff ...]
cli.py glossary validate --glossary glossary.csv [--max-length 24] [--out report.txt]
cli.py glossary mentions --glossary glossary.csv --db script.db [--out mentions.csv]
```

The glossary is a CSV with columns `id, category, shipped, restored, reason, pronunciation, signoff`. Categories: enemy, item, location, menu, skill, technique. A name kept as shipped has `restored` equal to `shipped`.

`validate` errors (exit 1): empty or duplicate IDs, unknown category, empty names, names with surrounding or doubled spaces or braces, a rename with no reason, duplicate shipped names, shipped names that differ only in case, two shipped names restoring to the same name (the shipped-names setting could not map it back, TXT-016), and a restored name that is another entry's shipped name. Warnings: a shipped name inside a longer one, a shipped name over the 10-character cap, a name at the cap left unchanged, a restored name shorter than shipped, and a restored name over `--max-length`. `add` refuses an entry that would make the glossary invalid.

`mentions` lists every line that mentions a shipped name. Matching is whole-word and case-insensitive: "Tonc's" matches, "Toncs", "Hi-Tonc" and "Tonc-Plus" do not, and the case is reported (`exact`, `upper`, `lower`, `other`). A multi-word name matches across a line break (`Brine{br}Pot`), not across a page pause. Where names overlap the longest wins. Text inside codes is never matched.

### rename

```bash
cli.py rename --db script.db --glossary glossary.csv --out renames.csv
```

Writes the proposed rename pass as a review sheet: one row per mention of a renamed name, with the whole line before and after that one substitution. The case of the mention is kept (TONC becomes TONIC). A line break inside a name stays at the same word gap. Notes flag case mismatches (which may be an ordinary word), split names, entries with no sign-off, and names with a code inside, which cannot be substituted automatically.

### typos

```bash
cli.py typos --db script.db --out typos.csv [--words words.txt] [--glossary glossary.csv]
```

Writes typo candidates in the same sheet format: doubled words, doubled spaces, a space before punctuation, a missing space after punctuation, words not in the word list or the glossary (only with `--words`), and rare spellings one or two edits away from a more common name or a glossary name. Without a word list only glossary names are used for the spelling check. Words in all capitals are treated as shouting, not as a variant spelling. A suggested fix is filled in where one is obvious; the rest are left blank for the reviewer.

### apply

```bash
cli.py apply --db script.db --sheet renames.csv [--sheet more.csv] --glossary glossary.csv --out renamed.db
```

Applies the approved rows to a new copy of the database. The input database is not changed. Nothing is written if any approved row fails a check:

- The line must still be the one the sheet was proposed from (the `base` hash).
- `old` must be at `start`, and `new` must not be blank.
- A rename must be a real glossary mention, the glossary entry must be signed off, and `new` must be exactly the substitution the tool computes.
- A typo fix needs a reason and must keep the control codes the same.
- Approved rows in one line must not overlap.

The new database records each sheet's SHA-256 and the changes it made.

### verify

```bash
cli.py verify --db final.db --glossary glossary.csv --sheet renames.csv --sheet typos.csv [--delivery DIR] [--out report.txt] [--change-list changes.csv]
```

Proves that the current text is the locked original plus the approved rows of the given sheets and nothing else. It replays the sheets in order from `original`, with the same checks as apply, and compares the result with `current`. It fails (exit 1) on any unapproved edit, a sheet whose contents changed since it was applied, a missing or extra sheet, or a glossary change that no longer matches an applied rename. `--delivery` also checks `original` against the delivered text. `--change-list` writes the dialogue change list (TXT-003): message ID, change ID, kind, old, new, reason, and the full original and current line.

### export

```bash
cli.py export --db final.db --out table.csv [--kind all|dialogue|system]
```

Writes the localization table, one row per message keyed by message ID: `id, kind, speaker, map, trigger, context, branches, char_limit, codes, source, target`. `source` is the current English. `codes` lists the codes a translation must keep in order. `target` is empty, for the translator.

### import-translation

```bash
cli.py import-translation --db final.db --lang fr --table fr.csv [--kind ...] [--allow-missing] [--out report.txt] [--out-db final_fr.db]
```

Checks a translated table. It reports missing IDs, extra IDs, duplicate IDs and empty targets, and rejects any target that breaks a code, or adds, removes or reorders a portrait, page, wait or placeholder code (TXT-034). Line breaks may move. It also rejects a display line over the message's `limit`, and warns when the source changed since export. Missing and untranslated IDs fail the check unless `--allow-missing` is given. With `--out-db`, a passing translation is stored in a new copy of the database.

### diff

```bash
cli.py diff --old region_a/ --new region_b/ [--codes codes.json] [--out report.txt] [--csv diff.csv]
```

Compares two deliveries by message ID and reports added, removed and changed messages, with each changed field. A removed and an added message with the same unique text are reported as moved.

## Review workflow

1. `import` the delivery into a database.
2. `glossary validate` until there are no errors. Get each rename signed off.
3. `rename` to write the proposal sheet. A reviewer fills `approve` with `yes` or `no` for each row. Any other value is an error, not a skip.
4. `apply` the approved sheet to a new database.
5. `typos` on the new database. The reviewer fills `new` where it is blank, edits the `reason` if needed, and approves.
6. `apply` the typo sheet to another new database.
7. `verify` with every applied sheet in the order applied, the glossary and the delivery. Publish the change list it writes.

Each pass is proposed from the database the previous pass produced, because a sheet is tied to the exact text it was proposed from. To redo a pass, edit the sheet and apply it again to the earlier database.
