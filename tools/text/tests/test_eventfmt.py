import unittest

from support import REGISTRY, TempCase, entry

from textkit import eventfmt, glossary

ENTRIES = [
	entry("ITM_001", "Tonc", "Tonic"),
	entry("ITM_004", "Rope", "Rope", reason=""),
	entry("SKL_001", "Net Thrw", "Net Throw", category="skill", reason=""),
	entry("MNU_001", "Dscrd", "Discard", category="menu"),
]


class EventGlossaryTest(TempCase):
	def setUp(self):
		super().setUp()
		self.path = self.tmp / "glossary.txt"

	def test_export_lists_renamed_names_in_the_event_format(self):
		self.assertEqual(2, eventfmt.write_glossary(self.path, ENTRIES, "g.csv"))
		lines = [line for line in self.path.read_text(encoding="utf-8").splitlines() if not line.startswith("#")]
		self.assertEqual(["ITM_001 | Tonc | Tonic | cut to fit", "SKL_001 | Net Thrw | Net Throw"], lines)

	def test_export_refuses_names_the_format_cannot_hold(self):
		with self.assertRaises(eventfmt.FormatError) as caught:
			eventfmt.write_glossary(self.path, [entry("I1", "Tonc", "Tonic", reason="cut | trimmed")], "g.csv")
		self.assertIn("contains '|'", caught.exception.problems[0])

	def test_round_trip_keeps_csv_only_fields(self):
		eventfmt.write_glossary(self.path, ENTRIES, "g.csv")
		merged, notes, problems = eventfmt.merge(ENTRIES, eventfmt.read_glossary(self.path))
		self.assertEqual(([], []), (notes, problems))
		self.assertEqual(
			[(e.id, e.category, e.shipped, e.restored, e.reason, e.signoff) for e in ENTRIES],
			[(e.id, e.category, e.shipped, e.restored, e.reason, e.signoff) for e in merged],
		)

	def test_import_updates_adds_and_clears_sign_off_on_a_name_change(self):
		self.path.write_text(
			"# edited on the event side\n"
			"ITM_001 | Tonc | Tonic Water | cut to fit\n"
			"SKL_001 | Net Thrw | Net Throw | cut by the skill table\n"
			"ENM_001 | Reef Crwlr | Reef Crawler | cut to the cap\n",
			encoding="utf-8",
		)
		merged, notes, problems = eventfmt.merge(ENTRIES, eventfmt.read_glossary(self.path), category="enemy")
		self.assertEqual([], problems)
		by_id = {e.id: e for e in merged}
		self.assertEqual(("Tonic Water", ""), (by_id["ITM_001"].restored, by_id["ITM_001"].signoff))
		self.assertEqual("cut by the skill table", by_id["SKL_001"].reason)
		self.assertEqual(("enemy", ""), (by_id["ENM_001"].category, by_id["ENM_001"].signoff))
		self.assertIn("MNU_001", by_id)
		self.assertIn("ITM_004", by_id)
		self.assertEqual(3, len(notes))
		self.assertEqual([], glossary.validate(merged)[0])

	def test_new_entries_need_a_category_and_missing_ones_are_refused(self):
		self.path.write_text("ENM_001 | Reef Crwlr | Reef Crawler | cut\n", encoding="utf-8")
		_, _, problems = eventfmt.merge(ENTRIES, eventfmt.read_glossary(self.path))
		self.assertEqual(3, len(problems))
		self.assertIn("ITM_001: renamed in the CSV glossary but not in the event glossary", problems[0])
		self.assertIn("ENM_001: new entry needs a category", problems[2])
		merged, notes, problems = eventfmt.merge(ENTRIES, eventfmt.read_glossary(self.path), "enemy", drop_missing=True)
		self.assertEqual([], problems)
		self.assertEqual(["ITM_004", "MNU_001", "ENM_001"], [e.id for e in merged])

	def test_bad_lines_are_reported(self):
		self.path.write_text("a | b\nITM-001 | Tonc | Tonic\nI1 | Tonc | Tonic\nI1 | Tnc | Tonic\nI2 |  | Tonic\n", encoding="utf-8")
		with self.assertRaises(eventfmt.FormatError) as caught:
			eventfmt.read_glossary(self.path)
		problems = caught.exception.problems
		self.assertEqual(4, len(problems))
		self.assertIn("got 2 field(s)", problems[0])
		self.assertIn("not a valid name", problems[1])
		self.assertIn("duplicate id I1", problems[2])
		self.assertIn("empty name", problems[3])


class StringTableTest(TempCase):
	def test_codes_line_breaks_and_backslashes(self):
		path = self.tmp / "en.strings"
		undefined = eventfmt.write_strings(path, "en", [
			("harbor_guide:1", "{portrait:FINCH}Bring a {name:ITM_001}.{br}Back\\slash."),
			("menu:0", "Discard"),
		], REGISTRY, "x.db", prefix="script.")
		lines = path.read_text(encoding="utf-8").splitlines()
		self.assertEqual("language en", lines[1])
		self.assertEqual("script.harbor_guide.1 = {portrait:FINCH}Bring a {name:ITM_001}.\\nBack\\\\slash.", lines[3])
		self.assertEqual("script.menu.0 = Discard", lines[4])
		self.assertEqual({"portrait": 1}, dict(undefined))

	def test_refuses_what_the_format_would_change(self):
		with self.assertRaises(eventfmt.FormatError) as caught:
			eventfmt.write_strings(self.tmp / "x.strings", "en", [
				("9th-pier:0", "Hello."),
				("guide:0", "Trailing. "),
				("guide:1", "Two\nlines"),
			], REGISTRY, "x.db")
		problems = caught.exception.problems
		self.assertIn("'9th-pier.0' is not a valid name", problems[0])
		self.assertIn("leading or trailing spaces", problems[1])
		self.assertIn("raw line break", problems[2])
		self.assertFalse((self.tmp / "x.strings").exists())


if __name__ == "__main__":
	unittest.main()
