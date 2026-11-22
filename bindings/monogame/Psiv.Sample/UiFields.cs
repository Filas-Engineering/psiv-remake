namespace Psiv.Sample;

// What each screen offers its layout: the data fields its expressions may read (item fields are what a list's or a
// repeater's items carry), the slots its code draws into, the windows its code places things by, and its params.
// docs/ui-layout.md lists the same per screen; the check refuses any other name.
public static class UiFields
{
	public sealed class ScreenFields(string screen, string[] fields, string[]? itemFields = null, string[]? slots = null, string[]? windows = null, string[]? parameters = null,
		bool tags = false, bool statuses = false, bool title = false)
	{
		public string Screen => screen;
		public HashSet<string> Fields { get; } = [.. fields, .. Common];
		public HashSet<string> ItemFields { get; } = [.. itemFields ?? []];
		public HashSet<string> Slots { get; } = [.. slots ?? []];
		public HashSet<string> Windows { get; } = [.. windows ?? []];
		public HashSet<string> Params { get; } = [.. parameters ?? []];
		// The screen reads a `tags` table: what each control tag of a line does.
		public bool Tags => tags;
		// The screen reads a `statuses` table: how each status shows on the party's slots.
		public bool Statuses => statuses;
		// The screen reads `start` and `options`: the title's start state and its menu's entries.
		public bool Title => title;

		public bool Has(string name) => Fields.Contains(name);
	}

	// A layout drawn into the rectangle another layout's slot gave it reads that rectangle from these.
	public static readonly string[] Area = ["area_x", "area_y", "area_w", "area_h"];

	// Every screen has these.
	public static readonly string[] Common = ["clock_ms", "text_scale", "dev", "gamepad"];

	// Names every expression may read: the logical screen, the parent window's size, the window's content size, the
	// element's measured text width, and inside a window rectangle its own width and height.
	public static readonly HashSet<string> Variables = ["W", "H", "pw", "ph", "cw", "ch", "tw"];

	// Names inside a list's row or a repeater's templates.
	public static readonly HashSet<string> ItemVariables = ["i", "item", "selected", "row_y", "count", "col", "row", "text"];

	public static readonly HashSet<string> WindowEdges = ["x", "y", "w", "h", "right", "bottom"];

	// Timings in milliseconds (or characters a second), with the sample's own values as defaults.
	public static readonly Dictionary<string, int> Timings = new()
	{
		["auto_advance_ms_per_char"] = 50,
		["auto_advance_min_ms"] = 1000,
		["text_tick_ms"] = 70,
		["next_arrow_ms"] = 260,
		["caret_ms"] = 400,
		["cursor_blink_ms"] = 0,
		["toast_ms"] = 4000,
		["toast_fade_ms"] = 400,
		["panel_transition_ms"] = 350,
		["wait_unit_ms"] = 16,
		["list_repeat_delay_ms"] = 0,
		["list_repeat_ms"] = 0,
		["run_speed_percent"] = 200,
		["music_fade_ms"] = 800,
		["game_over_return_ms"] = 0,
	};

	// Numbers the code's own drawing reads (styles.json `params`), with the sample's values as defaults: the field's cell
	// and its shake, and the notices' boxes, which several screens draw through their `toasts` slot.
	public static readonly Dictionary<string, int> StyleParams = new()
	{
		["field_cell"] = Art.Tile,
		["dev_draw"] = 1,
		["dev_scale"] = 100,
		["dev_scale_bitmap"] = 0,
		["dev_badge_x"] = -1,
		["dev_badge_y"] = 12,
		["field_shake"] = 10,
		["toast_gap"] = 6,
		["toast_h"] = 32,
		["toast_pad"] = 14,
		["toast_text_y"] = 7,
	};

	// What a `tags` table may map a control tag to: an audio cue or a music track by id (the tag's value, or the entry's
	// `id`), the music stopped, a live panel shown (on the entry's `slot`), hidden or all cleared, the speaker's portrait
	// taken away, or only a transcript line.
	public static readonly string[] TagActions = ["cue", "fx", "music", "music_pause", "music_resume", "music_stop", "panel_show", "panel_hide", "panel_clear", "picture", "picture_hide", "picture_clear",
		"portrait_off", "log"];

	// What a title layout's `options` may list, in the built-in order. A listed entry still shows only when it can be
	// chosen: retry after a lost battle that allows one, the sound test after a finished story.
	public static readonly string[] TitleEntries = ["retry", "new_game", "continue", "load", "sound_test", "erase", "options", "quit"];

