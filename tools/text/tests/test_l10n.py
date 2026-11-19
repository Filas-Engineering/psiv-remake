import csv
import unittest

from support import TempCase, record

from textkit import l10n

RECORDS = [
	record("guide", 0, "{portrait:GUIDE}Welcome, {var:hero}.{br}The ferry leaves soon.{page}Hurry!", context="First visit"),
	record("guide", 1, "{portrait:GUIDE}Safe travels.", branches=[0]),
	record("menu", 0, "Discard", speaker=None, map=None, trigger=None, kind="system", limit=10),
]
FRENCH = {
	"guide:0": "{portrait:GUIDE}Bienvenue, {var:hero}. Le ferry{br}part bientôt.{br}Vite !{page}Allez !",
	"guide:1": "{portrait:GUIDE}Bon voyage.",
	"menu:0": "Jeter",
}


class LocalizationRoundTripTest(TempCase):
	def setUp(self):
		super().setUp()
		self.conn = self.build_db(RECORDS)
		self.table = self.tmp / "table.csv"
		l10n.export(self.conn, self.table)
		with self.table.open(encoding="utf-8", newline="") as f:
			self.rows = list(csv.DictReader(f))

	def translated(self, targets, drop=(), extra=()):
		path = self.tmp / "fr.csv"
		with path.open("w", encoding="utf-8", newline="") as f:
			writer = csv.DictWriter(f, l10n.TABLE_COLUMNS, lineterminator="\n")
			writer.writeheader()
			for r in self.rows:
				if r["id"] not in drop:
					writer.writerow({**r, "target": targets.get(r["id"], "")})
			for i in extra:
				writer.writerow({**self.rows[0], "id": i, "target": "x"})
		return l10n.check_translation(self.conn, "fr", path)

	def test_export_rows_carry_context(self):
		first, second, menu = self.rows
		self.assertEqual(["guide:0", "guide:1", "menu:0"], [r["id"] for r in self.rows])
		self.assertEqual(("GUIDE", "harbor", "First visit"), (first["speaker"], first["map"], first["context"]))
		self.assertEqual("portrait:GUIDE var:hero page", first["codes"])
		self.assertEqual("guide:0", second["branches"])
		self.assertEqual(("10", "system", ""), (menu["char_limit"], menu["kind"], menu["target"]))

	def test_round_trip_stores_translation(self):
		report = self.translated(FRENCH)
		self.assertTrue(report.ok(), report.render())
		out = l10n.store(self.conn, report, self.tmp / "fr.db")
		self.addCleanup(out.close)
		stored = dict(out.execute("SELECT message_id, text FROM translations WHERE lang = 'fr'").fetchall())
		self.assertEqual(FRENCH, stored)

	def test_missing_extra_and_untranslated_ids(self):
		report = self.translated({"guide:0": FRENCH["guide:0"]}, drop=("menu:0",), extra=("ghost:1",))
		self.assertEqual((["menu:0"], ["ghost:1"], ["guide:1"]), (report.missing, report.extra, report.untranslated))
		self.assertFalse(report.ok(allow_missing=True))
		report = self.translated({"guide:0": FRENCH["guide:0"]}, drop=("menu:0",))
		self.assertFalse(report.ok())
		self.assertTrue(report.ok(allow_missing=True))

	def test_portrait_and_placeholder_must_be_kept_in_order(self):
		report = self.translated({
			**FRENCH,
			"guide:0": "{var:hero}, {portrait:GUIDE}bienvenue.{page}Allez !",
			"guide:1": "Bon voyage.",
		})
		self.assertEqual(2, len(report.errors))
		self.assertIn("guide:0: control codes and placeholders must be kept in order (reordered)", report.errors[0])
		self.assertIn("removed {portrait:GUIDE}", report.errors[1])

	def test_page_pauses_are_kept_but_line_breaks_may_move(self):
		report = self.translated({**FRENCH, "guide:0": "{portrait:GUIDE}Bienvenue, {var:hero}. Vite !"})
		self.assertIn("removed {page}", report.errors[0])

	def test_broken_codes_and_character_limit(self):
		report = self.translated({**FRENCH, "guide:1": "{portrait:GUIDE}Bon {voyage}.", "menu:0": "Se débarrasser"})
		self.assertIn("guide:1: unknown control code {voyage}", report.errors[0])
		self.assertIn("menu:0: line 'Se débarrasser' is 14 characters, over the limit of 10", report.errors[1])

	def test_stale_source_is_a_warning(self):
		with self.conn:
			self.conn.execute("UPDATE messages SET current = 'Throw away' WHERE id = 'menu:0'")
		report = self.translated(FRENCH)
		self.assertEqual(["menu:0: source text changed since export; check the translation"], report.warnings)


if __name__ == "__main__":
	unittest.main()
