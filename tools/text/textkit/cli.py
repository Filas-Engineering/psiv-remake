import argparse
import sys
from contextlib import closing
from pathlib import Path

from . import assets, codes, db, delivery, descriptions, diff, eventfmt, glossary, l10n, names, outputs, rename, review, sheet, stats, typos, workload

EXIT_OK = 0
EXIT_FAIL = 1
EXIT_USAGE = 2


class Failure(Exception):
	def __init__(self, message, details=()):
		super().__init__(message)
		self.details = list(details)


def emit(text, out=None, force=False, inputs=()):
	if out is None:
		sys.stdout.write(text)
		return
	path = outputs.check(out, force, inputs)
	path.write_text(text, encoding="utf-8")
	print(f"wrote {path}")


def load_registry(path):
	if path is None:
		return codes.Registry.default()
	try:
		return codes.Registry.load(path)
	except (OSError, ValueError) as e:
		raise Failure(f"cannot read code registry {path}: {e}")


def load_delivery(path, registry):
	try:
		return delivery.load(path, registry)
	except delivery.DeliveryError as e:
		raise Failure(f"{path}: delivery is invalid ({len(e.problems)} problem(s))", e.problems)


def load_glossary(path, strict=True):
	try:
		entries = glossary.load(path)
	except glossary.GlossaryError as e:
		raise Failure(f"{path}: cannot read glossary", e.problems)
	if strict:
		errors, _ = glossary.validate(entries)
		if errors:
			raise Failure(f"{path}: glossary has {len(errors)} error(s); run glossary validate", errors)
	return entries


def load_portraits(path):
	try:
		return assets.load_portraits(path)
	except assets.AssetError as e:
		raise Failure(str(e), e.problems)


def open_db(path):
	try:
		return db.connect(path)
	except db.DatabaseError as e:
		raise Failure(str(e))


def cmd_import(args):
	registry = load_registry(args.codes)
	messages = load_delivery(args.delivery, registry)
	print(f"{args.delivery}: {len(messages)} messages, valid")
	reference = None
	if args.reference:
		ref_messages = load_delivery(args.reference, registry)
		print(f"{args.reference}: {len(ref_messages)} {args.reference_lang} messages, valid")
		missing, extra = db.compare_ids(messages, ref_messages)
		print(f"  in the delivery, not in {args.reference_lang}: {len(missing)}{': ' + ', '.join(missing) if missing else ''}")
		print(f"  in {args.reference_lang}, not in the delivery: {len(extra)}{': ' + ', '.join(extra) if extra else ''}")
		reference = (args.reference_lang, ref_messages, Path(args.reference).name)
	if args.portraits:
		portraits = load_portraits(args.portraits)
		texts = [(m.id, m.text) for m in messages]
		if reference:
			texts += [(f"{args.reference_lang} {m.id}", m.text) for m in reference[1]]
		problems = assets.portrait_problems(texts, portraits)
		if problems:
			raise Failure(f"{len(problems)} portrait code(s) name no asset in {args.portraits} (TXT-021)", problems)
		print(f"{args.portraits}: every portrait code names one of {len(portraits)} assets")
	if args.check:
		return EXIT_OK
	path = outputs.check(args.db, args.force)
	with closing(db.create(path, messages, registry, Path(args.delivery).name, reference)):
		pass
	print(f"wrote {path}")
	return EXIT_OK


def cmd_stats(args):
	with closing(open_db(args.db)) as conn:
		report = stats.collect(conn, args.top)
	emit(stats.render(report, args.format), args.out, args.force, [args.db])
	return EXIT_OK


def cmd_glossary_init(args):
	path = outputs.check(args.glossary, args.force)
	glossary.init(path)
	print(f"wrote {path}")
	return EXIT_OK


def cmd_glossary_add(args):
	entries = load_glossary(args.glossary, strict=False)
	entries.append(glossary.Entry(
		id=args.id, category=args.category, shipped=args.shipped, restored=args.restored,
		reason=args.reason or "", pronunciation=args.pronunciation or "", signoff=args.signoff or "",
		line=len(entries) + 2,
	))
	errors, warnings = glossary.validate(entries)
	for w in warnings:
		print(f"warning: {w}")
	if errors:
		raise Failure("entry not added", errors)
	glossary.save(outputs.check_location(args.glossary), entries)
	print(f"added {args.id} to {args.glossary}")
	return EXIT_OK