	// The field menu's pages, which its layout's `entries` may list in any order; the built-in menu shows MenuDefault.
	public static readonly string[] MenuPages = ["status", "items", "techniques", "skills", "abilities", "equip", "order", "macros", "combos", "quests", "codex", "log",
		"options", "save", "system", "party_talk", "extras"];

	public static readonly string[] MenuDefault = ["status", "items", "equip", "save", "order", "macros", "combos", "quests", "codex", "log", "options"];

	// The Extras menu's settings rows, changed in place; in battle only BattleExtras show.
	public static readonly string[] SettingRows = ["text_speed", "battle_speed", "flashing", "auto_advance", "text_size", "auto_trouble", "auto_policy"];

	public static readonly string[] BattleExtras = ["text_speed", "battle_speed", "flashing", "auto_trouble", "auto_policy"];

	// What an Extras layout's `entries` may list: the field menu's pages but itself (they open the field menu there, from
	// the field), the options and controls, the photosensitivity notice, the developer tools (with --dev) and the settings rows.
	public static readonly string[] ExtrasEntries = [.. MenuPages.Where(p => p != "extras"), "controls", "photosensitivity", "dev", .. SettingRows];

	public static readonly string[] ExtrasDefault = ["options", "controls"];

	// The system page's rows (menu_system `rows`): the slot list, quitting to the title, and settings rows.
	public static readonly string[] SystemRows = ["save", "quit", .. SettingRows];

	public static readonly string[] SystemDefault = ["save", "text_speed", "battle_speed", "auto_advance"];

	// The battle's command rows by step with `command_top` (a battle layout's `command_rows`): the top-level step's and a
	// member's, each in the built-in order.
	public static readonly string[] TopCommands = ["fight", "macro", "run"];

	public static readonly string[] MemberCommands = ["attack", "technique", "skill", "item", "defend", "escape", "macro", "auto"];

	// A vehicle's rows (`command_rows.vehicle`): its attack, its skills (one row each), the party's macros, auto-battle and
	// escape; without the list the first, second and last.
	public static readonly string[] VehicleCommands = ["attack", "skills", "macro", "auto", "escape"];

	public static readonly string[] VehicleDefault = ["attack", "skills", "escape"];

	// The places an Equip page's `places` may list, in the built-in order.
	public static readonly string[] EquipPlaces = ["weapon", "left_hand", "head", "body", "arms", "accessory"];

	// What a status page's profile rows may show: the member's and its detail's fields the session gives, and `data`, the
	// character's own profile rows from party.json in their order.
	public static readonly string[] ProfileKeys = ["name", "level", "experience", "experience_next", "kind", "hp", "max_hp", "tp", "max_tp", "strength", "mental",
		"agility", "dexterity", "defense", "critical", "evasion", "magic_defense", "attack_element", "data"];

	// The tags a line's host acts on itself (events/FORMAT.md "Control tags") and the name tokens the session resolves,
	// which a `tags` table cannot map.
	public static readonly string[] BuiltInTags = ["page", "wait", "portrait", "var", "name", "name_upper", "name_lower", "narration"];

	// Tables of timings: characters a second per text speed, and the pause after each punctuation character.
	public static readonly Dictionary<string, string[]> TimingTables = new()
	{
		["text_speed"] = ["slow", "normal", "fast"],
		["punctuation_pauses"] = [],
	};

	// The opacity of the black over the field that a `backdrop` slot draws, 0 to 1 (0.55 when the files leave it out).
	public const string BackdropDim = "backdrop_dim";

	// The tabs elements' sizes (menu and menu_status params): a tab's height, the room beside its text, the gap after it
	// and the text's top.
	public static readonly string[] TabParams = ["tab_gap", "tab_h", "tab_pad", "tab_text_y"];

	// A name pad's sizes (name_entry and menu_macros params).
	public static readonly string[] PadParams = ["pad_box_h", "pad_cell_h", "pad_cell_w", "pad_entry_size", "pad_gap", "pad_label_size", "pad_label_y", "pad_text_x", "pad_text_y"];

	// The battle's two layouts (the presentation table's `front` and `side`) read the same fields and params.
	private static readonly string[] BattleData = ["action_chosen", "action_command", "actor_center", "actor_left", "actor_right", "actor_slot", "auto", "banner", "banner_alpha", "banner_big",
		"banner_cancel", "banner_plate", "command_center", "cursor", "description", "description_shown", "enemy_back", "enemy_count", "enemy_groups", "hint", "hit_fade",
		"hit_flash", "key_back", "key_confirm", "key_fast_forward", "key_text_log", "list_index", "list_kind", "log_lines", "log_shown", "macro_steps", "macro_trouble",
		"macro_trouble_text", "members", "menu_items", "menu_shown", "menu_title", "outcome", "page_arrow", "party", "phase", "result_confirm", "result_lines", "result_more", "result_page", "result_pages", "result_step", "target_pulse",
		"transition_playing", "vehicle"];

