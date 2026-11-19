import unittest

from support import TempCase, entry

from textkit import glossary


def numbered(*entries):
	for i, e in enumerate(entries, 2):
		e.line = i
	return list(entries)


class GlossaryValidationTest(unittest.TestCase):
	def test_clean_glossary(self):
		errors, warnings = glossary.validate(numbered(
			entry("I1", "Tonc", "Tonic"),
			entry("I2", "Rope", "Rope", reason=""),
		))
		self.assertEqual([], errors)
		self.assertEqual([], warnings)

	def test_duplicate_shipped_names(self):
		errors, _ = glossary.validate(numbered(entry("I1", "Tonc", "Tonic"), entry("T1", "Tonc", "Tonic Wave", category="technique")))
		self.assertEqual(1, len(errors))
		self.assertIn("duplicate shipped name 'Tonc'", errors[0])

	def test_case_only_difference_is_ambiguous(self):
		errors, _ = glossary.validate(numbered(entry("I1", "Tonc", "Tonic"), entry("I2", "TONC", "Tonic Plus")))
		self.assertIn("differs only in case", errors[0])

	def test_two_shipped_names_restoring_to_one_name(self):
		errors, _ = glossary.validate(numbered(entry("I1", "Tonc", "Tonic"), entry("I2", "Tnc", "Tonic")))
		self.assertEqual(1, len(errors))
		self.assertIn("both restore to 'Tonic'", errors[0])

	def test_restored_name_that_is_another_shipped_name(self):
		errors, _ = glossary.validate(numbered(entry("I1", "Pot", "Brine Pot"), entry("I2", "Brine Pot", "Brine Potion")))
		self.assertEqual(1, len(errors))
		self.assertIn("is the shipped name of", errors[0])

	def test_structural_errors(self):
		errors, _ = glossary.validate(numbered(
			entry("", "Tonc ", "Tonic", category="weapon", reason=""),
			entry("I2", "Net{br}", "Net Throw"),
		))
		text = "\n".join(errors)
		self.assertIn("id is empty", text)
		self.assertIn("category 'weapon'", text)
		self.assertIn("leading, trailing or doubled spaces", text)
		self.assertIn("with no reason", text)
		self.assertIn("contains braces", text)

	def test_length_and_overlap_warnings(self):
		_, warnings = glossary.validate(numbered(
			entry("E1", "Reef Crwlr", "Reef Crwlr", category="enemy", reason=""),
			entry("E2", "Crwlr", "Crawler", category="enemy"),
			entry("I1", "Tonc Supreme", "Tonc Supreme Ultimate Elixir Draught", reason="x"),
			entry("I2", "Elixr", "Elx"),
		))
		text = "\n".join(warnings)
		self.assertIn("exactly at the cap and unchanged", text)
		self.assertIn("'Crwlr' is part of 'Reef Crwlr'", text)
		self.assertIn("over the shipped cap of 10", text)
		self.assertIn("over the layout check of 24", text)
		self.assertIn("shorter than shipped", text)


class GlossaryFileTest(TempCase):
	def test_round_trip_and_column_checks(self):
		path = self.tmp / "glossary.csv"
		glossary.init(path)
		glossary.save(path, [entry("I1", "Tonc", "Tonic")])
		[loaded] = glossary.load(path)
		self.assertEqual(("Tonc", "Tonic", 2), (loaded.shipped, loaded.restored, loaded.line))
		path.write_text("id,shipped,extra\n", encoding="utf-8")
		with self.assertRaises(glossary.GlossaryError) as caught:
			glossary.load(path)
		self.assertIn("missing columns", caught.exception.problems[0])
		self.assertIn("unknown columns extra", caught.exception.problems[1])


if __name__ == "__main__":
	unittest.main()
