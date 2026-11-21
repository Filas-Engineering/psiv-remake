import contextlib
import io
import json
from pathlib import Path

from support import TempCase

from textkit import cli, descriptions

ROOT = Path(__file__).resolve().parents[3]


class DescriptionsTest(TempCase):
	def run_cli(self, *argv):
		out, err = io.StringIO(), io.StringIO()
		with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
			code = cli.main([str(a) for a in argv])
		return code, out.getvalue(), err.getvalue()

	def core(self):
		core = self.tmp / "core"
		core.mkdir()
		lists = {"items.json": ("items", ["tonic", "gel"]), "equipment.json": ("equipment", ["cap"]), "abilities.json": ("abilities", ["spark"]),
			"statuses.json": ("statuses", ["sleep"]), "enemies.json": ("enemies", ["mite"]), "difficulties.json": ("difficulties", ["original"])}
		for file, (key, ids) in lists.items():
			(core / file).write_text(json.dumps({key: [{"id": i} for i in ids]}), encoding="utf-8")
		return core

	def table(self, name, entries):
		path = self.tmp / name
		path.write_text(json.dumps({"text": entries}, ensure_ascii=False), encoding="utf-8")
		return path

	def test_missing_descriptions_are_counted_and_unknown_ones_fail(self):
		core = self.core()
		en = self.table("en.json", {"game.saved": "Saved.", "desc.item.tonic": "Bitter.", "desc.status.sleep": "Zzz.", "desc.difficulty.original": "As made."})
		ja = self.table("ja.json", {"game.saved": "セーブした。", "desc.item.tonic": "苦い。"})
		code, out, _ = self.run_cli("descriptions", "--text", en, "--text", ja, "--core", core)
		self.assertEqual(0, code)
		self.assertIn("en.json: item 1 of 2, equipment 0 of 1, ability 0 of 1, status 1 of 1, enemy 0 of 1, difficulty 1 of 1", out)
		self.assertIn("  no description (item): gel", out)
		self.assertIn("0 problem(s)", out)

		bad = self.table("bad.json", {"desc.item.potion": "Unknown id.", "desc.weapon.cap": "Unknown kind.", "desc.equipment.cap": "Fine."})
		code, out, _ = self.run_cli("descriptions", "--text", bad, "--core", core)
		self.assertEqual(1, code)
		self.assertIn("problem: 'desc.item.potion' describes nothing", out)
		self.assertIn("problem: 'desc.weapon.cap' describes nothing", out)
		self.assertIn("2 problem(s)", out)

	def test_the_game_tables_describe_only_things_the_battle_data_has(self):
		text = ROOT / "game" / "data" / "text"
		reports = descriptions.check([text / "en.json", text / "ja.json"], ROOT / "core" / "data")
		self.assertEqual([], [key for r in reports for key in r.unknown])
		self.assertTrue(all(r.described["item"] > 0 for r in reports))

	def test_a_core_directory_without_the_lists_fails_cleanly(self):
		en = self.table("en.json", {})
		code, _, err = self.run_cli("descriptions", "--text", en, "--core", self.tmp / "nowhere")
		self.assertNotEqual(0, code)
		self.assertIn("cannot read the item ids", err)
