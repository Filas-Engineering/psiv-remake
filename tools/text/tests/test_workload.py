import csv
import sqlite3

from support import REGISTRY, TempCase, entry, record

from textkit import db, delivery, l10n, workload

ENGLISH = [
	record("guide", 0, "{portrait:GUIDE}Welcome, {var:hero}. Take a Tonc."),
	record("guide", 1, "{portrait:GUIDE}Safe travels."),
	record("item", 0, "Tonc: heals a{br}little.", speaker=None, map=None, trigger=None, kind="system", limit=13),
]
JAPANESE = [
	record("guide", 0, "{portrait:GUIDE}ようこそ、{var:hero}。トニックをどうぞ。"),
	record("guide", 1, "{portrait:GUIDE}よい旅を。"),
	record("guide", 2, "{portrait:GUIDE}またね。"),
]
ENTRIES = [entry("I1", "Tonc", "Tonic")]


class ReferenceAndWorkloadTest(TempCase):
	def setUp(self):
		super().setUp()
		messages = delivery.load(self.write_delivery(ENGLISH, folder="en"), REGISTRY)
		ref = delivery.load(self.write_delivery(JAPANESE, folder="ja"), REGISTRY)
		self.ids = db.compare_ids(messages, ref)
		self.conn = db.create(self.tmp / "script.db", messages, REGISTRY, "en", ("ja", ref, "ja"))
		self.addCleanup(self.conn.close)
		with self.conn:
			self.conn.execute("UPDATE messages SET current = replace(current, 'Tonc', '{name:I1}')")

	def test_ids_present_in_one_language_only(self):
		self.assertEqual((["item:0"], ["guide:2"]), self.ids)

	def test_reference_is_stored_locked_and_exported_beside_the_source(self):
		self.assertEqual("ja", db.reference_lang(self.conn))
		with self.assertRaises(sqlite3.IntegrityError):
			with self.conn:
				self.conn.execute("UPDATE reference SET text = 'x'")
		l10n.export(self.conn, self.tmp / "table.csv")
		with (self.tmp / "table.csv").open(encoding="utf-8", newline="") as f:
			rows = {r["id"]: r for r in csv.DictReader(f)}
		self.assertEqual(JAPANESE[1]["text"], rows["guide:1"]["reference"])
		self.assertEqual("", rows["item:0"]["reference"])
		self.assertEqual("{portrait:GUIDE}Safe travels.", rows["guide:1"]["source"])

	def test_workload_per_language_pair(self):
		with self.conn:
			self.conn.execute("INSERT INTO translations (lang, message_id, text) VALUES ('fr', 'guide:1', 'Bon voyage.')")
		report = workload.collect(self.conn, ENTRIES, ["de"])
		languages = {r["lang"]: r for r in report["languages"]}
		# "Welcome, . Take a Tonic." + "Safe travels." + "Tonic: heals a" + "little."
		self.assertEqual({"lang": "en", "role": "source", "messages": 3, "words": 10, "characters": 58}, languages["en"])
		self.assertEqual(2, languages["ja"]["messages"])
		pairs = {(p["source"], p["target"]): p for p in report["pairs"]}
		self.assertEqual([("en", "de"), ("ja", "de"), ("en", "fr"), ("ja", "fr")], list(pairs))
		self.assertEqual((1, 2, 8), (pairs["en", "fr"]["done"], pairs["en", "fr"]["remaining"]["messages"], pairs["en", "fr"]["remaining"]["words"]))
		self.assertEqual(1, pairs["ja", "fr"]["remaining"]["messages"])
		self.assertEqual({"missing": ["item:0"], "only_in_reference": ["guide:2"]}, {k: report["reference"][k] for k in ("missing", "only_in_reference")})

	def test_token_and_placeholder_counts_and_limits_after_restoration(self):
		report = workload.collect(self.conn, ENTRIES)
		self.assertEqual((2, 1, 1), (report["with_name_tokens"], report["with_placeholders"], report["with_both"]))
		[over] = report["over_limit_restored"]
		self.assertEqual(
			{"message_id": "item:0", "line": 1, "length": 14, "shipped_length": 13, "limit": 13, "text": "Tonic: heals a"},
			over,
		)

	def test_unknown_token_ids_are_reported(self):
		self.assertEqual(["I1"], workload.collect(self.conn, [])["unknown_name_ids"])
