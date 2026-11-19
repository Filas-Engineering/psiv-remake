import unittest

from support import REGISTRY, entry

from textkit import mentions

ENTRIES = [
	entry("I1", "Tonc", "Tonic"),
	entry("I2", "Brine Pot", "Brine Potion"),
	entry("I3", "Pot", "Pottery", category="item"),
	entry("E1", "Reef Crwlr", "Reef Crawler", category="enemy"),
	entry("T1", "Tide Call Wave", "Tidewave", category="technique"),
	entry("M1", "Dscrd Item", "Discard Item", category="menu"),
	entry("M2", "Eqp", "Equip", category="menu"),
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


class BrokenNameTest(unittest.TestCase):
	def test_name_broken_inside_a_word_is_found_for_manual_handling(self):
		for text, mention in (
			("Buy a Ton-{br}c now.", "Ton-{br}c"),
			("Buy a To{br}nc now.", "To{br}nc"),
			("Buy a To{page}nc now.", "To{page}nc"),
			("A Brine Po-{br}t, then.", "Brine Po-{br}t"),
		):
			[m] = mentions.find(text, REGISTRY, COMPILED)
			self.assertEqual((mention, True), (m.text, m.broken), text)
			self.assertIn("broken inside a word", m.note)
			new, why = mentions.substitute(m)
			self.assertIsNone(new)
			self.assertIn("manual handling", why)

	def test_broken_mention_keeps_its_case(self):
		[m] = mentions.find("TON-{br}C!", REGISTRY, COMPILED)
		self.assertEqual(("upper", True), (m.case, m.broken))

	def test_hyphen_outside_the_name_is_still_not_a_mention(self):
		self.assertEqual([], found("Hi-Tonc and Tonc-{br}Plus"))

	def test_unbroken_names_are_not_marked_broken(self):
		self.assertEqual([False, False], [m.broken for m in mentions.find("Tonc and Brine{br}Pot", REGISTRY, COMPILED)])


class SubstituteTest(unittest.TestCase):
	def sub(self, text):
		[m] = mentions.find(text, REGISTRY, COMPILED)
		return mentions.substitute(m)

	def test_names_become_tokens_whose_code_keeps_the_case(self):
		self.assertEqual(("{name_upper:I1}", ""), self.sub("TONC!"))
		self.assertEqual(("{name_lower:I1}", ""), self.sub("a tonc"))
		self.assertEqual(("{name:I1}", ""), self.sub("a Tonc"))

	def test_mixed_case_needs_manual_handling(self):
		new, why = self.sub("a tONC")
		self.assertIsNone(new)
		self.assertIn("mixed capitalization", why)

	def test_a_line_break_inside_a_name_moves_before_the_token(self):
		self.assertEqual("{br}{name:I2}", self.sub("Brine{br}Pot")[0])
		self.assertEqual("{br}{name:E1}", self.sub("Reef {br}Crwlr")[0])
		new, note = self.sub("Tide Call{br}Wave")
		self.assertEqual("{br}{name:T1}", new)
		self.assertIn("moved before the name token", note)

	def test_menu_labels_stay_literal_restored_text(self):
		self.assertEqual(("DISCARD ITEM", ""), self.sub("DSCRD ITEM"))
		self.assertEqual(("Discard{br}Item", ""), self.sub("Dscrd{br}Item"))
		new, note = self.sub("Dscrd {br}Item")
		self.assertEqual("Discard {br}Item", new)
		self.assertEqual("", note)
		self.assertEqual("Equip", self.sub("Eqp")[0])


if __name__ == "__main__":
	unittest.main()
