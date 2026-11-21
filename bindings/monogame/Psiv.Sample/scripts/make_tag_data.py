#!/usr/bin/env python3
"""Copies the placeholder data for scripts/dialogue_tags.txt and scripts/dialogue_tags_ja.txt and adds one event,
`dialogue_tags`, whose lines use every control tag the sample acts on (events/FORMAT.md "Control tags"), in English and
Japanese: a portrait switch and a page break, three waits and three values, a narration longer than the dialogue box,
and two tags of the sample's own: `sound`, which the dialogue layout's tags table maps to an audio cue (once with a cue
the game has, once with one it lacks), and `shake`, which no table maps, so it is only logged; then `portrait_off`. A
second event, `portrait_expressions`, switches the guide's portrait by expression (`{portrait:guide:smile}`) for
scripts/assets_portraits.txt, naming one expression the test assets lack in both of its lines. A third,
`dialogue_pictures`, for scripts/dialogue_pictures.txt, shows the test assets' still pictures with the `picture` tags (a
chart, then a bell tower in its place, one the assets lack, a hide naming the picture that is not up, then a clear) and
rings a `bell` tag whose values a swapped-in dialogue layout maps to cues (two values it maps, one it leaves to its
default).

Usage: make_tag_data.py GAME_DATA CORE_DATA OUT

OUT gets game/data and core/data side by side, where game.json's core_data looks. The events are on no hook: the scripts
start them with the `dev event` command.
"""

import json
import shutil
import sys
from pathlib import Path

EVENT = """
# Test-only (scripts/make_tag_data.py): every control tag the sample acts on, and two of its own.
event dialogue_tags skippable
	say guide @tags.portrait as guide_smile
	say guide @tags.wait
	narrate @tags.long
	say dockhand @tags.hooks
end

# Test-only (scripts/make_tag_data.py): the guide's portrait by expression.
event portrait_expressions skippable
	say guide @tags.expressions
	say guide @tags.expressions_again
end

# Test-only (scripts/make_tag_data.py): still pictures and a tag whose values map to cues.
event dialogue_pictures skippable
	say guide @pictures.show
	say guide @pictures.swap
	narrate @pictures.hide
end
"""

