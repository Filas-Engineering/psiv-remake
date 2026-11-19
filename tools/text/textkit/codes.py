import json
import re
from dataclasses import dataclass

TOKEN_RE = re.compile(r"\{([a-z][a-z_]*)(?::([^{}]*))?\}")
ARG_RES = {
	"int": re.compile(r"\d+\Z"),
	"id": re.compile(r"[A-Za-z0-9_]+\Z"),
}
SHOWS = ("", "newline", "page", "placeholder")

# Stands in for a runtime value such as the hero's name. It is not a word
# character, so it never joins or splits words in mention and typo matching.
PLACEHOLDER = ""
VISIBLE = {"": "", "newline": "\n", "page": "\f", "placeholder": PLACEHOLDER}

# Name tokens are written by the rename pass, never delivered, and always
# known whatever the code registry says. The code name keeps the case of the
# mention it replaced; the argument is a glossary ID, which follows the event
# script name rules (events/FORMAT.md) so both tools share one glossary.
NAME_CODES = {"name": "exact", "name_upper": "upper", "name_lower": "lower"}
NAME_ID_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_.]*\Z")


@dataclass(frozen=True)
class CodeSpec:
	name: str
	arg: str
	shows: str = ""
	reflow: bool = False


DEFAULT_SPECS = (
	CodeSpec("br", "none", shows="newline", reflow=True),
	CodeSpec("end", "none"),
	CodeSpec("page", "none", shows="page"),
	CodeSpec("portrait", "id"),
	CodeSpec("var", "id", shows="placeholder"),
	CodeSpec("wait", "int"),
)


@dataclass(frozen=True)
class Token:
	start: int
	end: int
	name: str
	arg: str | None

	@property
	def text(self):
		return self.name if self.arg is None else f"{self.name}:{self.arg}"


class Registry:
	def __init__(self, specs):
		self.specs = {spec.name: spec for spec in specs}

	@classmethod
	def default(cls):
		return cls(DEFAULT_SPECS)

	@classmethod
	def from_json(cls, raw):
		data = json.loads(raw)
		if not isinstance(data, dict) or not data:
			raise ValueError("code registry must be a non-empty JSON object")
		specs = []
		for name, body in sorted(data.items()):
			if not re.fullmatch(r"[a-z][a-z_]*", name):
				raise ValueError(f"code name {name!r} must be lowercase letters and underscores")
			if name in NAME_CODES:
				raise ValueError(f"code name {name!r} is reserved for name tokens")
			if not isinstance(body, dict) or set(body) - {"arg", "shows", "reflow"}:
				raise ValueError(f"code {name!r}: expected an object with arg, shows, reflow")
			arg = body.get("arg", "none")
			shows = body.get("shows", "")
			reflow = body.get("reflow", False)
			if arg not in ("none", *ARG_RES):
				raise ValueError(f"code {name!r}: arg must be none, int or id")
			if shows not in SHOWS:
				raise ValueError(f"code {name!r}: shows must be one of {', '.join(repr(s) for s in SHOWS)}")
			if not isinstance(reflow, bool):
				raise ValueError(f"code {name!r}: reflow must be true or false")
			specs.append(CodeSpec(name, arg, shows, reflow))
		return cls(specs)

	@classmethod
	def load(cls, path):
		with open(path, encoding="utf-8") as f:
			return cls.from_json(f.read())

	def to_json(self):
		return json.dumps(
			{s.name: {"arg": s.arg, "shows": s.shows, "reflow": s.reflow} for s in self.specs.values()},
			sort_keys=True,
		)

	def check(self, token):
		if token.name in NAME_CODES:
			if token.arg is None or not NAME_ID_RE.match(token.arg):
				return f"name token {{{token.text}}} needs a glossary ID argument"
			return None
		spec = self.specs.get(token.name)
		if spec is None:
			return f"unknown control code {{{token.text}}}"
		if spec.arg == "none":
			if token.arg is not None:
				return f"control code {{{token.name}}} takes no argument, got {{{token.text}}}"
			return None
		if token.arg is None or not ARG_RES[spec.arg].match(token.arg):
			return f"control code {{{token.text}}} needs an {spec.arg} argument"
		return None

	def is_reflow(self, name):
		spec = self.specs.get(name)
		return spec is not None and spec.reflow

	def shows(self, name):
		if name in NAME_CODES:
			return PLACEHOLDER
		spec = self.specs.get(name)
		return VISIBLE[spec.shows] if spec else ""


def scan(text):
	"""Return the code tokens in text and any stray braces outside them."""
	tokens = []
	problems = []
	last = 0
	for m in TOKEN_RE.finditer(text):
		_stray(text, last, m.start(), problems)
		tokens.append(Token(m.start(), m.end(), m.group(1), m.group(2)))
		last = m.end()
	_stray(text, last, len(text), problems)
	return tokens, problems


def _stray(text, start, end, problems):
	for i in range(start, end):
		if text[i] in "{}":
			problems.append(f"stray {text[i]!r} at character {i} (braces are reserved for control codes)")


def problems_in(text, registry):
	tokens, problems = scan(text)
	for token in tokens:
		problem = registry.check(token)
		if problem:
			problems.append(problem)
	return tokens, problems


def visible(text, registry):
	"""Render text as the player reads it.

	Returns the rendered string and, for each rendered character, the raw
	(start, end) span it came from, so matches can be mapped back to raw text.
	"""
	tokens, _ = scan(text)
	out = []
	spans = []
	pos = 0
	for token in tokens:
		for i in range(pos, token.start):
			out.append(text[i])
			spans.append((i, i + 1))
		shown = registry.shows(token.name)
		if shown:
			out.append(shown)
			spans.append((token.start, token.end))
		pos = token.end
	for i in range(pos, len(text)):
		out.append(text[i])
		spans.append((i, i + 1))
	return "".join(out), spans


def raw_span(spans, vstart, vend):
	return spans[vstart][0], spans[vend - 1][1]


def mask(text):
	"""Replace each code token with NULs of the same length, keeping offsets."""
	tokens, _ = scan(text)
	chars = list(text)
	for token in tokens:
		for i in range(token.start, token.end):
			chars[i] = "\0"
	return "".join(chars)


def lines(text, registry):
	"""Split the rendered text into display lines (line breaks and page pauses)."""
	shown, _ = visible(text, registry)
	return re.split(r"[\n\f]", shown)


def strict_sequence(text, registry):
	"""Code tokens a translation must keep in the same order."""
	tokens, _ = scan(text)
	return [t.text for t in tokens if not registry.is_reflow(t.name)]


def is_name(token):
	return token.name in NAME_CODES
