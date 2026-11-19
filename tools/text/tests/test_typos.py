import unittest

from support import TempCase, entry, record

from textkit import typos

WORDS = {w.casefold() for w in "a and two the to harbor is open boats ship sails at dawn hello there you bring for crossing ferry leaves i came market".split()}
ENTRIES = [entry("I1", "Tonc", "Tonic")]


class TypoCandidateTest(TempCase):
	def rows(self, texts, words=WORDS):
		conn = self.build_db([record("guide", i, t) for i, t in enumerate(texts)])
		return [(r.message_id, r.old, r.new, r.reason) for r in typos.candidates(conn, words, ENTRIES)]

	def test_doubled_words(self):
		self.assertEqual(
			[("guide:0", "the the", "the", "doubled word"), ("guide:1", "The{br}the", "", "doubled word")],
			self.rows(["Bring the the Tonc.", "Hello. The{br}the harbor."]),
		)

	def test_spacing_around_punctuation(self):
		self.assertEqual(
			[
				("guide:0", "  ", " ", "doubled space"),
				("guide:1", " ,", ",", "space before punctuation"),
				("guide:2", ".", ". ", "missing space after punctuation"),
				("guide:3", ",", ", ", "missing space after punctuation"),
			],
			self.rows(["The ship  sails.", "Hello , you.", "The ship sails.The ferry leaves.", "Hello,you."]),
		)

	def test_codes_and_ellipses_do_not_cause_spacing_noise(self):
		self.assertEqual([], self.rows(["Hello {wait:9} there...", "Hello{br}there!", "The ship ... sails.", "{var:hero}, hello."]))

	def test_unknown_words_respect_glossary_and_possessives(self):
		self.assertEqual(
			[("guide:0", "Saltmere", "", "not in word list")],
			self.rows(["The Tonc and the Tonic at the harbor's Saltmere."]),
		)

	def test_no_word_list_means_no_unknown_word_candidates(self):
		self.assertEqual([], self.rows(["Zyxx qwerty."], words=None))

	def test_inconsistent_spelling_flags_the_rarer_form(self):
		rows = self.rows([
			"I came to Saltmere.",
			"Saltmere is open.",
			"The ferry to Saltmeer leaves at dawn.",
			"Bring a Tnoc for the crossing.",
			"SALTMERE! TONC!",
		])
		inconsistent = [r for r in rows if r[3].startswith("inconsistent")]
		self.assertEqual(
			[
				("guide:2", "Saltmeer", "Saltmere", "inconsistent spelling of Saltmere (2 uses)"),
				("guide:3", "Tnoc", "Tonc", "inconsistent spelling of Tonc (glossary)"),
			],
			inconsistent,
		)

	def test_plurals_are_not_spelling_variants(self):
		rows = self.rows(["Two Toncs at the market.", "A Tonc."])
		self.assertFalse([r for r in rows if r[3].startswith("inconsistent")])


if __name__ == "__main__":
	unittest.main()
