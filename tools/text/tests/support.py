import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from textkit import codes, db, delivery, glossary  # noqa: E402

REGISTRY = codes.Registry.default()


def record(tree, index, text, **fields):
	tokens, _ = codes.scan(text)
	base = {
		"tree": tree,
		"index": index,
		"speaker": "GUIDE",
		"text": text,
		"codes": [t.text for t in tokens],
		"branches": [],
		"map": "harbor",
		"trigger": "talk_guide",
	}
	base.update(fields)
	return base


def entry(id, shipped, restored, category="item", reason="cut to fit", signoff="lead"):
	return glossary.Entry(id=id, category=category, shipped=shipped, restored=restored, reason=reason, signoff=signoff)


class TempCase(unittest.TestCase):
	def setUp(self):
		self._tmp = tempfile.TemporaryDirectory()
		self.tmp = Path(self._tmp.name)

	def tearDown(self):
		self._tmp.cleanup()

	def write_delivery(self, records, folder="delivery", name="script.json"):
		path = self.tmp / folder
		path.mkdir(exist_ok=True)
		(path / name).write_text(json.dumps(records), encoding="utf-8")
		return path

	def build_db(self, records, name="script.db"):
		messages = delivery.load(self.write_delivery(records, folder=f"delivery_{name}"), REGISTRY)
		conn = db.create(self.tmp / name, messages, REGISTRY, "test")
		self.addCleanup(conn.close)
		return conn
