import unittest

from support import REGISTRY, TempCase, record

from textkit import delivery, diff


class DeliveryDiffTest(TempCase):
	def test_added_removed_changed_and_moved(self):
		old = [
			record("guide", 0, "Welcome.", branches=[1]),
			record("guide", 1, "The ferry leaves at dusk."),
			record("guide", 2, "Mind the gulls."),
			record("menu", 0, "Use", kind="system"),
			record("menu", 1, "Dscrd", kind="system"),
		]
		new = [
			record("guide", 0, "Welcome.", branches=[]),
			record("guide", 1, "The ferry leaves at sunset.", speaker="SAILOR"),
			record("guide", 3, "Safe travels."),
			record("menu", 0, "Use", kind="system"),
			record("menu", 2, "Dscrd", kind="system"),
		]
		a = delivery.load(self.write_delivery(old, folder="a"), REGISTRY)
		b = delivery.load(self.write_delivery(new, folder="b"), REGISTRY)
		result = diff.compare(a, b)

		self.assertEqual(1, result.same)
		self.assertEqual(["guide:3"], [m.id for m in result.added])
		self.assertEqual(["guide:2"], [m.id for m in result.removed])
		self.assertEqual([("menu:1", "menu:2")], [(x.id, y.id) for x, y in result.moved])
		changed = {m.id: [f[0] for f in fields] for m, _, fields in result.changed}
		self.assertEqual({"guide:0": ["branches"], "guide:1": ["text", "speaker"]}, changed)

		report = diff.render(result, "a", "b")
		self.assertIn("unchanged 1  changed 2  added 1  removed 1  moved 1", report)

	def test_repeated_text_is_not_reported_as_moved(self):
		old = [record("menu", 0, "Yes"), record("menu", 1, "Yes")]
		new = [record("menu", 2, "Yes"), record("menu", 3, "Yes")]
		result = diff.compare(
			delivery.load(self.write_delivery(old, folder="a"), REGISTRY),
			delivery.load(self.write_delivery(new, folder="b"), REGISTRY),
		)
		self.assertEqual(([], 2, 2), (result.moved, len(result.added), len(result.removed)))


if __name__ == "__main__":
	unittest.main()