def cmd_glossary_validate(args):
	entries = load_glossary(args.glossary, strict=False)
	errors, warnings = glossary.validate(entries, args.max_length)
	text = [f"{args.glossary}: {len(entries)} entries, {len(errors)} error(s), {len(warnings)} warning(s)"]
	text += [f"error: {e}" for e in errors]
	text += [f"warning: {w}" for w in warnings]
	emit("\n".join(text) + "\n", args.out, args.force, [args.glossary])
	return EXIT_FAIL if errors else EXIT_OK


def cmd_glossary_export_events(args):
	entries = load_glossary(args.glossary)
	path = outputs.check(args.out, args.force, [args.glossary])
	try:
		count = eventfmt.write_glossary(path, entries, Path(args.glossary).name)
	except eventfmt.FormatError as e:
		raise Failure("glossary not exported", e.problems)
	print(f"wrote {count} renamed names to {path} ({len(entries) - count} kept-as-shipped or menu entries are CSV-only)")
	unsigned = [e.id for e in eventfmt.shared(entries) if not e.signoff.strip()]
	if unsigned:
		print(f"warning: not signed off, so the rename pass will not write their tokens yet: {', '.join(unsigned)}")
	return EXIT_OK


def cmd_glossary_import_events(args):
	base = load_glossary(args.glossary, strict=False) if args.glossary else []
	try:
		imported = eventfmt.read_glossary(args.events)
	except eventfmt.FormatError as e:
		raise Failure(f"{args.events}: cannot read event glossary", e.problems)
	entries, notes, problems = eventfmt.merge(base, imported, args.category, args.drop_missing)
	if problems:
		raise Failure("glossary not written", problems)
	errors, warnings = glossary.validate(entries)
	if errors:
		raise Failure("glossary not written: the result would be invalid", errors)
	path = outputs.check(args.out, args.force, [args.events, *([args.glossary] if args.glossary else [])])
	glossary.save(path, entries)
	for n in notes:
		print(n)
	for w in warnings:
		print(f"warning: {w}")
	print(f"wrote {len(entries)} entries to {path}")
	return EXIT_OK


def cmd_glossary_mentions(args):
	entries = load_glossary(args.glossary)
	with closing(open_db(args.db)) as conn:
		if args.out is None:
			for message, m in rename.all_mentions(conn, entries):
				flags = ", ".join(f for f in (
					m.case if m.case != "exact" else "", "split" if m.split else "", "broken" if m.broken else "",
				) if f)
				print(f"{message['id']}\t{m.text!r}\t{m.entry.id} {m.entry.shipped} -> {m.entry.restored}"
					f"{'  [' + flags + ']' if flags else ''}")
			return EXIT_OK
		path = outputs.check(args.out, args.force, [args.db, args.glossary])
		count = rename.write_mentions(path, conn, entries)
	print(f"wrote {count} mentions to {path}")
	return EXIT_OK


def cmd_rename(args):
	entries = load_glossary(args.glossary)
	with closing(open_db(args.db)) as conn:
		rows = rename.propose(conn, entries)
	path = outputs.check(args.out, args.force, [args.db, args.glossary])
	sheet.write(path, rows)
	print(f"wrote {len(rows)} proposed renames to {path}; fill the approve column with yes or no")
	return EXIT_OK


def cmd_typos(args):
	words = None
	if args.words:
		try:
			words = typos.load_words(args.words)
		except OSError as e:
			raise Failure(f"cannot read word list: {e}")
	entries = load_glossary(args.glossary) if args.glossary else []
	with closing(open_db(args.db)) as conn:
		rows = typos.candidates(conn, words, entries)
	path = outputs.check(args.out, args.force, [args.db])
	sheet.write(path, rows)
	by_reason = {}
	for r in rows:
		by_reason[r.reason] = by_reason.get(r.reason, 0) + 1
	print(f"wrote {len(rows)} typo candidates to {path}")
	for reason, n in sorted(by_reason.items()):
		print(f"  {reason}: {n}")
	return EXIT_OK


def cmd_apply(args):
	entries = load_glossary(args.glossary)
	with closing(open_db(args.db)) as conn:
		path = outputs.check(args.out, args.force, [args.db, args.glossary, *args.sheet])
		try:
			out, applied = review.apply(conn, args.sheet, entries, path)
		except review.ApplyError as e:
			raise Failure(str(e), e.problems)
		out.close()
	print(f"applied {applied} approved change(s); wrote {path}")
	return EXIT_OK


