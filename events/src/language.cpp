#include "events/language.h"

namespace events {

const std::vector<CommandInfo>& script_commands() {
	static const std::vector<CommandInfo> commands = {
		{"ambient", {"ambient play <sound> [nowait]", "ambient stop [nowait]"}, false, true},
		{"anim", {"anim <actor> <animation> [loop] [nowait]"}, true, true},
		{"ask", {"ask [<text>] [into <flag>] ... yes ... no ... end"}, false, false},
		{"battle", {"battle <encounter> [aboard <vehicle>] [opening normal|party_first|enemy_first] [until survive <rounds>|until defeat <enemy>] ... win ... [lose ...] [flee ...] end"}, false, false},
		{"call", {"call <event>"}, true, false},
		{"camera", {"camera cut <camera> [nowait]", "camera move <camera> <seconds> [nowait]", "camera to <marker> [seconds] [nowait]", "camera to <x> <y> [seconds] [nowait]",
					   "camera reset [seconds] [nowait]"},
			false, true},
		{"choice", {"choice [<text>] [into <variable>] option <text> ... end"}, false, false},
		{"clear", {"clear <flag>"}, true, false},
		{"credits", {"credits <credits>"}, false, false},
		{"end", {"end game"}, false, false},
		{"equip", {"equip <member> <piece>"}, false, false},
		{"face", {"face <actor> <actor|marker|north|east|south|west> [nowait]"}, true, true},
		{"fade", {"fade out [seconds] [nowait]", "fade in [seconds] [nowait]"}, false, true},
		{"follower", {"follower <actor>", "follower none"}, false, false},
		{"give", {"give <item> [count]"}, false, false},
		{"goto", {"goto <label>"}, true, false},
		{"hide", {"hide <actor> [with <animation>] [nowait]"}, true, true},
		{"if", {"if <condition> ... [else if <condition> ...] [else ...] end"}, true, false},
		{"label", {"label <name>"}, true, false},
		{"learn", {"learn <member> <ability>"}, false, false},
		{"lock", {"lock player"}, false, false},
		{"look", {"look <actor> enemy <enemy>", "look <actor> <look>", "look <actor> default"}, true, false},
		{"money", {"money give <amount>", "money take <amount>"}, false, false},
		{"move", {"move <actor> [via <marker>...] to <marker> [speed <n>] [pathfind] [nowait]", "move <actor> [via <marker>...] to <x> <y> <z> [speed <n>] [pathfind] [nowait]"},
			true, true},
		{"music", {"music play <track> [fade <ms>|cut] [nowait]", "music stop [nowait]", "music fade <seconds> [nowait]", "music area <track>", "music area default"}, false, true},
		{"name", {"name entry <member>"}, false, false},
		{"narrate", {"narrate <text>"}, false, false},
		{"panel", {"panel show <id> camera <camera> layout <slot> [nowait]", "panel hide <id> [nowait]", "panel clear [nowait]"}, false, true},
		{"party", {"party add <member> [silent]", "party remove <member> [silent]", "party order <member>...", "party save [<name>]", "party restore [<name>]",
					  "party swap <slot|member> <member> [equipment keep|bag] [silent]"},
			false, false},
		{"picture", {"picture show <picture>", "picture hide [<picture>]", "picture clear"}, false, false},
		{"place", {"place <actor> <marker> [facing <direction>] [nowait]", "place <actor> <x> <y> <z> [facing <direction>] [nowait]"}, true, true},
		{"race", {"race ... end"}, true, false},
		{"remove", {"remove <actor> [until reload]"}, true, false},
		{"rest", {"rest [<rest point>]"}, false, false},
		{"return", {"return"}, true, false},
		{"save", {"save prompt"}, false, false},
		{"say", {"say <speaker> <text> [as <portrait>]"}, false, false},
		{"sequence", {"sequence ... end"}, true, true},
		{"set", {"set <flag>"}, true, false},
		{"shake", {"shake <seconds> [strength <n>] [nowait]"}, false, true},
		{"shop", {"shop <shop>"}, false, false},
		{"show", {"show <actor> [with <animation>] [nowait]"}, true, true},
		{"sound", {"sound <cue> [nowait]"}, true, true},
		{"spawn", {"spawn <event>"}, true, false},
		{"sync", {"sync"}, true, false},
		{"take", {"take <item> [count]"}, false, false},
		{"together", {"together ... end"}, true, false},
		{"unequip", {"unequip <member> <piece>"}, false, false},
		{"unlock", {"unlock player"}, false, false},
		{"var", {"var <name> = <integer>", "var <name> += <integer>", "var <name> -= <integer>"}, true, false},
		{"vehicle", {"vehicle give <vehicle> <map> <marker>", "vehicle place <vehicle> <map> <marker>", "vehicle remove <vehicle>"}, false, false},
		{"wait", {"wait <seconds>"}, true, true},
		{"warp", {"warp <map> <marker>"}, false, false},
		{"while", {"while <condition> ... end"}, true, false},
	};
	return commands;
}

const CommandInfo* script_command(const std::string& name) {
	for (const CommandInfo& c : script_commands()) {
		if (c.name == name) {
			return &c;
		}
	}
	return nullptr;
}

const std::vector<ConditionInfo>& condition_tests() {
	static const std::vector<ConditionInfo> tests = {
		{"vehicle", "[not] vehicle <vehicle>", TermTest::owns},
		{"aboard", "[not] aboard <vehicle>", TermTest::aboard},
		{"party", "[not] party <member>", TermTest::member},
		{"item", "[not] item <item>", TermTest::holds},
		{"money", "[not] money ==|!=|<|<=|>|>= <integer>", TermTest::money},
		{"", "[not] <flag>", TermTest::none},
		{"", "[not] <variable> ==|!=|<|<=|>|>= <integer>", TermTest::none},
	};
	return tests;
}

}
