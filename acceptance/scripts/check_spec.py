"""Spec consistency checker for docs/spec. Prints every problem; exits 1 when there are errors."""

import argparse
import re
import sys
from pathlib import Path

import spec as specmod


def check(spec):
	errors, warnings = [], []
	known = set(spec.requirements) | set(spec.checks)

	def at(file, line):
		return f"{file}:{line}"

	for identifier, file, line in spec.duplicates:
		errors.append(f"{at(file, line)}: {identifier} is defined more than once")

	for r in spec.rows:
		where = at(r.file, r.line)
		expected = spec.prefixes.get(r.file)
		if not specmod.ID_PATTERN.fullmatch(r.id):
			errors.append(f"{where}: '{r.id}' is not a valid requirement ID")
		elif expected and r.system != expected:
			errors.append(f"{where}: {r.id} lives in {r.file}, whose prefix is {expected}")
		if r.label not in specmod.LABELS and not r.text.startswith("Withdrawn"):
			errors.append(f"{where}: {r.id} has label '{r.label}', not one of {', '.join(specmod.LABELS)}")
		if r.label == "To measure":
			if not r.text.startswith("To measure:"):
				errors.append(f"{where}: {r.id} is labeled To measure but does not say what to measure")
			if "Method:" not in r.text:
				errors.append(f"{where}: {r.id} is a To measure row with no method")
			targets = specmod.feeds(r, spec.requirements)
			if not targets:
				errors.append(f"{where}: {r.id} is a To measure row with no 'Feeds' target")
		for key in (k.strip() for k in r.source.split(",")):
			if key and key not in spec.trace_keys:
				errors.append(f"{where}: {r.id} cites source '{key}', which is not a trace key in README.md")

	for name, text in spec.files.items():
		for number, line in enumerate(text.splitlines(), 1):
			for m in specmod.RANGE_PATTERN.finditer(line):
				for end in (m.group(1), m.group(2)):
					if end not in known:
						errors.append(f"{at(name, number)}: range end {end} is not defined")
				if specmod.prefix_of(m.group(1)) != specmod.prefix_of(m.group(2)) or specmod.number(m.group(1)) >= specmod.number(m.group(2)):
					errors.append(f"{at(name, number)}: '{m.group(0)}' is not an ascending range in one prefix")
			for m in specmod.ID_PATTERN.finditer(line):
				if m.group(0) not in known:
					errors.append(f"{at(name, number)}: {m.group(0)} is referenced but not defined")
			for m in specmod.LINK_PATTERN.finditer(line):
				target = m.group(1)
				if re.match(r"^[a-z]+://", target):
					continue
				path, _, anchor = target.partition("#")
				resolved = (spec.root / "docs" / "spec" / path).resolve() if path else spec.root / "docs" / "spec" / name
				if not resolved.exists():
					errors.append(f"{at(name, number)}: link to {target}, which does not exist")
				elif anchor and resolved.suffix == ".md" and anchor not in specmod.heading_anchors(resolved.read_text(encoding="utf-8")):
					errors.append(f"{at(name, number)}: link to {target}, but {resolved.name} has no heading '#{anchor}'")

	fed_by = {}
	for r in spec.requirements.values():
		if r.label == "To measure":
			for target in specmod.feeds(r, spec.requirements):
				fed_by.setdefault(target, []).append(r.id)

	for c in spec.checks.values():
		where = at(c.file, c.line)
		if not c.verifies:
			errors.append(f"{where}: {c.id} verifies no requirement")
		for target in specmod.expand(c.verifies_text, known):
			if target not in spec.requirements:
				errors.append(f"{where}: {c.id} verifies {target}, which is not a requirement")
		if c.runs_on not in specmod.RUNS_ON:
			errors.append(f"{where}: {c.id} runs on '{c.runs_on}', not one of {', '.join(specmod.RUNS_ON)}")
		for b in c.baselines:
			if b in spec.requirements and spec.requirements[b].label != "To measure":
				errors.append(f"{where}: {c.id} names {b} as a baseline, but {b} is not a To measure row")
		for target in c.verifies:
			r = spec.requirements.get(target)
			if r and r.label == "To measure":
				warnings.append(f"{where}: {c.id} verifies {target}, which is a To measure row, not a behavior")
			unmarked = [t for t in fed_by.get(target, []) if t not in c.baselines]
			if unmarked:
				warnings.append(f"{where}: {c.id} verifies {target}, which waits on {', '.join(unmarked)}, but does not mark it as a baseline")

	listed = {}
	for runs_on, text, line in spec.summary:
		for identifier in specmod.expand(text, spec.checks):
			if identifier in listed:
				errors.append(f"{at('acceptance.md', line)}: {identifier} appears twice in the automation summary")
			listed[identifier] = (runs_on, line)
		for m in specmod.RANGE_PATTERN.finditer(text):
			for end in (m.group(1), m.group(2)):
				if end not in spec.checks:
					errors.append(f"{at('acceptance.md', line)}: summary range end {end} is not a check")
	for c in spec.checks.values():
		if c.id not in listed:
			errors.append(f"{at(c.file, c.line)}: {c.id} is missing from the automation summary")
		elif listed[c.id][0] != c.runs_on:
			errors.append(f"{at('acceptance.md', listed[c.id][1])}: the summary lists {c.id} under {listed[c.id][0]}, but the check runs on {c.runs_on}")
	return errors, warnings


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--root", default="..")
	args = parser.parse_args()
	spec = specmod.load(Path(args.root))
	errors, warnings = check(spec)
	for e in errors:
		print(f"error: {e}")
	for w in warnings:
		print(f"warning: {w}")
	print(f"spec: {len(spec.requirements)} requirements, {len(spec.checks)} checks, {len(errors)} error(s), {len(warnings)} warning(s)")
	return 1 if errors else 0


if __name__ == "__main__":
	sys.exit(main())