	private static readonly string[] BattleItems = ["acting", "alive", "command", "command_icon", "cost", "fallen", "hit", "icon", "hp", "hp_low", "icon_count", "issue", "kind", "level",
		"max_hp", "max_tp", "max_uses", "name", "newest", "next", "number", "ok", "portrait", "slot", "state", "status_color", "targeted", "tp", "usable", "uses"];

	// A battle item's flag per status of the game data: `status_ID`, true while the combatant shows that status.
	public const string StatusFlag = "status_";

	private static readonly string[] BattleSlots = ["anchor", "effects", "icons", "stage", "target_pointer"];

	private static readonly string[] BattleParams = ["auto_choose", "backdrop_h", "command_axis", "command_top", "critical_marks", "critical_marks_enemy", "critical_marks_party", "damage_numbers", "dev_badge_y", "effect_scale", "effect_scale_large", "effectiveness_marks", "effectiveness_marks_enemy", "effectiveness_marks_party", "enemy_back_foot", "enemy_bar_h",
		"enemy_bar_w", "enemy_bar_y", "enemy_bars", "enemy_figures", "enemy_front_foot", "enemy_icons_y", "enemy_labels", "enemy_rank_spacing_max", "enemy_rank_span", "enemy_ranks_interleaved",
		"enemy_row_foot", "enemy_row_max", "enemy_row_spacing_max", "enemy_row_span", "enemy_row_stagger", "enemy_size",
		.. Enumerable.Range(0, 8).SelectMany(n => new[] { $"enemy_slot_{n}_foot", $"enemy_slot_{n}_x" }),
		"enemy_statuses", "enemy_x", "game_over_confirm", "ground_h", "icon_gap", "icon_size", "item_order", "label_h", "label_max_width", "label_pad", "label_text_y", "label_y", "log_rows", "log_wrap_margin",
		"macro_auto_first", "macro_check", "macro_failed", "macro_flow", "miss_marks", "miss_marks_enemy", "miss_marks_party", "motion_percent", "number_critical_size", "number_gap", "number_member_y", "number_rise", "number_size", "number_tp_size", "number_word_size", "number_y", "party_dx",
		"party_dy", "party_figures", "party_foot", "party_size",
		.. Enumerable.Range(0, 5).SelectMany(n => new[] { $"party_slot_{n}_foot", $"party_slot_{n}_x" }),
		"party_x", "pointer_bob", "pointer_member_y", "pointer_size", "pointer_y", "result_page_lines", "stage_bottom", "step_aside", "step_gap",
		"step_margin", "vehicle_foot", "vehicle_size", "vehicle_x"];

