import unittest

from support import REGISTRY, TempCase, record

from textkit import delivery


class DeliveryValidationTest(TempCase):
	def problems(self, records):
		with self.assertRaises(delivery.DeliveryError) as caught:
			delivery.load(self.write_delivery(records), REGISTRY)
		return caught.exception.problems

	def test_valid_delivery_normalizes_branch_targets(self):
		messages = delivery.load(self.write_delivery([
			record("guide", 0, "{portrait:GUIDE}Hello.{br}Choose.", branches=[1, "traveler:0"]),
			record("guide", 1, "Fine."),
			record("traveler", 0, "{var:hero}!"),
		]), REGISTRY)
		self.assertEqual(["guide:1", "traveler:0"], messages[0].branches)
		self.assertEqual("guide:0", messages[0].id)

	def test_duplicate_ids_are_reported_with_both_locations(self):
		problems = self.problems([record("guide", 0, "One."), record("guide", 0, "Two.")])
		self.assertEqual(1, len(problems))
		self.assertIn("duplicate message ID guide:0", problems[0])
		self.assertIn("first at script.json[0]", problems[0])

	def test_dangling_branch_targets(self):
		problems = self.problems([
			record("guide", 0, "Go.", branches=[5, "inn:2"]),
		])
		self.assertEqual(
			["script.json[0]: branch target guide:5 from guide:0 does not exist",
				"script.json[0]: branch target inn:2 from guide:0 does not exist"],
			problems,
		)

	def test_unknown_code_and_bad_argument(self):
		problems = self.problems([
			record("guide", 0, "Hi{shake}there."),
			record("guide", 1, "Wait{wait:soon}."),
			record("guide", 2, "Page{page:2}."),
		])
		self.assertIn("unknown control code {shake}", problems[0])
		self.assertIn("{wait:soon} needs an int argument", problems[1])
		self.assertIn("{page} takes no argument", problems[2])

	def test_empty_text_including_codes_only(self):
		problems = self.problems([
			record("guide", 0, ""),
			record("guide", 1, "{portrait:GUIDE}{br} {page}"),
		])
		self.assertTrue(problems[0].endswith("empty text"))
		self.assertIn("only control codes or whitespace", problems[1])

	def test_placeholder_alone_is_not_empty(self):
		delivery.load(self.write_delivery([record("battle", 0, "{var:target}", kind="system")]), REGISTRY)

	def test_codes_list_must_match_text_in_order(self):
		problems = self.problems([
			record("guide", 0, "A{br}B{page}C", codes=["page", "br"]),
		])
		self.assertIn("do not match the codes in the text, in order", problems[0])

	def test_stray_brace_unknown_and_missing_fields(self):
		bad = record("guide", 0, "Price {100.")
		bad["speeker"] = "GUIDE"
		missing = record("guide", 1, "Hi.")
		del missing["map"]
		problems = self.problems([bad, missing])
		self.assertIn("unknown field 'speeker'", " ".join(problems))
		self.assertIn("missing field 'map'", " ".join(problems))

	def test_all_problems_are_collected_in_one_run(self):
		negative = record("guide", 1, "Ok{zap}.")
		negative["index"] = -1
		problems = self.problems([record("guide", 0, "", kind="cutscene"), negative])
		self.assertEqual(4, len(problems))

	def test_custom_registry_replaces_builtin_codes(self):
		from textkit import codes
		registry = codes.Registry.from_json('{"nl": {"arg": "none", "shows": "newline", "reflow": true}}')
		path = self.write_delivery([record("guide", 0, "A{nl}B")])
		self.assertEqual(1, len(delivery.load(path, registry)))
		with self.assertRaises(delivery.DeliveryError):
			delivery.load(self.write_delivery([record("guide", 0, "A{br}B")], folder="other"), registry)


if __name__ == "__main__":
	unittest.main()
