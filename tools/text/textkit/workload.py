import json

from . import codes, db, names, stats

SOURCE_LANG = "en"


def _line_lengths(text, registry):
	return [len(line.replace(codes.PLACEHOLDER, "")) for line in codes.lines(text, registry)]


def collect(conn, entries, targets=()):
	"""Localization workload. English is counted as the player reads it with restored names."""
	registry = db.registry(conn)
	by_id = {e.id: e for e in entries}
	messages = db.messages(conn)
	ids = [m["id"] for m in messages]
	unknown = set()

	def restored(text):
		out, missing = names.render(text, by_id, "restored")
		unknown.update(missing)
		return out

	texts = {SOURCE_LANG: ("source", {m["id"]: restored(m["current"]) for m in messages})}
	ref_lang = db.reference_lang(conn)
	reference = db.reference(conn)
	if ref_lang:
		texts[ref_lang] = ("reference", {i: reference[i] for i in ids if i in reference})
	stored = db.translation_langs(conn)
	for lang in stored:
		done = db.translations(conn, lang)
		texts.setdefault(lang, ("translation", {i: restored(done[i]) for i in ids if i in done}))

	def measure(lang, wanted):
		_, by = texts[lang]
		words = chars = 0
		for i in wanted:
			_, w, c = stats.measure(by[i], registry)
			words += w
			chars += c
		return {"messages": len(wanted), "words": words, "characters": chars}

	languages = [
		{"lang": lang, "role": role, **measure(lang, [i for i in ids if i in by])}
		for lang, (role, by) in texts.items()
	]

	pairs = []
	sources = [SOURCE_LANG] + ([ref_lang] if ref_lang else [])
	for target in sorted(set(targets) | set(stored)):
		done = texts[target][1] if target in texts else {}
		for source in sources:
			if source == target:
				continue
			have = [i for i in ids if i in texts[source][1]]
			remaining = [i for i in have if i not in done]
			pairs.append({
				"source": source,
				"target": target,
				"total": measure(source, have),
				"done": len(have) - len(remaining),
				"remaining": measure(source, remaining),
			})

	with_names = with_placeholders = with_both = 0
	for m in messages:
		tokens = codes.scan(m["current"])[0]
		has_name = any(codes.is_name(t) for t in tokens)
		has_placeholder = any(not codes.is_name(t) and registry.shows(t.name) == codes.PLACEHOLDER for t in tokens)
		with_names += has_name
		with_placeholders += has_placeholder
		with_both += has_name and has_placeholder

	over = []
	for m in messages:
		limit = m["char_limit"]
		if limit is None:
			continue
		shipped = _line_lengths(names.render(m["current"], by_id, "shipped")[0], registry)
		lines = codes.lines(texts[SOURCE_LANG][1][m["id"]], registry)
		for n, line in enumerate(lines):
			length = len(line.replace(codes.PLACEHOLDER, ""))
			if length > limit:
				over.append({
					"message_id": m["id"],
					"line": n + 1,
					"length": length,
					"shipped_length": shipped[n] if n < len(shipped) else None,
					"limit": limit,
					"text": line.replace(codes.PLACEHOLDER, ""),
				})

	return {
		"languages": languages,
		"pairs": pairs,
		"reference": {
			"lang": ref_lang,
			"missing": [i for i in ids if ref_lang and i not in reference],
			"only_in_reference": sorted(set(reference) - set(ids)),
		},
		"messages": len(messages),
		"with_name_tokens": with_names,
		"with_placeholders": with_placeholders,
		"with_both": with_both,
		"over_limit_restored": over,
		"unknown_name_ids": sorted(unknown),
	}


def render(report, fmt="text"):
	if fmt == "json":
		return json.dumps(report, indent="\t", ensure_ascii=False) + "\n"
	out = ["Languages (English with restored names)"]
	out.append(f"  {'lang':<8}{'role':<13}{'messages':>9}{'words':>8}{'chars':>8}")
	for lang in report["languages"]:
		out.append(f"  {lang['lang']:<8}{lang['role']:<13}{lang['messages']:>9}{lang['words']:>8}{lang['characters']:>8}")
	ref = report["reference"]
	if ref["lang"]:
		out.append(f"  missing from {ref['lang']}: {', '.join(ref['missing']) or 'none'}")
		out.append(f"  only in {ref['lang']}: {', '.join(ref['only_in_reference']) or 'none'}")
	out.append("")
	out.append("Language pairs (still to translate)")
	if not report["pairs"]:
		out.append("  no target language; pass --target or store a translation")
	for p in report["pairs"]:
		r = p["remaining"]
		out.append(
			f"  {p['source']} -> {p['target']}: {r['messages']} of {p['total']['messages']} messages,"
			f" {r['words']} words, {r['characters']} characters ({p['done']} done)"
		)
	out.append("")
	out.append("Name tokens and runtime placeholders")
	out.append(f"  messages {report['messages']}  with name tokens {report['with_name_tokens']}"
		f"  with placeholders {report['with_placeholders']}  with both {report['with_both']}")
	out.append("")
	out.append("Over the character limit with restored names")
	if not report["over_limit_restored"]:
		out.append("  none")
	for o in report["over_limit_restored"]:
		shipped = "?" if o["shipped_length"] is None else o["shipped_length"]
		out.append(f"  {o['message_id']} line {o['line']}: {o['length']} characters (shipped {shipped}), limit {o['limit']}: {o['text']}")
	if report["unknown_name_ids"]:
		out.append("")
		out.append(f"Name tokens not in the glossary: {', '.join(report['unknown_name_ids'])}")
	return "\n".join(out) + "\n"
