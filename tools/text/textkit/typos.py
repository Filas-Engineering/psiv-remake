import re
from collections import Counter, defaultdict

from . import codes, db, sheet

WORD_RE = re.compile(r"[A-Za-z]+(?:'[A-Za-z]+)*")
DOUBLED_RE = re.compile(r"(?<![\w'])([A-Za-z]+(?:'[A-Za-z]+)*)([ \n]+)(\1)(?![\w'])", re.IGNORECASE)
DOUBLE_SPACE_RE = re.compile(r" {2,}")
SPACE_BEFORE_RE = re.compile(r"(?<=[^ \0])( +)([,;:!?]|\.(?!\.))")
MISSING_AFTER_RE = re.compile(r"[,;:!?](?=[A-Za-z])|(?<=[a-z])\.(?=[A-Z])")


def load_words(path):
	with open(path, encoding="utf-8") as f:
		return {line.strip().casefold() for line in f if line.strip() and not line.startswith("#")}


def glossary_words(entries):
	words = set()
	for e in entries:
		for name in (e.shipped, e.restored):
			words.update(w.casefold() for w in WORD_RE.findall(name))
	return words


def candidates(conn, words=None, entries=()):
	"""Typo candidates as review sheet rows. Nothing here is a decision."""
	registry = db.registry(conn)
	known = glossary_words(entries)
	found = []

	def add(message, start, old, new, reason, note=""):
		found.append((message["seq"], start, sheet.proposal("", "typo", message, start, old, new, reason, note)))

	messages = db.messages(conn)
	for message in messages:
		text = message["current"]
		shown, spans = codes.visible(text, registry)
		masked = codes.mask(text)

		for m in DOUBLED_RE.finditer(shown):
			start, end = codes.raw_span(spans, m.start(), m.end())
			if "\n" in m.group(2):
				add(message, start, text[start:end], "", "doubled word", "spans a line break; fix by hand")
			else:
				first_end = codes.raw_span(spans, m.start(1), m.end(1))[1]
				add(message, start, text[start:end], text[start:first_end], "doubled word")

		for m in DOUBLE_SPACE_RE.finditer(masked):
			add(message, m.start(), m.group(), " ", "doubled space")
		for m in SPACE_BEFORE_RE.finditer(masked):
			add(message, m.start(), m.group(), m.group(2), "space before punctuation")
		for m in MISSING_AFTER_RE.finditer(masked):
			add(message, m.start(), m.group(), m.group() + " ", "missing space after punctuation")

		if words is not None:
			for m in WORD_RE.finditer(shown):
				word = m.group()
				key = word.casefold()
				base = key[:-2] if key.endswith("'s") else key
				if key in words or base in words or key in known or base in known:
					continue
				start, end = codes.raw_span(spans, m.start(), m.end())
				add(message, start, text[start:end], "", "not in word list")

	for message, start, old, new, reason in _inconsistent_names(messages, registry, words, entries):
		add(message, start, old, new, reason)

	found.sort(key=lambda f: (f[0], f[1]))
	by_id = {e.id: e for e in entries}
	rows = []
	for i, (_, _, row) in enumerate(found, 1):
		row.change_id = f"T{i:04d}"
		if by_id:
			row.show_names(by_id)
		rows.append(row)
	return rows


def _inconsistent_names(messages, registry, words, entries):
	"""Rare spellings that sit one or two edits from a more common name."""
	occurrences = defaultdict(list)
	for message in messages:
		text = message["current"]
		shown, spans = codes.visible(text, registry)
		for m in WORD_RE.finditer(shown):
			word = m.group()
			if word.endswith("'s"):
				word = word[:-2]
			if not word[0].isupper() or len(word) < 4:
				continue
			if words is not None and word.casefold() in words:
				continue
			start, _ = codes.raw_span(spans, m.start(), m.start() + len(word))
			occurrences[word].append((message, start))

	canonical = {w for e in entries for name in (e.shipped, e.restored) for w in WORD_RE.findall(name)}
	counts = Counter({w: len(o) for w, o in occurrences.items()})
	for w in canonical:
		counts[w] += 1_000_000

	forms = sorted(occurrences)
	# Without a word list, capitalized common words ("Then", "They") would pair
	# up with each other, so only glossary names are references.
	references = canonical if words is None else set(forms) | canonical
	folded = {r.casefold() for r in references}
	for word in forms:
		if word.isupper() and word.casefold() in folded:
			continue
		best = None
		for other in references:
			if other == word or counts[other] <= counts[word]:
				continue
			if word in canonical and other in canonical:
				continue
			if _same_name(word, other) and (best is None or counts[other] > counts[best]):
				best = other
		if best is None:
			continue
		uses = "glossary" if best in canonical else f"{counts[best]} uses"
		for message, start in occurrences[word]:
			yield message, start, word, best, f"inconsistent spelling of {best} ({uses})"


def _same_name(a, b):
	la, lb = a.casefold(), b.casefold()
	if la == lb:
		# All capitals is how the script shouts, not a spelling.
		return not (a.isupper() or b.isupper())
	if la + "s" == lb or lb + "s" == la:
		return False
	limit = 1 if max(len(la), len(lb)) < 8 else 2
	return _distance(la, lb, limit) <= limit


def _distance(a, b, limit):
	if abs(len(a) - len(b)) > limit:
		return limit + 1
	# Optimal string alignment distance: edits plus swaps of adjacent letters.
	rows = [list(range(len(b) + 1))]
	for i in range(1, len(a) + 1):
		row = [i] + [0] * len(b)
		for j in range(1, len(b) + 1):
			cost = a[i - 1] != b[j - 1]
			row[j] = min(rows[-1][j] + 1, row[j - 1] + 1, rows[-1][j - 1] + cost)
			if i > 1 and j > 1 and a[i - 1] == b[j - 2] and a[i - 2] == b[j - 1]:
				row[j] = min(row[j], rows[-2][j - 2] + 1)
		if min(row) > limit:
			return limit + 1
		rows.append(row)
	return rows[-1][-1]