def cmd_verify(args):
	entries = load_glossary(args.glossary)
	portraits = load_portraits(args.portraits) if args.portraits else None
	with closing(open_db(args.db)) as conn:
		verdict = review.verify(conn, args.sheet, entries, args.delivery)
		if portraits is not None:
			verdict.problems.extend(assets.portrait_problems(assets.db_texts(conn), portraits))
		lines = [
			f"verify {args.db}: {'PASS' if verdict.ok else 'FAIL'}",
			f"  messages {verdict.messages}  changed {verdict.changed}  approved changes {len(verdict.changes)}"
			f" (renames {sum(r.kind == 'rename' for r in verdict.changes)},"
			f" typos {sum(r.kind == 'typo' for r in verdict.changes)})",
		]
		if args.delivery:
			lines.append(f"  locked original checked against {args.delivery}")
		if portraits is not None:
			lines.append(f"  portrait codes checked against {args.portraits} ({len(portraits)} assets)")
		lines += [f"problem: {p}" for p in verdict.problems]
		emit("\n".join(lines) + "\n", args.out, args.force, [args.db, args.glossary, *args.sheet])
		if args.change_list:
			path = outputs.check(args.change_list, args.force, [args.db, args.glossary, *args.sheet])
			review.write_change_list(path, conn, verdict)
			print(f"wrote {path}")
	return EXIT_OK if verdict.ok else EXIT_FAIL


def cmd_render(args):
	entries = load_glossary(args.glossary)
	by_id = {e.id: e for e in entries}
	modes = names.MODES if args.names == "both" else (args.names,)
	wanted = set(args.id or [])
	out = []
	unknown = set()
	with closing(open_db(args.db)) as conn:
		messages = db.messages(conn)
		missing = sorted(wanted - {m["id"] for m in messages})
		if missing:
			raise Failure(f"no such message: {', '.join(missing)}")
		for m in messages:
			if wanted and m["id"] not in wanted:
				continue
			if args.tokens_only and not names.ids_in(m["current"]):
				continue
			views = []
			for mode in modes:
				text, bad = names.render(m["current"], by_id, mode)
				unknown.update(bad)
				views.append((mode, text))
			if len(views) == 1:
				out.append(f"{m['id']}\t{views[0][1]}")
			else:
				out.append(m["id"])
				out.extend(f"  {mode + ':':<10}{text}" for mode, text in views)
	if unknown:
		raise Failure(f"name tokens not in the glossary: {', '.join(sorted(unknown))}")
	emit("\n".join(out) + "\n", args.out, args.force, [args.db, args.glossary])
	return EXIT_OK


def cmd_workload(args):
	entries = load_glossary(args.glossary)
	for lang in args.target or []:
		if not l10n.LANG_RE.match(lang):
			raise Failure(f"target {lang!r} is not a language code such as fr or pt-BR")
	with closing(open_db(args.db)) as conn:
		report = workload.collect(conn, entries, args.target or [])
	emit(workload.render(report, args.format), args.out, args.force, [args.db, args.glossary])
	return EXIT_FAIL if report["unknown_name_ids"] else EXIT_OK


def cmd_glyphs(args):
	if not (args.db or args.strings or args.text):
		raise Failure("glyphs needs --db, --strings or --text")
	try:
		glyphs = assets.load_glyphs(args.glyphs)
		by_id = {e.id: e for e in load_glossary(args.glossary)} if args.glossary else None
		langs = set(args.lang or [])
		texts = []
		registry = codes.Registry.default()
		inputs = [args.glyphs, *(args.strings or []), *(args.text or [])]
		if args.db:
			inputs.append(args.db)
			with closing(open_db(args.db)) as conn:
				registry = db.registry(conn)
				texts += assets.db_texts(conn, langs)
		for path in args.strings or []:
			texts += assets.strings_texts(path, langs)
		for path in args.text or []:
			texts += assets.text_table_texts(path)
	except assets.AssetError as e:
		raise Failure(str(e), e.problems)
	report = assets.check_glyphs(glyphs, texts, registry, by_id)
	emit(assets.render_glyph_report(report), args.out, args.force, inputs)
	return EXIT_FAIL if report.missing else EXIT_OK


