import unittest

from support import REGISTRY, entry

from textkit import mentions

ENTRIES = [
	entry("I1", "Tonc", "Tonic"),
	entry("I2", "Brine Pot", "Brine Potion"),
	entry("I3", "Pot", "Pottery", category="item"),
	entry("E1", "Reef Crwlr", "Reef Crawler", category="enemy"),
	entry("T1", "Tide Call Wave", "Tidewave", category="technique"),
]
COMPILED = mentions.compile_names(ENTRIES)


def found(text):
	return [(m.text, m.entry.id, m.case, m.split) for m in mentions.find(text, REGISTRY, COMPILED)]


class MentionFindingTest(unittest.TestCase):
	def test_whole_words_only(self):
		self.assertEqual([], found("Toncs and Sitonc and Tonc-Plus and Hi-Tonc"))
		self.assertEqual([("Tonc", "I1", "exact", False)], found("Buy a Tonc."))

	def test_possessive_still_counts(self):
		self.assertEqual([("Tonc", "I1", "exact", False)], found("The Tonc's label"))

	def test_case_variants_are_found_and_classified(self):
		self.assertEqual(
			[("TONC", "I1", "upper", False), ("tonc", "I1", "lower", False), ("tONC", "I1", "other", False)],
			found("TONC! a tonc, tONC"),
		)

	def test_split_across_line_break(self):
		text = "A Brine{br}Pot helps. A Reef {br}Crwlr bites."
		self.assertEqual(
			[("Brine{br}Pot", "I2", "exact", True), ("Reef {br}Crwlr", "E1", "exact", True)],
			found(text),
		)

	def test_not_split_across_a_page_pause(self):
		self.assertEqual([("Pot", "I3", "exact", False)], found("Brine{page}Pot"))

	def test_longest_name_wins_where_names_overlap(self):
		self.assertEqual([("Brine Pot", "I2", "exact", False)], found("One Brine Pot."))

	def test_not_inside_code_arguments(self):
		self.assertEqual([], found("{var:Tonc}{portrait:Tonc} hi"))

	def test_raw_offsets_point_at_the_mention(self):
		text = "{portrait:GUIDE}{var:hero}, a Tonc!"
		[m] = mentions.find(text, REGISTRY, COMPILED)
		self.assertEqual("Tonc", text[m.start:m.end])

	def test_codes_inside_a_name_block_substitution(self):
		[m] = mentions.find("Brine{wait:5} Pot", REGISTRY, COMPILED)
		self.assertEqual(["{wait:5}"], m.inner_codes)
		self.assertIsNone(mentions.substitute(m)[0])


class SubstituteTest(unittest.TestCase):
	def sub(self, text):
		[m] = mentions.find(text, REGISTRY, COMPILED)
		return mentions.substitute(m)

	def test_case_follows_the_mention(self):
		self.assertEqual("TONIC", self.sub("TONC!")[0])
		self.assertEqual("tonic", self.sub("a tonc")[0])
		self.assertEqual("Tonic", self.sub("a Tonc")[0])

	def test_line_break_stays_at_the_same_word_gap(self):
		self.assertEqual(("Brine{br}Potion", ""), self.sub("Brine{br}Pot"))
		self.assertEqual(("Reef {br}Crawler", ""), self.sub("Reef {br}Crwlr"))

	def test_line_break_moves_when_the_restored_name_has_fewer_words(self):
		new, note = self.sub("Tide Call{br}Wave")
		self.assertEqual("{br}Tidewave", new)
		self.assertIn("moved before the name", note)


if __name__ == "__main__":
	unittest.main()
