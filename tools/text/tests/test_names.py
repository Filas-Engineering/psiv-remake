import unittest

from support import REGISTRY, TempCase, entry, record

from textkit import codes, delivery, glossary, l10n, names

BY_ID = {e.id: e for e in (entry("I1", "Tonc", "Tonic"), entry("I2", "Brine Pot", "Brine Potion"))}


class NameTokenTest(unittest.TestCase):
	def test_render_resolves_each_case_variant(self):
		text = "{portrait:GUIDE}{name_upper:I1}! A {name:I2}, or {name_lower:I1}.{br}Bye."
		self.assertEqual(("{portrait:GUIDE}TONC! A Brine Pot, or tonc.{br}Bye.", []), names.render(text, BY_ID, "shipped"))
		self.assertEqual(("{portrait:GUIDE}TONIC! A Brine Potion, or tonic.{br}Bye.", []), names.render(text, BY_ID, "restored"))

	def test_unknown_ids_are_reported_and_left_alone(self):
		self.assertEqual(("A {name:I7} and Tonic", ["I7"]), names.render("A {name:I7} and {name:I1}", BY_ID, "restored"))

	def test_registry_knows_name_tokens_and_checks_the_id(self):
		self.assertEqual([], codes.problems_in("A {name:I1} and {name_upper:harbor.lamp}", REGISTRY)[1])
		problems = codes.problems_in("{name} {name:1st} {name:a-b}", REGISTRY)[1]
		self.assertEqual(3, len(problems))
		self.assertIn("needs a glossary ID argument", problems[0])

	def test_name_tokens_show_as_placeholders_without_a_glossary(self):
		self.assertEqual(["A " + codes.PLACEHOLDER + "."], codes.lines("A {name:I1}.", REGISTRY))

	def test_a_registry_may_not_redefine_name_tokens(self):
		with self.assertRaises(ValueError) as caught:
			codes.Registry.from_json('{"name_upper": {"arg": "id"}}')
		self.assertIn("reserved for name tokens", str(caught.exception))

	def test_glossary_ids_must_be_usable_as_tokens(self):
		errors, _ = glossary.validate([entry("ITM-001", "Tonc", "Tonic"), entry("9lives", "Ctt", "Cat", category="enemy")])
		self.assertEqual(2, len(errors))
		self.assertIn("must be letters, digits, '_' and '.'", errors[0])
		self.assertEqual([], glossary.validate([entry("item.tonic_1", "Tonc", "Tonic")])[0])


class DeliveredNameTokenTest(TempCase):
	def test_a_delivery_may_not_contain_name_tokens(self):
		with self.assertRaises(delivery.DeliveryError) as caught:
			delivery.load(self.write_delivery([record("guide", 0, "Buy a {name:I1}.")]), REGISTRY)
		self.assertIn("name token {name:I1} in delivered text", caught.exception.problems[0])


class TranslatedNameTokenTest(unittest.TestCase):
	MESSAGE = {"current": "{portrait:GUIDE}{name_upper:I1}! Take a {name:I2}.", "char_limit": None}

	def problems(self, target):
		return l10n._target_problems(self.MESSAGE, target, REGISTRY)

	def test_a_translation_may_change_the_case_variant(self):
		self.assertEqual([], self.problems("{portrait:GUIDE}{name:I1} ! Prends un {name_lower:I2}."))

	def test_a_translation_may_not_drop_or_swap_a_name_token(self):
		self.assertIn("removed {name:I2}", self.problems("{portrait:GUIDE}{name:I1} ! Prends ça.")[0])
		self.assertIn("reordered", self.problems("{portrait:GUIDE}{name:I2} ! {name:I1}.")[0])


if __name__ == "__main__":
	unittest.main()
