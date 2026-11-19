import re
from dataclasses import dataclass

from . import codes

# Hyphens and apostrophes count as part of a word on the left, so "Hi-Tonc"
# is not a mention of "Tonc". On the right only hyphens do, so the possessive
# "Tonc's" still is.
LEFT = r"(?<![\w'\-])"
RIGHT = r"(?![\w\-])"
GAP = r"[ \n]+"
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

	@property
	def note(self):
		notes = []
		if self.case != "exact":
			notes.append(f"case differs from shipped name ({self.case}); may be an ordinary word")
		if self.split:
			notes.append("name is split across a line break")
		if self.inner_codes:
			notes.append(f"control codes inside the name: {' '.join(self.inner_codes)}")
		return "; ".join(notes)


def compile_names(entries):
	compiled = []
	for entry in entries:
		words = entry.shipped.split(" ")
		pattern = LEFT + GAP.join(re.escape(w) for w in words) + RIGHT
		compiled.append((entry, re.compile(pattern, re.IGNORECASE)))
	return compiled


def find(text, registry, compiled):
	"""Every glossary mention in text, longest name first where they overlap."""
	shown, spans = codes.visible(text, registry)
	candidates = []
	for entry, regex in compiled:
		for m in regex.finditer(shown):
			candidates.append((m.start(), -(m.end() - m.start()), m.end(), entry))
	candidates.sort(key=lambda c: (c[0], c[1]))
	mentions = []
	taken_until = -1
	for vstart, _, vend, entry in candidates:
		if vstart < taken_until:
			continue
		taken_until = vend
		start, end = codes.raw_span(spans, vstart, vend)
		raw = text[start:end]
		tokens, _ = codes.scan(raw)
		matched = re.sub(r"[ \n]+", " ", shown[vstart:vend])
		mentions.append(Mention(
			start=start,
			end=end,
			text=raw,
			entry=entry,
			case=_case(matched, entry.shipped),
			split=any(registry.shows(t.name) == "\n" for t in tokens),
			inner_codes=[f"{{{t.text}}}" for t in tokens if registry.shows(t.name) != "\n"],
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
	"""The restored text for a mention, or (None, reason) if it needs a hand edit."""
	if mention.inner_codes:
		return None, "control code inside the name; cannot substitute automatically"
	restored = mention.entry.restored
	if mention.case == "upper":
		restored = restored.upper()
	elif mention.case == "lower":
		restored = restored.lower()
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