	public static readonly Dictionary<string, ScreenFields> Screens = new()
	{
		["battle_front"] = new("battle_front", BattleData, BattleItems, BattleSlots,
			["command", "description", "keys", "log", "name_plate", "strip"], BattleParams, statuses: true),
		["battle_side"] = new("battle_side", BattleData, BattleItems, BattleSlots, ["log"], BattleParams, statuses: true),
		["title"] = new("title",
			["title", "subtitle", "game_over", "options", "cursor", "version", "hint", "hero_frame", "creature_frame", "logo", "logo_w", "logo_h", "hero_w", "hero_h",
				"creature_w", "creature_h", "art_scale", "start", "start_hint", "option_ids", "erasing", "erase_rows", "erase_cursor", "has_save", "save_count", "has_clear", "can_load"],
			slots: ["scene", "toasts"], title: true,
			parameters: ["continue", "erase"]),
		["credits"] = new("credits",
			["lines", "scroll", "shown", "total", "hint"],
			itemFields: ["text", "heading", "y"]),
		["sound_test"] = new("sound_test",
			["tracks", "list", "playing", "hint"],
			slots: ["scene"]),
		["inn"] = new("inn",
			["cost", "money", "cursor", "bed", "top", "options", "keeper", "per_member", "cost_each", "voice_line", "name", "title"]),
		["save_prompt"] = new("save_prompt",
			["leave", "leave_selected"],
			slots: ["backdrop", "slot_list"],
			parameters: [BackdropDim]),
		["confirm"] = new("confirm",
			["question", "on_yes", "options", "cursor"]),
		["field"] = new("field",
			["hud", "area", "status", "banner_visible", "banner_title", "banner_kind", "banner_alpha", "banner_drop", "banner_ms", "banner_left_ms", "toasts", "hint",
				"prompt_visible", "prompt_key", "prompt_text", "prompt_x", "prompt_y", "prompt_ready"],
			itemFields: ["text", "alpha", "box_alpha", "width"],
			parameters: ["area_banner", "arrive_toast", "click_actor", "followers_arrive"]),
		["dialogue"] = new("dialogue",
			["banner_visible", "banner_title", "banner_kind", "banner_alpha", "banner_drop", "banner_ms", "banner_left_ms", "top", "box_visible", "has_line", "speaker",
				"portrait", "line", "revealed", "all_shown", "line_step", "hint", "next_arrow_ms", "arrow", "has_choice", "prompt", "options", "cursor", "picture",
				"picture_w", "picture_h", "pictures", "narration_visible", "narration_rows", "narration_all_shown", "shown_ms", "line_key", "picture_top", "picture_names", "page_arrow", "choice_header"],
			itemFields: ["picture", "picture_w", "picture_h", "text", "full", "shown"],
			parameters: ["box_h", "box_margin", "box_top", "picture_stack", "narration_lines", "narration_cps", "narration_delay_ms", "narration_hold_ms", "narration_wait", "narration_width",
				"narration_when"],
			tags: true),
		["pause"] = new("pause",
			["hud", "area", "status", "rows", "cursor", "hint"],
			slots: ["scene"]),
		["travel"] = new("travel",
			["title", "question", "labels", "list", "description", "description_missing", "hint"],
			slots: ["backdrop", "toasts"],
			parameters: [BackdropDim]),
		["shop"] = new("shop",
			["title", "open", "tab", "tabs", "money", "stall", "rows", "list", "description", "description_missing", "compare", "hint", "money_value", "keeper", "voice_line",
				"step", "choices", "choice_cursor", "quantity", "quantity_max", "unit_price", "total", "result_text", "result_ok", "name", "bag", "bag_used", "bag_slots"],
			itemFields: ["text", "price", "have"],
			slots: ["backdrop", "toasts"],
			parameters: [BackdropDim, "steps"]),
		["name_entry"] = new("name_entry",
			["portrait", "member_name", "entry_h", "hint"],
			slots: ["backdrop", "entry"],
			parameters: [BackdropDim, .. PadParams]),
		["panels"] = new("panels",
			["dim", "panels"],
			itemFields: ["left", "top", "width", "height", "shown", "shadow_alpha", "skinned"],
			slots: ["frame", "scene"],
			parameters: ["bottom", "gutter", "margin", "slide"]),
		["menu"] = new("menu",
			["tabs", "tab", "money", "hint", "money_value", "party", "page_ids", "item_count", "macro_count"],
			itemFields: ["name", "level", "hp", "max_hp", "tp", "max_tp", "portrait", "alive"],
			slots: ["backdrop", "page", "toasts"],
			parameters: [BackdropDim, .. TabParams]),
		["menu_children"] = new("menu_children",
			[.. Area, "label", "children", "child_rows", "list", "tab_x", "tab_w", "list_w", "row_h", "rows_max", "list_pad", "text_size"],
			itemFields: ["enabled"],
			parameters: ["list_w", "pad", "row_h", "rows", "text_size"]),
		["menu_status"] = new("menu_status",
			[.. Area, "view", "members", "list", "portrait", "name", "level", "experience", "experience_next", "hp", "max_hp", "tp", "max_tp", "pages", "page",
				"sub_list", "stat_rows", "picking", "abilities", "description", "target_name", "targets", "target_rows", "target_list", "portrait_w", "portrait_h", "profile",
				"attack", "base_agility", "base_defense", "base_dexterity", "base_hp", "base_mental", "base_strength", "base_tp", "experience_for_level", "kind",
				"total_experience"],
			itemFields: ["cells", "shown", "label", "value", "usable", "kind", "cost", "uses", "total", "bonus", "key", "name", "reachable"],
			parameters: ["stat_bonus", .. TabParams]),
		["menu_items"] = new("menu_items",
			[.. Area, "picking", "sort_name", "item_count", "item_total", "items", "list", "description", "target_name", "members", "member_rows", "member_list", "acting", "actions",
				"action_ids", "action_list", "looking", "look_text", "result_text", "result_ok", "bag", "bag_used", "bag_slots", "discarding", "discard_step",
				"discard_count", "discard_max", "discard_text"],
			itemFields: ["name", "reachable"],
			parameters: ["actions", "discard"]),
		["menu_equip"] = new("menu_equip",
			[.. Area, "view", "members", "list", "member_name", "slot_rows", "slot_list", "slot_name", "choices", "choice_list", "right", "worn_text", "has_change",
				"change_text", "removing", "has_preview", "piece_name", "description", "effects", "effect_lines", "wearers", "can_wear", "refusal", "with_it",
				"compare_rows", "notes", "places", "member_index", "member_count"],
			itemFields: ["enabled", "label", "before", "after", "slot", "piece", "worn"],
			windows: ["piece"],
			parameters: ["members_first", "stats_preview_only"]),
		["save_slots"] = new("save_slots",
			[.. Area, "slots", "list", "active", "system", "system_rows", "system_cursor", "mode"],
			itemFields: ["head", "enabled", "detail", "detail_color", "number", "slot", "empty", "damaged", "area", "play_time", "difficulty", "party_text", "leader", "level", "written", "newest", "write_time"],
			parameters: ["system_menu"]),
		["menu_abilities"] = new("menu_abilities",
			[.. Area, "view", "kind", "members", "list", "name", "abilities", "sub_list", "description", "picking", "target_name", "targets", "target_rows", "target_list"],
			itemFields: ["label", "usable", "kind", "cost", "uses", "name", "reachable"],
			parameters: ["unusable"]),
		["menu_system"] = new("menu_system",
			[.. Area, "rows", "list", "description"],
			itemFields: ["id", "label", "value", "changeable"],
			parameters: ["system_action"]),
		["extras"] = new("extras",
			["rows", "list", "view", "notice", "description", "hint", "in_battle", "on_title"],
			itemFields: ["id", "label", "value", "changeable"],
			slots: ["scene", "toasts"]),
		["menu_order"] = new("menu_order",
			[.. Area, "rows", "list", "lineup"],
			itemFields: ["name", "level", "hp", "max_hp", "tp", "max_tp", "number", "picked", "statuses"]),
		["menu_macros"] = new("menu_macros",
			[.. Area, "macro_count", "macro_rows", "list", "stage", "building", "has_macro", "title", "name_prompt", "sub_list", "actions", "step_notes", "steps",
				"what", "stage_rows", "stage_list", "on_trouble", "recover_who", "unnamed", "policy", "option_rows"],
			itemFields: ["ok", "enabled"],
			slots: ["name_pad"],
			parameters: [.. PadParams, "options"]),
		["menu_combos"] = new("menu_combos",
			[.. Area, "discovered", "log_total", "total", "rows", "list", "has_entry", "members_text", "abilities_text", "found_time", "make_key"],
			itemFields: ["enabled"]),
		["menu_quests"] = new("menu_quests",
			[.. Area, "rows", "list"],
			itemFields: ["heading"]),
		["menu_codex"] = new("menu_codex",
			[.. Area, "titles", "list", "title", "body"]),
		["menu_log"] = new("menu_log",
			[.. Area, "lines", "first", "rows", "total"],
			parameters: ["wrap_width", "rows"]),
		["menu_options"] = new("menu_options",
			[.. Area, "options", "talking", "talk", "talk_speaker_name", "talk_speaker_id", "talk_portrait", "talk_text", "talk_shown", "talk_page", "talk_pages",
				"talk_index", "talk_count", "talk_more", "page_arrow"],
			itemFields: ["speaker", "speaker_id", "portrait", "line", "pages"],
			slots: ["talk_box"],
			parameters: ["party_talk", "talk_hide_menu", "talk_name_size", "talk_pad", "talk_row_h", "talk_rows", "talk_size"]),
		["quest_log"] = new("quest_log",
			[],
			slots: ["backdrop", "quests"],
			parameters: [BackdropDim]),
		["text_log"] = new("text_log",
			[],
			slots: ["backdrop", "log"],
			parameters: [BackdropDim]),
		["load_game"] = new("load_game",
			["has_saves", "hint"],
			slots: ["scene", "slot_list", "toasts"],
			parameters: ["select_newest"]),
		["options"] = new("options",
			["title_mode", "title", "on_controls", "rows", "list", "description", "hint"],
			itemFields: ["label", "value", "changeable", "enabled"],
			slots: ["scene", "backdrop", "controls", "toasts"],
			parameters: [BackdropDim, "flashing_original"]),
		["controls"] = new("controls",
			[.. Area, "actions", "list", "active", "picking", "action_name", "choices", "choice", "capturing", "ask"],
			itemFields: ["label", "keys", "buttons", "reset"],
			parameters: ["list_extras"]),
	};
}
