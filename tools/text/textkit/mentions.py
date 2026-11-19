import re
from dataclasses import dataclass

from . import codes, names

# Hyphens and apostrophes count as part of a word on the left, so "Hi-Tonc"
# is not a mention of "Tonc". On the right only hyphens do, so the possessive
# "Tonc's" still is.
LEFT = r"(?<![\w'\-])"
RIGHT = r"(?![\w\-])"
GAP = r"[ \n]+"
# Inside a word: a hyphen, a line break or page pause, or a hyphen then a
# break ("Ton-{br}c"). A name broken this way is found but never substituted.
INNER = r"(?:-?[\n\f]|-)?"
SEPARATOR_RE = re.compile(r"(?:\s|\{[a-z][a-z_]*\})+")


@dataclass
class Mention:
	start: int
	end: int
	text: str
	entry: object
	case: str
	split: bool
	inner_codes: list
	broken: bool = False

	@property
	def note(self):
		notes = []
		if self.case != "exact":
			notes.append(f"case differs from shipped name ({self.case}); may be an ordinary word")
		if self.broken:
			notes.append("name is broken inside a word by a hyphen, line break or page pause")
		elif self.split:
			notes.append("name is split across a line break")
		if self.inner_codes:
			notes.append(f"control codes inside the name: {' '.join(self.inner_codes)}")
		return "; ".join(notes)


def compile_names(entries):
	"""Per entry: (entry, search pattern, the pattern an unbroken mention fully matches)."""
	compiled = []
	for entry in entries:
		words = entry.shipped.split(" ")
		loose = GAP.join(INNER.join(re.escape(c) for c in w) for w in words)
		whole = GAP.join(re.escape(w) for w in words)
		compiled.append((entry, re.compile(LEFT + loose + RIGHT, re.IGNORECASE), re.compile(whole, re.IGNORECASE)))
	return compiled


def find(text, registry, compiled):
	"""Every glossary mention in text, longest name first where they overlap."""
	shown, spans = codes.visible(text, registry)
	candidates = []
	for entry, regex, whole in compiled:
		for m in regex.finditer(shown):
			candidates.append((m.start(), -(m.end() - m.start()), m.end(), entry, whole))
	candidates.sort(key=lambda c: (c[0], c[1]))
	mentions = []
	taken_until = -1
	for vstart, _, vend, entry, whole in candidates:
		if vstart < taken_until:
			continue
		taken_until = vend
		start, end = codes.raw_span(spans, vstart, vend)
		raw = text[start:end]
		tokens, _ = codes.scan(raw)
		matched = shown[vstart:vend]
		broken = not whole.fullmatch(matched)
		if broken:
			case = _case(re.sub(r"\W", "", matched), re.sub(r"\W", "", entry.shipped))
		else:
			case = _case(re.sub(r"[ \n]+", " ", matched), entry.shipped)
		mentions.append(Mention(
			start=start,
			end=end,
			text=raw,
			entry=entry,
			case=case,
			split=any(registry.shows(t.name) in ("\n", "\f") for t in tokens),
			inner_codes=[f"{{{t.text}}}" for t in tokens if registry.shows(t.name) not in ("\n", "\f")],
			broken=broken,
		))
	return mentions


def _case(matched, shipped):
	if matched == shipped:
		return "exact"
	if matched.isupper() and not shipped.isupper():
		return "upper"
	if matched.islower() and not shipped.islower():
		return "lower"
	return "other"


def substitute(mention):
	"""The replacement for a mention, or (None, reason) if it needs manual handling.

	Dialogue and system text get a name token, so the game can show either
	name (TXT-005, TXT-016). Menu labels other than names stay restored under
	the shipped-names setting, so a menu entry is written as literal text.
	"""
	if mention.broken:
		return None, "needs manual handling"
	if mention.inner_codes:
		return None, "needs manual handling"
	if mention.entry.category == "menu":
		return _literal(mention)
	new = names.token(mention.entry.id, mention.case)
	if new is None:
		return None, "mixed capitalization no name token can keep; needs manual handling"
	if not mention.split:
		return new, ""
	breaks = "".join(re.findall(r"\{[^}]*\}", "".join(SEPARATOR_RE.findall(mention.text))))
	return breaks + new, "line break moved before the name token; check layout"


def _literal(mention):
	restored = names.with_case(mention.entry.restored, mention.case)
	if not mention.split:
		return restored, ""

	separators = SEPARATOR_RE.findall(mention.text)
	at = next(i for i, s in enumerate(separators) if "{" in s)
	words = restored.split(" ")
	if len(words) == 1:
		return separators[at] + restored, "line break moved before the name; check layout"
	kept = min(at, len(words) - 2)
	out = words[0]
	for i, word in enumerate(words[1:]):
		out += (separators[at] if i == kept else " ") + word
	note = "" if kept == at and len(words) == len(separators) + 1 else f"line break kept after word {kept + 1}; check layout"
	return out, note