STRINGS = {
	"en": {
		"tags.portrait": "Back so soon?{portrait:guide} Then you had better hear this.{page}"
			"The cove bell rang three times at midnight, and nobody was there to pull the rope.",
		"tags.wait": "Listen{wait:20}.{wait:20}.{wait:20}. There it is again. {var:leader}, keep your {var:money} coins close and a "
			"{var:tonic} handy.",
		"tags.long": "The harbor wakes slowly. Gulls argue over a basket of sprats on the quay, the ferry creaks against its ropes, "
			"and the smell of tar and wet rope drifts up from the boatsheds. Down by the noticeboard a cart has lost a wheel, and "
			"three fishers are arguing about whose turn it is to fix it. Beyond the breakwater the sea lies flat and grey, and "
			"somewhere past the reeds the old cove keeps its secrets. The lamplighter is still asleep on the bench outside the "
			"inn, his ladder propped against the wall, and the baker has already sold every round loaf the town wanted. A bell "
			"rings once, far away, and the whole town pretends not to hear it. Nobody says the word cove out loud before noon.",
		"tags.hooks": "{sound:lookout_creak}Did you hear that creak?{shake:2} The whole pier is trembling!{sound:foghorn} "
			"{portrait_off}I am not staying to find out.",
		"tags.expressions": "{portrait:guide:smile}Good to see you again.{portrait:guide:frown} That bell still worries me."
			"{portrait:guide:grin} Chin up, though.",
		"tags.expressions_again": "{portrait:guide:grin}Off you go.{portrait:guide} Mind the tide.",
		"pictures.show": "{picture:chart}Here is the cove on the old chart.{bell:1} Hear the pier answer?",
		"pictures.swap": "{picture:tower}And this is the bell tower.{bell:2} It rang again.{bell:9}{picture:lantern}",
		"pictures.hide": "{picture_hide:chart}That is all I know.{picture_hide:tower} Off you go.{pictures_clear}",
	},
	"ja": {
		"tags.portrait": "もう戻ったのかい？{portrait:guide}なら、これを聞いておくといい。{page}"
			"真夜中に入り江の鐘が三度鳴った。綱を引く者は誰もいなかったのに。",
		"tags.wait": "聞いて{wait:20}。{wait:20}。{wait:20}。また鳴った。{var:leader}、{var:money}枚の硬貨を手放さず、{var:tonic}を用意しておきな。",
		"tags.long": "港はゆっくりと目を覚ます。岸壁ではカモメが小魚のかごを取り合い、渡し船は綱にきしみ、舟小屋からはタールと濡れた綱の"
			"においが漂ってくる。掲示板のそばでは荷車の車輪が外れ、三人の漁師が誰が直す番かで言い争っている。防波堤の向こうの海は"
			"平らで灰色に広がり、葦原のさらに先では古い入り江が秘密を守り続けている。点灯夫は宿屋の前の長椅子でまだ眠っていて、"
			"はしごは壁に立てかけられたままだ。パン屋は町じゅうが欲しがっていた丸いパンをもう売り切ってしまった。遠くで鐘が一度"
			"だけ鳴り、町じゅうが聞こえないふりをする。昼になるまで、誰も入り江という言葉を口にしない。市場の屋台には干した海藻"
			"と貝殻の飾りが並び、子どもたちは桟橋の端から足をぶらぶらさせて、まだ帰ってこない船の数を指で数えている。宿屋のおか"
			"みは窓を大きく開けて、今日も昼過ぎには霧が出るだろうかと空を見上げ、洗濯物を干すかどうか長いあいだ迷っている。灯台守"
			"の老人は今朝も坂道をゆっくり下りてきて、桟橋の先で網を繕う娘たちに、昨夜の鐘の音を聞いたかと一人ずつ尋ねて回った。"
			"誰もが首を横に振ったが、その目は揃って入り江のほうを見ていた。港の朝は、いつもと同じように見えて、どこか少しだけ違っていた。",
		"tags.hooks": "{sound:lookout_creak}今のきしみ、聞こえた？{shake:2}桟橋じゅうが揺れている！{sound:foghorn}"
			"{portrait_off}確かめに残る気はないよ。",
		"tags.expressions": "{portrait:guide:smile}また会えてうれしいよ。{portrait:guide:frown}あの鐘がまだ気がかりだ。{portrait:guide:grin}でも、元気を出して。",
		"tags.expressions_again": "{portrait:guide:grin}さあ、行っておいで。{portrait:guide}潮に気をつけて。",
		"pictures.show": "{picture:chart}古い海図の入り江だ。{bell:1}桟橋が応えたのが聞こえる？",
		"pictures.swap": "{picture:tower}これが鐘楼だ。{bell:2}また鳴った。{bell:9}{picture:lantern}",
		"pictures.hide": "{picture_hide:chart}知っているのはこれだけ。{picture_hide:tower}さあ、行っておいで。{pictures_clear}",
	},
}


def main():
	game, core, out = (Path(a) for a in sys.argv[1:4])
	if out.exists():
		shutil.rmtree(out)
	shutil.copytree(game, out / "game" / "data")
	shutil.copytree(core, out / "core" / "data")
	data = out / "game" / "data"
	(data / "events" / "dialogue_tags.ev").write_text(EVENT.lstrip("\n"))
	game_file = data / "game.json"
	game_json = json.loads(game_file.read_text())
	game_json["events"].append("events/dialogue_tags.ev")
	game_file.write_text(json.dumps(game_json, indent="\t", ensure_ascii=False) + "\n")
	for language in game_json["languages"]:
		table = data / language["strings"]
		lines = "".join(f"{key} = {text}\n" for key, text in STRINGS[language["code"]].items())
		table.write_text(table.read_text().rstrip("\n") + "\n\n" + lines)


if __name__ == "__main__":
	main()
