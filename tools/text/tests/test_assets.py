import contextlib
import io
import json

from support import REGISTRY, TempCase, entry, record

from textkit import assets, cli

RECORDS = [
	record("guide", 0, "{portrait:GUIDE}Buy a Tonc.{br}Or two."),
	record("guide", 1, "{portrait:GUEST}Café?"),
]


class AssetListTest(TempCase):
	def run_cli(self, *argv):
		out, err = io.StringIO(), io.StringIO()
		with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
			code = cli.main([str(a) for a in argv])
		return code, out.getvalue(), err.getvalue()

	def write(self, name, text):
		path = self.tmp / name
		path.write_text(text, encoding="utf-8")
		return path

	def test_a_portrait_code_naming_no_asset_fails_import_and_verify(self):
		delivery = self.write_delivery(RECORDS)
		portraits = self.write("portraits.txt", "# assets\nGUIDE\n")
		code, _, err = self.run_cli("import", "--delivery", delivery, "--db", self.tmp / "a.db", "--portraits", portraits)
		self.assertEqual(1, code)
		self.assertIn("guide:1: {portrait:GUEST} names no portrait asset", err)
		self.assertFalse((self.tmp / "a.db").exists())

		both = self.write("both.txt", "GUIDE\nGUEST\n")
		self.assertEqual(0, self.run_cli("import", "--delivery", delivery, "--db", self.tmp / "b.db", "--portraits", both)[0])
		glossary = self.tmp / "g.csv"
		self.assertEqual(0, self.run_cli("glossary", "init", "--glossary", glossary)[0])
		code, out, _ = self.run_cli("verify", "--db", self.tmp / "b.db", "--glossary", glossary, "--portraits", portraits)
		self.assertEqual(1, code)
		self.assertIn("problem: en guide:1: {portrait:GUEST} names no portrait asset", out)
		self.assertEqual(0, self.run_cli("verify", "--db", self.tmp / "b.db", "--glossary", glossary, "--portraits", both)[0])

	def test_portrait_lists_reject_bad_and_doubled_ids(self):
		with self.assertRaises(assets.AssetError) as caught:
			assets.load_portraits(self.write("bad.txt", "GUIDE\nno-dash\nGUIDE\n"))
		self.assertEqual(2, len(caught.exception.problems))

	def test_glyph_lists_take_ranges_and_characters(self):
		glyphs = assets.load_glyphs(self.write("font.txt", "# font\nU+0041-U+0043\nU+00E9\nxyz\n"))
		self.assertEqual(set("ABCéxyz"), glyphs)
		with self.assertRaises(assets.AssetError):
			assets.load_glyphs(self.write("bad.txt", "U+0043-U+0041\nU+zz\n"))

	def test_missing_glyphs_are_reported_with_their_first_use(self):
		texts = [("en a", "{portrait:X}Ab{br}{var:hero}c"), ("en b", "Cb {name:ITM}")]
		glyphs = set("Abc ")
		report = assets.check_glyphs(glyphs, texts, REGISTRY)
		self.assertEqual(["C"], report.missing)
		self.assertEqual("en b", report.first_use["C"])
		by_id = {"ITM": entry("ITM", "Tonc", "Tonic")}
		report = assets.check_glyphs(glyphs | set("C"), texts, REGISTRY, by_id)
		self.assertEqual(sorted(set("Toncic") - set("Abc C")), report.missing)

	def test_glyphs_command_reads_databases_string_tables_and_text_tables(self):
		delivery = self.write_delivery(RECORDS)
		self.assertEqual(0, self.run_cli("import", "--delivery", delivery, "--db", self.tmp / "s.db")[0])
		font = self.write("font.txt", "U+0020-U+007E\n")
		code, out, _ = self.run_cli("glyphs", "--glyphs", font, "--db", self.tmp / "s.db")
		self.assertEqual(1, code)
		self.assertIn("missing U+00E9", out)
		self.assertIn("first en guide:1", out)

		strings = self.write("ja.strings", "# table\nlanguage ja\ndock.hello = こんにちは\\nまた\n")
		table = self.write("en.json", json.dumps({"text": {"game.found": "Found {0} {1}."}}))
		code, out, _ = self.run_cli("glyphs", "--glyphs", font, "--strings", strings, "--text", table)
		self.assertEqual(1, code)
		self.assertIn("missing 7", out)
		self.assertEqual(0, self.run_cli("glyphs", "--glyphs", font, "--strings", strings, "--text", table, "--lang", "en")[0])
		code, out, _ = self.run_cli("glyphs", "--glyphs", self.write("all.txt", "U+0020-U+007E\nこんにちはまた\n"), "--strings", strings, "--text", table)
		self.assertEqual(0, code, out)
