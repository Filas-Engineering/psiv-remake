import contextlib
import csv
import io
import unittest

from support import TempCase, record

from textkit import cli, glossary, outputs, sheet

RECORDS = [
	record("guide", 0, "{portrait:GUIDE}Buy a Tonc.{br}Or two.", branches=[1]),
	record("guide", 1, "{portrait:GUIDE}Safe  travels."),
]


class CliWorkflowTest(TempCase):
	def run_cli(self, *argv):
		out, err = io.StringIO(), io.StringIO()
		with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
			code = cli.main([str(a) for a in argv])
		return code, out.getvalue(), err.getvalue()

	def test_invalid_delivery_writes_no_database(self):
		bad = self.write_delivery([record("guide", 0, "Hi.", branches=[4])])
		code, _, err = self.run_cli("import", "--delivery", bad, "--db", self.tmp / "x.db")
		self.assertEqual(1, code)
		self.assertIn("branch target guide:4 from guide:0 does not exist", err)
		self.assertFalse((self.tmp / "x.db").exists())

	def test_refuses_unignored_paths_inside_the_repository(self):
		root = outputs.repo_root()
		if root is None:
			self.skipTest("not inside a git checkout")
		delivery_path = self.write_delivery(RECORDS)
		target = root / "tools" / "text" / "should-not-exist.db"
		code, _, err = self.run_cli("import", "--delivery", delivery_path, "--db", target)
		self.assertEqual(1, code)
		self.assertIn("not git-ignored", err)
		self.assertFalse(target.exists())

	def test_propose_approve_apply_verify(self):
		t = self.tmp
		self.assertEqual(0, self.run_cli("import", "--delivery", self.write_delivery(RECORDS), "--db", t / "a.db")[0])
		self.assertEqual(0, self.run_cli("glossary", "init", "--glossary", t / "g.csv")[0])
		code, _, err = self.run_cli(
			"glossary", "add", "--glossary", t / "g.csv", "--id", "I1", "--category", "item",
			"--shipped", "Tonc", "--restored", "Tonic", "--reason", "cut", "--signoff", "lead",
		)
		self.assertEqual(0, code, err)
		code, _, err = self.run_cli(
			"glossary", "add", "--glossary", t / "g.csv", "--id", "I2", "--category", "item",
			"--shipped", "Tnc", "--restored", "Tonic", "--reason", "cut",
		)
		self.assertEqual(1, code)
		self.assertIn("both restore to 'Tonic'", err)
		self.assertEqual(1, len(glossary.load(t / "g.csv")))

		self.assertEqual(0, self.run_cli("rename", "--db", t / "a.db", "--glossary", t / "g.csv", "--out", t / "r.csv")[0])
		self.assertEqual(1, self.run_cli("rename", "--db", t / "a.db", "--glossary", t / "g.csv", "--out", t / "r.csv")[0])
		rows = sheet.read(t / "r.csv")
		for r in rows:
			r.approved = True
		sheet.write(t / "r.csv", rows)
		self.assertEqual(0, self.run_cli("apply", "--db", t / "a.db", "--sheet", t / "r.csv", "--glossary", t / "g.csv", "--out", t / "b.db")[0])

		code, out, _ = self.run_cli(
			"verify", "--db", t / "b.db", "--glossary", t / "g.csv", "--sheet", t / "r.csv",
			"--change-list", t / "changes.csv",
		)
		self.assertEqual(0, code, out)
		self.assertIn("PASS", out)
		with (t / "changes.csv").open(encoding="utf-8", newline="") as f:
			[change] = list(csv.DictReader(f))
		self.assertEqual(("guide:0", "Tonc", "{name:I1}"), (change["message_id"], change["old"], change["new"]))

		code, out, _ = self.run_cli("verify", "--db", t / "b.db", "--glossary", t / "g.csv")
		self.assertEqual(1, code)
		self.assertIn("FAIL", out)

		code, out, err = self.run_cli("render", "--db", t / "b.db", "--glossary", t / "g.csv", "--tokens-only")
		self.assertEqual(0, code, err)
		self.assertEqual(
			"guide:0\n  shipped:  {portrait:GUIDE}Buy a Tonc.{br}Or two.\n  restored: {portrait:GUIDE}Buy a Tonic.{br}Or two.\n",
			out,
		)
		code, out, _ = self.run_cli("render", "--db", t / "b.db", "--glossary", t / "g.csv", "--names", "restored", "--id", "guide:1")
		self.assertEqual("guide:1\t{portrait:GUIDE}Safe  travels.\n", out)

		# The event glossary goes out and comes back as the same CSV glossary.
		self.assertEqual(0, self.run_cli("glossary", "export-events", "--glossary", t / "g.csv", "--out", t / "g.txt")[0])
		code, _, err = self.run_cli("glossary", "import-events", "--events", t / "g.txt", "--glossary", t / "g.csv", "--out", t / "g2.csv")
		self.assertEqual(0, code, err)
		self.assertEqual((t / "g.csv").read_text(encoding="utf-8"), (t / "g2.csv").read_text(encoding="utf-8"))

	def test_reference_delivery_and_string_tables(self):
		t = self.tmp
		japanese = [
			record("guide", 0, "{portrait:GUIDE}トニックをどうぞ。{br}二つでも。", branches=[1]),
			record("guide", 1, "{portrait:GUIDE}よい旅を。"),
			record("guide", 2, "{portrait:GUIDE}またね。"),
		]
		ja = self.write_delivery(japanese, folder="ja")
		code, _, err = self.run_cli("import", "--delivery", self.write_delivery(RECORDS), "--db", t / "a.db", "--reference", ja)
		self.assertEqual(2, code)
		self.assertIn("go together", err)
		code, out, err = self.run_cli(
			"import", "--delivery", self.write_delivery(RECORDS), "--db", t / "a.db", "--reference", ja, "--reference-lang", "ja",
		)
		self.assertEqual(0, code, err)
		self.assertIn("in the delivery, not in ja: 0", out)
		self.assertIn("in ja, not in the delivery: 1: guide:2", out)

		code, out, err = self.run_cli("export-strings", "--db", t / "a.db", "--lang", "ja", "--out", t / "ja.strings")
		self.assertEqual(0, code, err)
		self.assertIn("kept as written, not defined by the event string table format: portrait 2", out)
		self.assertIn("guide.0 = {portrait:GUIDE}トニックをどうぞ。\\n二つでも。", (t / "ja.strings").read_text(encoding="utf-8"))
		code, _, err = self.run_cli("export-strings", "--db", t / "a.db", "--lang", "fr", "--out", t / "fr.strings")
		self.assertEqual(1, code)
		self.assertIn("has no fr text", err)


if __name__ == "__main__":
	unittest.main()
