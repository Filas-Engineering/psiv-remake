"""Name tokens: what the rename pass writes, and how the game resolves one."""

from . import codes

MODES = ("shipped", "restored")
TOKEN_FOR_CASE = {case: name for name, case in codes.NAME_CODES.items()}


def token(entry_id, case):
	"""The name token for a mention of the given case, or None if no token keeps that case."""
	code = TOKEN_FOR_CASE.get(case)
	return None if code is None else f"{{{code}:{entry_id}}}"


def with_case(name, case):
	if case == "upper":
		return name.upper()
	if case == "lower":
		return name.lower()
	return name


def render(text, by_id, mode):
	"""Resolve every name token to the shipped or restored name.

	by_id maps glossary ID to entry. Returns (text, unknown IDs); an unknown
	token is left as it is. Other control codes are kept.
	"""
	tokens, _ = codes.scan(text)
	out = []
	unknown = []
	pos = 0
	for t in tokens:
		if not codes.is_name(t):
			continue
		entry = by_id.get(t.arg)
		if entry is None:
			unknown.append(t.arg)
			continue
		out.append(text[pos:t.start])
		out.append(with_case(entry.shipped if mode == "shipped" else entry.restored, codes.NAME_CODES[t.name]))
		pos = t.end
	out.append(text[pos:])
	return "".join(out), unknown


def ids_in(text):
	return [t.arg for t in codes.scan(text)[0] if codes.is_name(t)]