def cmd_descriptions(args):
	try:
		reports = descriptions.check(args.text, args.core)
	except descriptions.DescriptionError as e:
		raise Failure(str(e))
	emit(descriptions.render(reports), args.out, args.force, [*args.text, args.core])
	return EXIT_FAIL if any(r.unknown for r in reports) else EXIT_OK


def cmd_export_strings(args):
	with closing(open_db(args.db)) as conn:
		registry = db.registry(conn)
		messages = [m for m in db.messages(conn) if args.kind == "all" or m["kind"] == args.kind]
		if args.lang == workload.SOURCE_LANG:
			texts = {m["id"]: m["current"] for m in messages}
		elif args.lang == db.reference_lang(conn):
			texts = db.reference(conn)
		elif args.lang in db.translation_langs(conn):
			texts = db.translations(conn, args.lang)
		else:
			raise Failure(f"the database has no {args.lang} text (English, the reference language or a stored translation)")
		items = [(m["id"], texts[m["id"]]) for m in messages if m["id"] in texts]
		missing = [m["id"] for m in messages if m["id"] not in texts]
		path = outputs.check(args.out, args.force, [args.db])
		try:
			undefined = eventfmt.write_strings(path, args.lang, items, registry, Path(args.db).name, args.prefix)
		except eventfmt.FormatError as e:
			raise Failure("string table not written", e.problems)
	print(f"wrote {len(items)} {args.lang} strings to {path}")
	if missing:
		print(f"  no {args.lang} text for {len(missing)} message(s): {', '.join(missing)}")
	if undefined:
		listed = ", ".join(f"{name} {n}" for name, n in sorted(undefined.items()))
		print(f"  kept as written, not defined by the event string table format: {listed}")
	return EXIT_OK


def cmd_export(args):
	with closing(open_db(args.db)) as conn:
		path = outputs.check(args.out, args.force, [args.db])
		count = l10n.export(conn, path, args.kind)
	print(f"wrote {count} rows to {path}")
	return EXIT_OK


def cmd_import_translation(args):
	with closing(open_db(args.db)) as conn:
		report = l10n.check_translation(conn, args.lang, args.table, args.kind)
		ok = report.ok(args.allow_missing)
		emit(report.render(args.allow_missing), args.out, args.force, [args.db, args.table])
		if args.out_db:
			if not ok:
				raise Failure("translation not stored: the check failed")
			path = outputs.check(args.out_db, args.force, [args.db, args.table])
			l10n.store(conn, report, path).close()
			print(f"stored {len(report.translations)} {args.lang} strings in {path}")
	return EXIT_OK if ok else EXIT_FAIL


def cmd_diff(args):
	registry = load_registry(args.codes)
	old = load_delivery(args.old, registry)
	new = load_delivery(args.new, registry)
	result = diff.compare(old, new)
	emit(diff.render(result, args.old, args.new), args.out, args.force, [args.old, args.new])
	if args.csv:
		path = outputs.check(args.csv, args.force, [args.old, args.new])
		diff.write_csv(path, result)
		print(f"wrote {path}")
	return EXIT_OK


