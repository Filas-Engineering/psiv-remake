import unittest

from support import TempCase, record

from textkit import stats


class StatsTest(TempCase):
	def test_counts_follow_what_the_player_reads(self):
		conn = self.build_db([
			record("guide", 0, "{portrait:GUIDE}Hello, {var:hero}!{br}Lovely day.{page}Bye.{page}"),
			record("guide", 1, "{portrait:GUIDE}A longer single line here.", map="market"),
			record("traveler", 0, "Hi... there friend", speaker="TRAVELER"),
		])
		report = stats.collect(conn, top=2)
		self.assertEqual({"messages": 3, "lines": 5, "words": 12, "characters": 67}, report["total"])
		self.assertEqual({"messages": 2, "lines": 4, "words": 9, "characters": 49}, report["by_speaker"]["GUIDE"])
		self.assertEqual(["harbor", "market"], list(report["by_map"]))
		self.assertEqual(
			[("guide:1", 26), ("traveler:0", 18)],
			[(line["message_id"], line["length"]) for line in report["longest_lines"]],
		)
		self.assertEqual({"br": 1, "page": 2, "portrait": 2, "var": 1}, report["codes_by_name"])
		self.assertEqual(2, report["codes_by_token"]["portrait:GUIDE"])


if __name__ == "__main__":
	unittest.main()
