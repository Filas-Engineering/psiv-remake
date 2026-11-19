import sqlite3
from datetime import datetime, timezone
from pathlib import Path

from .codes import Registry

SCHEMA_VERSION = "2"

SCHEMA = """
CREATE TABLE meta (
	key TEXT PRIMARY KEY,
	value TEXT NOT NULL
);
CREATE TABLE messages (
	id TEXT PRIMARY KEY,
	seq INTEGER NOT NULL UNIQUE,
	tree TEXT NOT NULL,
	idx INTEGER NOT NULL,
	speaker TEXT,
	kind TEXT NOT NULL,
	map TEXT,
	trigger TEXT,
	context TEXT,
	char_limit INTEGER,
	source_file TEXT NOT NULL,
	original TEXT NOT NULL,
	current TEXT NOT NULL,
	UNIQUE (tree, idx)
);
CREATE TABLE branches (
	message_id TEXT NOT NULL REFERENCES messages (id),
	position INTEGER NOT NULL,
	target TEXT NOT NULL REFERENCES messages (id),
	PRIMARY KEY (message_id, position)
);
CREATE TABLE applied_sheets (
	seq INTEGER PRIMARY KEY,
	name TEXT NOT NULL,
	sha256 TEXT NOT NULL,
	approved_rows INTEGER NOT NULL,
	applied_at TEXT NOT NULL
);
CREATE TABLE changes (
	sheet_seq INTEGER NOT NULL REFERENCES applied_sheets (seq),
	change_id TEXT NOT NULL,
	message_id TEXT NOT NULL REFERENCES messages (id),
	kind TEXT NOT NULL,
	start INTEGER NOT NULL,
	old TEXT NOT NULL,
	new TEXT NOT NULL,
	reason TEXT NOT NULL,
	PRIMARY KEY (sheet_seq, change_id)
);
CREATE TABLE translations (
	lang TEXT NOT NULL,
	message_id TEXT NOT NULL REFERENCES messages (id),
	text TEXT NOT NULL,
	PRIMARY KEY (lang, message_id)
);
-- The second-language delivery (TXT-032), keyed by the same message IDs.
-- It may hold IDs the English delivery lacks, so there is no foreign key.
CREATE TABLE reference (
	message_id TEXT PRIMARY KEY,
	text TEXT NOT NULL,
	source_file TEXT NOT NULL
);
CREATE TRIGGER reference_locked BEFORE UPDATE ON reference
BEGIN
	SELECT RAISE(ABORT, 'reference text is locked');
END;
-- The shipped text is locked. Edits go to current, through approved sheets.
CREATE TRIGGER original_locked BEFORE UPDATE OF original ON messages
BEGIN
	SELECT RAISE(ABORT, 'original text is locked');
END;
"""


class DatabaseError(Exception):
	pass


def now():
	return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def create(path, messages, registry, delivery_name, reference=None):
	"""reference is None or (lang, messages, delivery name) for the second-language delivery."""
	path = Path(path)
	if path.exists():
		raise DatabaseError(f"{path} already exists (use --force to replace it)")
	conn = sqlite3.connect(path)
	conn.row_factory = sqlite3.Row
	try:
		with conn:
			conn.executescript(SCHEMA)
			conn.executemany(
				"INSERT INTO meta (key, value) VALUES (?, ?)",
				[
					("schema_version", SCHEMA_VERSION),
					("codes", registry.to_json()),
					("delivery", delivery_name),
					("imported_at", now()),
				],
			)
			for seq, m in enumerate(messages):
				conn.execute(
					"INSERT INTO messages (id, seq, tree, idx, speaker, kind, map, trigger, context,"
					" char_limit, source_file, original, current) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
					(m.id, seq, m.tree, m.index, m.speaker, m.kind, m.map, m.trigger, m.context,
						m.limit, m.source, m.text, m.text),
				)
			conn.executemany(
				"INSERT INTO branches (message_id, position, target) VALUES (?, ?, ?)",
				[(m.id, i, t) for m in messages for i, t in enumerate(m.branches)],
			)
			if reference is not None:
				lang, ref_messages, ref_name = reference
				conn.executemany(
					"INSERT INTO meta (key, value) VALUES (?, ?)",
					[("reference_lang", lang), ("reference_delivery", ref_name)],
				)
				conn.executemany(
					"INSERT INTO reference (message_id, text, source_file) VALUES (?, ?, ?)",
					[(m.id, m.text, m.source) for m in ref_messages],
				)
	except BaseException:
		conn.close()
		path.unlink(missing_ok=True)
		raise
	return conn


def connect(path):
	path = Path(path)
	if not path.is_file():
		raise DatabaseError(f"{path}: no such database")
	conn = sqlite3.connect(f"file:{path}?mode=rw", uri=True)
	conn.row_factory = sqlite3.Row
	try:
		version = conn.execute("SELECT value FROM meta WHERE key = 'schema_version'").fetchone()
	except sqlite3.DatabaseError as e:
		conn.close()
		raise DatabaseError(f"{path}: not a line database ({e})") from e
	if not version or version[0] != SCHEMA_VERSION:
		conn.close()
		raise DatabaseError(f"{path}: unsupported schema version (import the delivery again)")
	return conn


def copy(conn, path):
	path = Path(path)
	if path.exists():
		raise DatabaseError(f"{path} already exists (use --force to replace it)")
	dest = sqlite3.connect(path)
	conn.backup(dest)
	dest.row_factory = sqlite3.Row
	return dest


def registry(conn):
	return Registry.from_json(conn.execute("SELECT value FROM meta WHERE key = 'codes'").fetchone()[0])


def messages(conn):
	return conn.execute("SELECT * FROM messages ORDER BY seq").fetchall()


def branches(conn):
	out = {}
	for row in conn.execute("SELECT message_id, target FROM branches ORDER BY message_id, position"):
		out.setdefault(row[0], []).append(row[1])
	return out


def applied_sheets(conn):
	return conn.execute("SELECT * FROM applied_sheets ORDER BY seq").fetchall()


def reference_lang(conn):
	row = conn.execute("SELECT value FROM meta WHERE key = 'reference_lang'").fetchone()
	return row[0] if row else None


def reference(conn):
	return dict(conn.execute("SELECT message_id, text FROM reference ORDER BY message_id").fetchall())


def translation_langs(conn):
	return [r[0] for r in conn.execute("SELECT DISTINCT lang FROM translations ORDER BY lang")]


def translations(conn, lang):
	return dict(conn.execute("SELECT message_id, text FROM translations WHERE lang = ?", (lang,)).fetchall())


def compare_ids(messages, reference_messages):
	"""(IDs only in the English delivery, IDs only in the reference delivery), in delivery order."""
	ours = {m.id for m in messages}
	theirs = {m.id for m in reference_messages}
	return [m.id for m in messages if m.id not in theirs], [m.id for m in reference_messages if m.id not in ours]
