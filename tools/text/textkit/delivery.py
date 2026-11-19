import json
import re
from dataclasses import dataclass, field
from pathlib import Path

from . import codes

REQUIRED = ("branches", "codes", "index", "map", "speaker", "text", "tree", "trigger")
OPTIONAL = ("context", "kind", "limit")
KINDS = ("dialogue", "system")
TREE_RE = re.compile(r"[A-Za-z0-9_.-]+\Z")


@dataclass
class Message:
	tree: str
	index: int
	speaker: str | None
	text: str
	codes: list[str]
	branches: list[str]
	map: str | None
	trigger: str | None
	kind: str = "dialogue"
	context: str | None = None
	limit: int | None = None
	source: str = ""
	where: str = field(default="", compare=False)

	@property
	def id(self):
		return f"{self.tree}:{self.index}"


class DeliveryError(Exception):
	def __init__(self, problems):
		super().__init__(f"{len(problems)} problem(s) in the delivery")
		self.problems = problems


def files_in(path):
	path = Path(path)
	if path.is_file():
		return [path]
	if path.is_dir():
		return sorted(p for p in path.rglob("*.json") if p.is_file())
	raise DeliveryError([f"{path}: no such file or directory"])


def load(path, registry):
	"""Read and strictly validate a delivery. Raises DeliveryError listing every problem."""
	root = Path(path)
	files = files_in(root)
	if not files:
		raise DeliveryError([f"{root}: no .json files"])
	problems = []
	messages = []
	for file in files:
		name = file.name if root.is_file() else str(file.relative_to(root))
		try:
			data = json.loads(file.read_text(encoding="utf-8"))
		except (UnicodeDecodeError, json.JSONDecodeError) as e:
			problems.append(f"{name}: not valid UTF-8 JSON: {e}")
			continue
		if not isinstance(data, list):
			problems.append(f"{name}: top level must be an array of message records")
			continue
		for i, record in enumerate(data):
			message = _record(record, f"{name}[{i}]", name, registry, problems)
			if message:
				messages.append(message)
	_cross_check(messages, problems)
	if problems:
		raise DeliveryError(problems)
	return messages


def _record(record, where, source, registry, problems):
	def bad(msg):
		problems.append(f"{where}: {msg}")

	if not isinstance(record, dict):
		bad("record must be an object")
		return None
	missing = [key for key in REQUIRED if key not in record]
	for key in missing:
		bad(f"missing field {key!r}")
	for key in sorted(set(record) - set(REQUIRED) - set(OPTIONAL)):
		bad(f"unknown field {key!r}")
	if missing:
		return None

	tree = record["tree"]
	index = record["index"]
	identified = True
	if not isinstance(tree, str) or not TREE_RE.match(tree):
		bad(f"tree must be a non-empty string of letters, digits, '_', '.' or '-', got {tree!r}")
		identified = False
	if type(index) is not int or index < 0:
		bad(f"index must be a non-negative integer, got {index!r}")
		identified = False
	speaker = record["speaker"]
	if speaker is not None and (not isinstance(speaker, str) or not speaker.strip()):
		bad(f"speaker must be null or a non-empty string, got {speaker!r}")
	for key in ("map", "trigger", "context"):
		value = record.get(key)
		if value is not None and (not isinstance(value, str) or not value.strip()):
			bad(f"{key} must be null or a non-empty string, got {value!r}")
	kind = record.get("kind", "dialogue")
	if kind not in KINDS:
		bad(f"kind must be one of {', '.join(KINDS)}, got {kind!r}")
	limit = record.get("limit")
	if limit is not None and (type(limit) is not int or limit <= 0):
		bad(f"limit must be null or a positive integer, got {limit!r}")

	text = record["text"]
	listed = record["codes"]
	if not isinstance(text, str):
		bad("text must be a string")
	elif not isinstance(listed, list) or not all(isinstance(c, str) for c in listed):
		bad("codes must be an array of strings such as \"br\" or \"wait:3\"")
	else:
		tokens, text_problems = codes.problems_in(text, registry)
		for p in text_problems:
			bad(p)
		for t in tokens:
			if codes.is_name(t):
				bad(f"name token {{{t.text}}} in delivered text; a delivery holds the shipped text and the rename pass writes name tokens")
		shown, _ = codes.visible(text, registry)
		if not shown.replace(codes.PLACEHOLDER, "x").strip():
			bad("empty text" if not text else "empty text (only control codes or whitespace)")
		found = [t.text for t in tokens]
		if found != listed:
			bad(f"codes {listed} do not match the codes in the text, in order: {found}")

	branches = record["branches"]
	targets = []
	if not isinstance(branches, list):
		bad("branches must be an array")
	else:
		for b in branches:
			if type(b) is int and b >= 0:
				targets.append(f"{tree}:{b}")
			elif isinstance(b, str) and re.fullmatch(r"[A-Za-z0-9_.-]+:\d+", b):
				targets.append(b)
			else:
				bad(f"branch target {b!r} must be a message index in this tree or \"tree:index\"")

	# A record with other problems still takes part in the duplicate and
	# branch checks, so one run reports everything.
	if not identified:
		return None
	return Message(
		tree=tree,
		index=index,
		speaker=speaker,
		text=text,
		codes=list(listed) if isinstance(listed, list) else [],
		branches=targets,
		map=record["map"],
		trigger=record["trigger"],
		kind=kind,
		context=record.get("context"),
		limit=limit,
		source=source,
		where=where,
	)


def _cross_check(messages, problems):
	seen = {}
	for m in messages:
		if m.id in seen:
			problems.append(f"{m.where}: duplicate message ID {m.id} (first at {seen[m.id].where})")
		else:
			seen[m.id] = m
	for m in messages:
		for target in m.branches:
			if target not in seen:
				problems.append(f"{m.where}: branch target {target} from {m.id} does not exist")