def parser():
	p = argparse.ArgumentParser(prog="cli.py", description="Dialogue and system text tooling.")
	sub = p.add_subparsers(dest="command", required=True)

	def command(name, func, help_text, parent=sub, writes=True):
		c = parent.add_parser(name, help=help_text, description=help_text)
		c.set_defaults(func=func)
		if writes:
			c.add_argument("--force", action="store_true", help="replace output files that already exist")
		return c

	c = command("import", cmd_import, "Validate a delivery and build the line database.")
	c.add_argument("--delivery", required=True, help="delivery JSON file or folder of JSON files")
	c.add_argument("--db", help="SQLite database to create")
	c.add_argument("--codes", help="JSON control code registry replacing the built-in one")
	c.add_argument("--check", action="store_true", help="validate only, write nothing")
	c.add_argument("--reference", help="second-language delivery keyed by the same message IDs (TXT-032)")
	c.add_argument("--reference-lang", help="language code of --reference, for example ja")
	c.add_argument("--portraits", help="portrait asset list; a portrait code naming no asset fails the import (TXT-021)")

	c = command("stats", cmd_stats, "Counts per kind, speaker, tree, map and control code.")
	c.add_argument("--db", required=True)
	c.add_argument("--out", help="report path (default: stdout)")
	c.add_argument("--format", choices=("text", "json"), default="text")
	c.add_argument("--top", type=int, default=10, help="how many longest lines to list")

	g = sub.add_parser("glossary", help="Manage and check the name glossary.")
	gsub = g.add_subparsers(dest="glossary_command", required=True)
	c = command("init", cmd_glossary_init, "Create an empty glossary CSV.", gsub)
	c.add_argument("--glossary", required=True)
	c = command("add", cmd_glossary_add, "Add one entry; refused if it makes the glossary invalid.", gsub, writes=False)
	c.add_argument("--glossary", required=True)
	c.add_argument("--id", required=True)
	c.add_argument("--category", required=True, choices=glossary.CATEGORIES)
	c.add_argument("--shipped", required=True)
	c.add_argument("--restored", required=True)
	c.add_argument("--reason")
	c.add_argument("--pronunciation")
	c.add_argument("--signoff")
	c = command("validate", cmd_glossary_validate, "Check the glossary for duplicates, ambiguity and lengths.", gsub)
	c.add_argument("--glossary", required=True)
	c.add_argument("--max-length", type=int, default=glossary.DEFAULT_MAX_LENGTH,
		help="warn when a restored name is longer than this")
	c.add_argument("--out", help="report path (default: stdout)")
	c = command("export-events", cmd_glossary_export_events, "Write the renamed names as an event script glossary.", gsub)
	c.add_argument("--glossary", required=True)
	c.add_argument("--out", required=True, help="event glossary file to write, for example glossary.txt")
	c = command("import-events", cmd_glossary_import_events, "Fold an event script glossary into a CSV glossary.", gsub)
	c.add_argument("--events", required=True, help="event glossary file to read")
	c.add_argument("--glossary", help="CSV glossary to start from (default: none)")
	c.add_argument("--out", required=True, help="CSV glossary to write")
	c.add_argument("--category", choices=glossary.CATEGORIES, help="category for entries the CSV glossary lacks")
	c.add_argument("--drop-missing", action="store_true",
		help="remove renamed CSV entries the event glossary no longer lists")
	c = command("mentions", cmd_glossary_mentions, "List every line that mentions a shipped name.", gsub)
	c.add_argument("--glossary", required=True)
	c.add_argument("--db", required=True)
	c.add_argument("--out", help="CSV path (default: a plain list on stdout)")

	c = command("rename", cmd_rename, "Propose the rename pass as a review sheet.")
	c.add_argument("--db", required=True)
	c.add_argument("--glossary", required=True)
	c.add_argument("--out", required=True, help="review sheet CSV to write")

	c = command("typos", cmd_typos, "List typo candidates as a review sheet.")
	c.add_argument("--db", required=True)
	c.add_argument("--out", required=True, help="review sheet CSV to write")
	c.add_argument("--words", help="word list, one word per line")
	c.add_argument("--glossary", help="glossary whose names count as known words")

	c = command("apply", cmd_apply, "Apply approved review sheets to a new copy of the database.")
	c.add_argument("--db", required=True)
	c.add_argument("--sheet", required=True, action="append", help="approved sheet; repeat to apply several in order")
	c.add_argument("--glossary", required=True)
	c.add_argument("--out", required=True, help="new database to write")

	c = command("verify", cmd_verify, "Prove the only changes from the locked text are approved ones.")
	c.add_argument("--db", required=True)
	c.add_argument("--glossary", required=True)
	c.add_argument("--sheet", action="append", default=[], help="every applied sheet, in the order applied")
	c.add_argument("--delivery", help="also check the locked original against this delivery")
	c.add_argument("--out", help="report path (default: stdout)")
	c.add_argument("--change-list", help="write the dialogue change list CSV here")
	c.add_argument("--portraits", help="also check every portrait code in every language against this asset list (TXT-021)")

	c = command("glyphs", cmd_glyphs, "Report characters the text uses that the font's glyph list lacks (TXT-033).")
	c.add_argument("--glyphs", required=True, help="the font's glyph list")
	c.add_argument("--db", help="line database: English, the reference language and stored translations")
	c.add_argument("--strings", action="append", help="event script string table; repeat for several")
	c.add_argument("--text", action="append", help='{"text": {...}} table as core and game use; repeat for several')
	c.add_argument("--lang", action="append", help="only this language; repeat for several (default: all)")
	c.add_argument("--glossary", help="also check shipped and restored names behind name tokens")
	c.add_argument("--out", help="report path (default: stdout)")

	c = command("descriptions", cmd_descriptions, "Count the descriptions of each kind in game text tables and report keys that describe nothing (UI-007).")
	c.add_argument("--text", required=True, action="append", help='{"text": {...}} game text table; repeat for several languages')
	c.add_argument("--core", required=True, help="the battle data directory whose ids descriptions name")
	c.add_argument("--out", help="report path (default: stdout)")

	c = command("render", cmd_render, "Print the text with name tokens shown as shipped or restored names.")
	c.add_argument("--db", required=True)
	c.add_argument("--glossary", required=True)
	c.add_argument("--names", choices=(*names.MODES, "both"), default="both")
	c.add_argument("--id", action="append", help="only this message; repeat for several")
	c.add_argument("--tokens-only", action="store_true", help="only messages that hold a name token")
	c.add_argument("--out", help="output path (default: stdout)")

	c = command("workload", cmd_workload, "Localization planning: counts per language pair, tokens, and limits after restoration.")
	c.add_argument("--db", required=True)
	c.add_argument("--glossary", required=True)
	c.add_argument("--target", action="append", help="target language to plan for; repeat for several")
	c.add_argument("--out", help="report path (default: stdout)")
	c.add_argument("--format", choices=("text", "json"), default="text")

	c = command("export-strings", cmd_export_strings, "Write one language as an event script string table.")
	c.add_argument("--db", required=True)
	c.add_argument("--out", required=True, help="string table to write, for example en.strings")
	c.add_argument("--lang", default="en", help="en (the current text), the reference language, or a stored translation")
	c.add_argument("--prefix", default="", help="put before every string ID, for example script.")
	c.add_argument("--kind", choices=("all", "dialogue", "system"), default="all")

	c = command("export", cmd_export, "Write the localization table keyed by message ID.")
	c.add_argument("--db", required=True)
	c.add_argument("--out", required=True)
	c.add_argument("--kind", choices=("all", "dialogue", "system"), default="all")

	c = command("import-translation", cmd_import_translation, "Check a translated table and optionally store it.")
	c.add_argument("--db", required=True)
	c.add_argument("--lang", required=True, help="language code, for example fr or pt-BR")
	c.add_argument("--table", required=True, help="translated CSV from export with the target column filled")
	c.add_argument("--kind", choices=("all", "dialogue", "system"), default="all")
	c.add_argument("--allow-missing", action="store_true", help="pass even if some IDs are missing or untranslated")
	c.add_argument("--out", help="report path (default: stdout)")
	c.add_argument("--out-db", help="write a database copy holding the translation (only if the check passes)")

	c = command("diff", cmd_diff, "Compare two deliveries: added, removed, changed and moved messages.")
	c.add_argument("--old", required=True)
	c.add_argument("--new", required=True)
	c.add_argument("--codes", help="JSON control code registry replacing the built-in one")
	c.add_argument("--out", help="report path (default: stdout)")
	c.add_argument("--csv", help="also write the diff as CSV")
	return p


def main(argv=None):
	args = parser().parse_args(argv)
	if args.command == "import" and not args.check and not args.db:
		print("error: import needs --db unless --check is given", file=sys.stderr)
		return EXIT_USAGE
	if args.command == "import" and bool(args.reference) != bool(args.reference_lang):
		print("error: --reference and --reference-lang go together", file=sys.stderr)
		return EXIT_USAGE
	if args.command == "import" and args.reference_lang and (
		not l10n.LANG_RE.match(args.reference_lang) or args.reference_lang == workload.SOURCE_LANG
	):
		print(f"error: --reference-lang {args.reference_lang!r} must be a language code other than en", file=sys.stderr)
		return EXIT_USAGE
	try:
		return args.func(args)
	except Failure as e:
		print(f"error: {e}", file=sys.stderr)
		for detail in e.details:
			print(f"  {detail}", file=sys.stderr)
		return EXIT_FAIL
	except (outputs.OutputError, db.DatabaseError, glossary.GlossaryError, sheet.SheetError, eventfmt.FormatError) as e:
		print(f"error: {e}", file=sys.stderr)
		for detail in getattr(e, "problems", []):
			print(f"  {detail}", file=sys.stderr)
		return EXIT_FAIL
