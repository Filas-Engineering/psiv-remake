#!/usr/bin/env python3
"""Checks that `--capabilities` lists as known every field the broken copy's check exercises: each name a problem line
gives a known field's bad value for (`field 'NAME' must ...`, `field 'NAME' reads ...`, `params.NAME: ...` but an
unknown param) must appear in the report. Prints the names missing and exits 1 when there are any.

Usage: check_capabilities.py REPORT.json CHECK_OUTPUT.txt
"""

import json
import re
import sys


def strings(node, into):
	if isinstance(node, dict):
		for key, value in node.items():
			into.add(key)
			strings(value, into)
	elif isinstance(node, list):
		for value in node:
			strings(value, into)
	elif isinstance(node, str):
		into.add(node)


def main():
	report, output = sys.argv[1:3]
	known = set()
	strings(json.load(open(report)), known)
	exercised = set()
	for line in open(output):
		for name in re.findall(r"(?<!unknown )field '([a-z_]+)'", line):
			exercised.add(name)
		match = re.search(r": params\.([a-z_0-9]+): (.*)$", line)
		if match and not match.group(2).startswith("unknown param") and "takes no params" not in match.group(2):
			exercised.add(match.group(1))
	missing = sorted(exercised - known)
	for name in missing:
		print(f"capabilities: '{name}' is exercised by the broken copy's check but not listed")
	print(f"capabilities: {len(exercised)} exercised fields, {len(missing)} missing")
	return 1 if missing else 0


if __name__ == "__main__":
	sys.exit(main())
