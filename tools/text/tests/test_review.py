import csv
import sqlite3
import unittest

from support import TempCase, entry, record

from textkit import db, rename, review, sheet

ENTRIES = [
	entry("I1", "Tonc", "Tonic"),
	entry("I2", "Brine Pot", "Brine Potion"),
	entry("S1", "Net Thrw", "Net Throw", category="skill", signoff=""),
]
RECORDS = [
	record("guide", 0, "{portrait:GUIDE}Take a Tonc.{br}Or a Brine{br}Pot."),
	record("guide", 1, "{portrait:GUIDE}Welcome to the the harbor."),
	record("traveler", 0, "My Net Thrw is quick.", speaker="TRAVELER"),
]


def approve(path, decisions, edits=None):
	with open(path, encoding="utf-8", newline="") as f:
		rows = list(csv.DictReader(f))
	for r in rows:
		if r["change_id"] in decisions:
			r["approve"] = decisions[r["change_id"]]
		r.update((edits or {}).get(r["change_id"], {}))
	with open(path, "w", encoding="utf-8", newline="") as f:
		writer = csv.DictWriter(f, sheet.COLUMNS, lineterminator="\n")
		writer.writeheader()
		writer.writerows(rows)


class RenameReviewTest(TempCase):
	def setUp(self):
		super().setUp()
		self.conn = self.build_db(RECORDS)
		self.renames = self.tmp / "renames.csv"
		sheet.write(self.renames, rename.propose(self.conn, ENTRIES))

	def apply(self, sheets, name="renamed.db", conn=None):
		out, _ = review.apply(conn or self.conn, sheets, ENTRIES, self.tmp / name)
		self.addCleanup(out.close)
		return out

	def test_proposal_shows_each_line_before_and_after(self):
		rows = {r.change_id: r for r in sheet.read(self.renames)}
		self.assertEqual(["R0001", "R0002", "R0003"], sorted(rows))
		r2 = rows["R0002"]
		self.assertEqual(("Brine{br}Pot", "Brine{br}Potion"), (r2.old, r2.new))
		self.assertEqual(RECORDS[0]["text"], r2.before)
		self.assertEqual("{portrait:GUIDE}Take a Tonc.{br}Or a Brine{br}Potion.", r2.after)
		self.assertIn("not signed off", rows["R0003"].note)
		self.assertFalse(any(r.approved for r in rows.values()))

	def test_apply_keeps_original_and_verify_passes(self):
		approve(self.renames, {"R0001": "yes", "R0002": "yes", "R0003": "no"})
		out = self.apply([self.renames])
		message = out.execute("SELECT original, current FROM messages WHERE id = 'guide:0'").fetchone()
		self.assertEqual(RECORDS[0]["text"], message["original"])
		self.assertEqual("{portrait:GUIDE}Take a Tonic.{br}Or a Brine{br}Potion.", message["current"])
		self.assertEqual(RECORDS[0]["text"], self.conn.execute("SELECT current FROM messages WHERE id = 'guide:0'").fetchone()[0])

		verdict = review.verify(out, [self.renames], ENTRIES, self.write_delivery(RECORDS, folder="again"))
		self.assertEqual([], verdict.problems)
		self.assertEqual((1, 2), (verdict.changed, len(verdict.changes)))

	def test_verify_catches_a_direct_edit_to_current_text(self):
		approve(self.renames, {"R0001": "yes"})
		out = self.apply([self.renames])
		with out:
			out.execute("UPDATE messages SET current = 'Welcome to the harbor.' WHERE id = 'guide:1'")
		verdict = review.verify(out, [self.renames], ENTRIES)
		self.assertFalse(verdict.ok)
		self.assertEqual(1, len(verdict.problems))
		self.assertIn("guide:1: unapproved change", verdict.problems[0])

	def test_verify_catches_a_sheet_edited_after_apply(self):
		approve(self.renames, {"R0001": "yes"})
		out = self.apply([self.renames])
		approve(self.renames, {"R0002": "yes"})
		problems = review.verify(out, [self.renames], ENTRIES).problems
		self.assertIn("contents changed since apply", problems[0])
		self.assertTrue(any("guide:0: unapproved change" in p for p in problems))

	def test_verify_needs_every_applied_sheet(self):
		approve(self.renames, {"R0001": "yes"})
		out = self.apply([self.renames])
		problems = review.verify(out, [], ENTRIES).problems
		self.assertIn("records 1 applied sheet(s) but 0 were given", problems[0])

	def test_verify_catches_a_glossary_change_after_apply(self):
		approve(self.renames, {"R0001": "yes"})
		out = self.apply([self.renames])
		changed = [entry("I1", "Tonc", "Tonic Water"), *ENTRIES[1:]]
		problems = review.verify(out, [self.renames], changed).problems
		self.assertIn("not the glossary substitution 'Tonic Water'", problems[0])

	def test_the_locked_original_cannot_be_updated(self):
		with self.assertRaises(sqlite3.IntegrityError):
			with self.conn:
				self.conn.execute("UPDATE messages SET original = 'x' WHERE id = 'guide:0'")

	def test_verify_catches_a_delivery_mismatch(self):
		changed = [dict(r) for r in RECORDS]
		changed[1]["text"] = "{portrait:GUIDE}Welcome to the harbor."
		problems = review.verify(self.conn, [], ENTRIES, self.write_delivery(changed, folder="other")).problems
		self.assertEqual(["guide:1: locked original text differs from the delivery"], problems)

	def test_apply_refuses_forged_and_unsigned_renames(self):
		approve(self.renames, {"R0001": "yes", "R0003": "yes"}, {"R0001": {"new": "Elixir"}})
		with self.assertRaises(review.ApplyError) as caught:
			self.apply([self.renames])
		problems = "\n".join(caught.exception.problems)
		self.assertIn("R0001 (guide:0): new text 'Elixir' is not the glossary substitution 'Tonic'", problems)
		self.assertIn("R0003 (traveler:0): glossary entry S1 ('Net Thrw') has no sign-off", problems)
		self.assertFalse((self.tmp / "renamed.db").exists())

	def test_rename_row_must_be_a_real_mention(self):
		approve(self.renames, {"R0001": "yes"}, {"R0001": {"old": "Take", "start": "16", "new": "Tonic"}})
		with self.assertRaises(review.ApplyError) as caught:
			self.apply([self.renames])
		self.assertIn("'Take' at 16 is not a glossary mention", caught.exception.problems[0])

	def test_bad_approve_value_is_an_error_not_a_skip(self):
		approve(self.renames, {"R0001": "ye"})
		with self.assertRaises(sheet.SheetError):
			sheet.read(self.renames)

	def test_rename_then_typo_pass_verifies_in_order(self):
		approve(self.renames, {"R0001": "yes", "R0002": "yes"})
		renamed = self.apply([self.renames])
		typo_sheet = self.tmp / "typos.csv"
		message = renamed.execute("SELECT * FROM messages WHERE id = 'guide:1'").fetchone()
		start = message["current"].index("the the")
		row = sheet.proposal("T0001", "typo", message, start, "the the", "the", "doubled word")
		row.approved = True
		sheet.write(typo_sheet, [row])
		final = self.apply([typo_sheet], name="final.db", conn=renamed)

		verdict = review.verify(final, [self.renames, typo_sheet], ENTRIES)
		self.assertEqual([], verdict.problems)
		self.assertEqual(["rename", "rename", "typo"], [c.kind for c in verdict.changes])
		self.assertEqual("{portrait:GUIDE}Welcome to the harbor.", db.messages(final)[1]["current"])

		out_of_order = review.verify(final, [typo_sheet, self.renames], ENTRIES)
		self.assertFalse(out_of_order.ok)

	def test_typo_fix_may_not_touch_control_codes(self):
		message = self.conn.execute("SELECT * FROM messages WHERE id = 'guide:0'").fetchone()
		row = sheet.proposal("T0001", "typo", message, 0, "{portrait:GUIDE}Take", "Take", "stray portrait")
		row.approved = True
		typo_sheet = self.tmp / "typos.csv"
		sheet.write(typo_sheet, [row])
		with self.assertRaises(review.ApplyError) as caught:
			self.apply([typo_sheet])
		self.assertIn("may not add, remove or reorder control codes", caught.exception.problems[0])

	def test_overlapping_approved_rows_are_refused(self):
		message = self.conn.execute("SELECT * FROM messages WHERE id = 'guide:1'").fetchone()
		start = message["current"].index("the the")
		rows = [
			sheet.proposal("T0001", "typo", message, start, "the the", "the", "doubled word"),
			sheet.proposal("T0002", "typo", message, start + 4, "the harbor", "the port", "wording"),
		]
		for r in rows:
			r.approved = True
		typo_sheet = self.tmp / "typos.csv"
		sheet.write(typo_sheet, rows)
		with self.assertRaises(review.ApplyError) as caught:
			self.apply([typo_sheet])
		self.assertIn("overlaps T0001", caught.exception.problems[0])


if __name__ == "__main__":
	unittest.main()
