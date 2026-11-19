"""Shared helpers for the tools checks: the text tool and event CLI, run from outside the checkout."""

import csv
import io
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


class Context:
	"""One run of one check: a failed expectation is FAIL, a gap is NOT IMPLEMENTED, notes go with PASS."""

	def __init__(self, root):
		self.root = Path(root).resolve()
		self.failures = []
		self.gaps = []
		self.notes = []
		self._temp = None

	def expect(self, ok, what):
		if not ok:
			self.failures.append(what)
		return ok

	def gap(self, what):
		self.gaps.append(what)

	def note(self, what):
		self.notes.append(what)

	@property
	def temp(self):
		# The text tool refuses outputs inside the checkout that git does not ignore.
		if self._temp is None:
			self._temp = Path(tempfile.mkdtemp(prefix="acceptance-"))
		return self._temp

	def close(self):
		if self._temp is not None:
			shutil.rmtree(self._temp, ignore_errors=True)

	@property
	def examples(self):
		return self.root / "tools" / "text" / "examples" / "placeholder"

	def text(self, *args):
		"""Runs the text tool's command line; returns (exit code, stdout + stderr)."""
		result = subprocess.run([sys.executable, "-I", str(self.root / "tools" / "text" / "cli.py"), *map(str, args)], capture_output=True, text=True, cwd=self.temp)
		return result.returncode, result.stdout + result.stderr

	def event_cli(self, *args):
		result = subprocess.run([str(self.root / "events" / "build" / "event_cli"), *map(str, args)], capture_output=True, text=True, cwd=self.root / "events")
		return result.returncode, result.stdout + result.stderr


def read_csv(path):
	with open(path, newline="", encoding="utf-8") as f:
		return list(csv.DictReader(f))


def write_csv(path, rows):
	out = io.StringIO()
	writer = csv.DictWriter(out, fieldnames=list(rows[0].keys()), lineterminator="\n")
	writer.writeheader()
	writer.writerows(rows)
	Path(path).write_text(out.getvalue(), encoding="utf-8")


def reviewed_renames(ctx, db, name="renames.csv"):
	"""Proposes the rename pass and approves every row whose glossary entry is signed off, as a reviewer would."""
	sheet = ctx.temp / name
	code, output = ctx.text("rename", "--db", db, "--glossary", ctx.examples / "glossary.csv", "--out", sheet)
	if not ctx.expect(code == 0, f"rename failed: {output.strip()}"):
		return None, []
	rows = read_csv(sheet)
	for row in rows:
		row["approve"] = "no" if "not signed off" in row["note"] else "yes"
	write_csv(sheet, rows)
	return sheet, rows


def imported(ctx, name="script.db"):
	db = ctx.temp / name
	code, output = ctx.text("import", "--delivery", ctx.examples / "delivery", "--db", db)
	ctx.expect(code == 0, f"import of the placeholder delivery failed: {output.strip()}")
	return db


# Words a game save API would use: the SAV-001 field groups' container, its writer and its loader.
GAME_SAVE_WORDS = ("SaveGame", "GameSave", "save_game", "load_game", "write_save", "read_save", "SaveFile", "save_file", "SAV-001", "SAV-010")


def game_save_api(ctx):
	"""Probes the libraries' public headers and the text tool's commands for a game save format.

	Returns (hits, probed): where a game save writer or reader was found, and what was looked at."""
	hits, probed = [], 0
	for folder in ("core/include", "events/include"):
		for path in sorted((ctx.root / folder).rglob("*.h")):
			probed += 1
			text = path.read_text(encoding="utf-8")
			hits += [f"{path.relative_to(ctx.root)}: {word}" for word in GAME_SAVE_WORDS if word in text]
	code, output = ctx.text("--help")
	probed += 1
	if code == 0 and any(word in output for word in ("save", "load-save")):
		hits.append("tools/text/cli.py has a save command")
	return hits, probed


def probe(ctx, mode):
	"""Runs build/save_probe; returns its "key value..." lines as a dict of integer lists, or None if it crashed."""
	result = subprocess.run([str(ctx.root / "acceptance" / "build" / "save_probe"), mode, str(ctx.root)], capture_output=True, text=True)
	if result.returncode != 0:
		ctx.expect(False, f"save_probe {mode} exited with {result.returncode}: {result.stderr.strip()[:200]}")
		return None
	return {parts[0]: [int(p) for p in parts[1:]] for parts in (line.split() for line in result.stdout.splitlines()) if parts}


def samples(ctx):
	return sorted(str(p) for p in (ctx.root / "events" / "samples").glob("*.ev"))
