import json
import re
from collections import Counter, defaultdict

from . import codes, db

WORD_RE = re.compile(r"\S*[^\W_]\S*")


def measure(text, registry):
	"""Lines, words and characters as the player sees them. Placeholders count as nothing."""
	shown_lines = [
		line.replace(codes.PLACEHOLDER, "")
		for line in codes.lines(text, registry)
		if line.replace(codes.PLACEHOLDER, "x").strip()
	]
	words = sum(len(WORD_RE.findall(line)) for line in shown_lines)
	chars = sum(len(line) for line in shown_lines)
	return shown_lines, words, chars


def collect(conn, top=10):
	registry = db.registry(conn)
	empty = lambda: {"messages": 0, "lines": 0, "words": 0, "characters": 0}
	total = empty()
	groups = {name: defaultdict(empty) for name in ("kind", "speaker", "tree", "map")}
	code_names = Counter()
	code_tokens = Counter()
	longest = []
	for m in db.messages(conn):
		text = m["current"]
		shown_lines, words, chars = measure(text, registry)
		counts = {"messages": 1, "lines": len(shown_lines), "words": words, "characters": chars}
		keys = {
			"kind": m["kind"],
			"speaker": m["speaker"] or "(none)",
			"tree": m["tree"],
			"map": m["map"] or "(none)",
		}
		for k, v in counts.items():
			total[k] += v
			for group, key in keys.items():
				groups[group][key][k] += v
		for token in codes.scan(text)[0]:
			code_names[token.name] += 1
			code_tokens[token.text] += 1
		for i, line in enumerate(shown_lines):
			longest.append((len(line), m["id"], i + 1, line))
	longest.sort(key=lambda x: (-x[0], x[1], x[2]))
	return {
		"total": total,
		**{f"by_{g}": dict(sorted(v.items())) for g, v in groups.items()},
		"longest_lines": [
			{"message_id": mid, "line": n, "length": length, "text": line}
			for length, mid, n, line in longest[:top]
		],
		"codes_by_name": dict(sorted(code_names.items())),
		"codes_by_token": dict(sorted(code_tokens.items())),
	}


def render(report, fmt="text"):
	if fmt == "json":
		return json.dumps(report, indent="\t", ensure_ascii=False) + "\n"
	out = []
	t = report["total"]
	out.append("Totals")
	out.append(f"  messages {t['messages']}  lines {t['lines']}  words {t['words']}  characters {t['characters']}")
	for group in ("kind", "speaker", "tree", "map"):
		out.append("")
		out.append(f"By {group}")
		out.append(f"  {'':<24}{'messages':>10}{'lines':>8}{'words':>8}{'chars':>8}")
		for key, c in report[f"by_{group}"].items():
			out.append(f"  {key:<24}{c['messages']:>10}{c['lines']:>8}{c['words']:>8}{c['characters']:>8}")
	out.append("")
	out.append("Longest lines")
	for line in report["longest_lines"]:
		out.append(f"  {line['length']:>4}  {line['message_id']} line {line['line']}: {line['text']}")
	out.append("")
	out.append("Control codes by name")
	for name, n in report["codes_by_name"].items():
		out.append(f"  {name:<24}{n:>6}")
	out.append("")
	out.append("Control codes by token")
	for name, n in report["codes_by_token"].items():
		out.append(f"  {name:<24}{n:>6}")
	return "\n".join(out) + "\n"
