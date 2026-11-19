import argparse
import sys
from contextlib import closing
from pathlib import Path

from . import codes, db, delivery, diff, glossary, l10n, outputs, rename, review, sheet, stats, typos

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


def open_db(path):
	try:
		return db.connect(path)
	except db.DatabaseError as e:
		raise Failure(str(e))


def cmd_import(args):
	registry = load_registry(args.codes)
	messages = load_delivery(args.delivery, registry)
	print(f"{args.delivery}: {len(messages)} messages, valid")
	if args.check:
		return EXIT_OK
	path = outputs.check(args.db, args.force)
	with closing(db.create(path, messages, registry, Path(args.delivery).name)):
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


def cmd_glossary_mentions(args):
	entries = load_glossary(args.glossary)
	with closing(open_db(args.db)) as conn:
		if args.out is None:
			for message, m in rename.all_mentions(conn, entries):
				flags = ", ".join(f for f in (m.case if m.case != "exact" else "", "split" if m.split else "") if f)
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
	with closing(open_db(args.db)) as conn:
		verdict = review.verify(conn, args.sheet, entries, args.delivery)
		lines = [
			f"verify {args.db}: {'PASS' if verdict.ok else 'FAIL'}",
			f"  messages {verdict.messages}  changed {verdict.changed}  approved changes {len(verdict.changes)}"
			f" (renames {sum(r.kind == 'rename' for r in verdict.changes)},"
			f" typos {sum(r.kind == 'typo' for r in verdict.changes)})",
		]
		if args.delivery:
			lines.append(f"  locked original checked against {args.delivery}")
		lines += [f"problem: {p}" for p in verdict.problems]
		emit("\n".join(lines) + "\n", args.out, args.force, [args.db, args.glossary, *args.sheet])
		if args.change_list:
			path = outputs.check(args.change_list, args.force, [args.db, args.glossary, *args.sheet])
			review.write_change_list(path, conn, verdict)
			print(f"wrote {path}")
	return EXIT_OK if verdict.ok else EXIT_FAIL


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

	c = command("stats", cmd_stats, "Workload report: counts per speaker, tree, map and control code.")
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
	try:
		return args.func(args)
	except Failure as e:
		print(f"error: {e}", file=sys.stderr)
		for detail in e.details:
			print(f"  {detail}", file=sys.stderr)
		return EXIT_FAIL
	except (outputs.OutputError, db.DatabaseError, glossary.GlossaryError, sheet.SheetError) as e:
		print(f"error: {e}", file=sys.stderr)
		for detail in getattr(e, "problems", []):
			print(f"  {detail}", file=sys.stderr)
		return EXIT_FAIL
