namespace Psiv.Sample;

// What each screen offers its layout: the data fields its expressions may read (item fields are what a list's or a
// repeater's items carry), the slots its code draws into, the windows its code places things by, and its params.
// docs/ui-layout.md lists the same per screen; the check refuses any other name.
public static class UiFields
{
	public sealed class ScreenFields(string screen, string[] fields, string[]? itemFields = null, string[]? slots = null, string[]? windows = null, string[]? parameters = null,
		bool tags = false)
	{
		public string Screen => screen;
		public HashSet<string> Fields { get; } = [.. fields, .. Common];
		public HashSet<string> ItemFields { get; } = [.. itemFields ?? []];
		public HashSet<string> Slots { get; } = [.. slots ?? []];
		public HashSet<string> Windows { get; } = [.. windows ?? []];
		public HashSet<string> Params { get; } = [.. parameters ?? []];
		// The screen reads a `tags` table: what each control tag of a line does.
		public bool Tags => tags;

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
	};

	// What a `tags` table may map a control tag to: an audio cue or a music track by id (the tag's value, or the entry's
	// `id`), the music stopped, a live panel shown (on the entry's `slot`), hidden or all cleared, the speaker's portrait
	// taken away, or only a transcript line.
	public static readonly string[] TagActions = ["cue", "music", "music_stop", "panel_show", "panel_hide", "panel_clear", "portrait_off", "log"];

	// The tags a line's host acts on itself (events/FORMAT.md "Control tags") and the name tokens the session resolves,
	// which a `tags` table cannot map.
	public static readonly string[] BuiltInTags = ["page", "wait", "portrait", "var", "name", "name_upper", "name_lower"];

	// Tables of timings: characters a second per text speed, and the pause after each punctuation character.
	public static readonly Dictionary<string, string[]> TimingTables = new()
	{
		["text_speed"] = ["slow", "normal", "fast"],
		["punctuation_pauses"] = [],
	};

	// The battle's two layouts (the presentation table's `front` and `side`) read the same fields and params.
	private static readonly string[] BattleData = ["actor_center", "actor_left", "actor_right", "auto", "banner", "banner_alpha", "banner_big",
		"banner_cancel", "banner_plate", "command_center", "cursor", "description", "description_shown", "enemy_back", "enemy_count", "hint", "hit_fade",
		"hit_flash", "key_back", "key_confirm", "key_fast_forward", "key_text_log", "log_lines", "log_shown", "members", "menu_items", "menu_shown",
		"menu_title", "outcome", "party", "phase", "result_lines", "target_pulse", "vehicle"];

	private static readonly string[] BattleItems = ["acting", "alive", "hit", "hp", "hp_low", "icon_count", "kind", "max_hp", "max_tp", "name", "newest",
		"next", "portrait", "state", "targeted", "tp", "usable"];

	private static readonly string[] BattleSlots = ["anchor", "effects", "icons", "stage", "target_pointer"];

	private static readonly string[] BattleParams = ["enemy_back_foot", "enemy_front_foot", "enemy_rank_spacing_max", "enemy_rank_span",
		"enemy_ranks_interleaved", "enemy_row_foot", "enemy_row_max", "enemy_row_spacing_max", "enemy_row_span", "enemy_row_stagger", "enemy_size",
		.. Enumerable.Range(0, 8).SelectMany(n => new[] { $"enemy_slot_{n}_foot", $"enemy_slot_{n}_x" }),
		"enemy_x", "log_rows", "log_wrap_margin", "party_dx", "party_dy", "party_foot", "party_size", "party_x", "stage_bottom", "step_gap", "step_margin",
		"vehicle_foot", "vehicle_size", "vehicle_x"];

	public static readonly Dictionary<string, ScreenFields> Screens = new()
	{
		["battle_front"] = new("battle_front", BattleData, BattleItems, BattleSlots,
			["command", "description", "keys", "log", "name_plate", "strip"], BattleParams),
		["battle_side"] = new("battle_side", BattleData, BattleItems, BattleSlots, ["log"], BattleParams),
		["title"] = new("title",
			["title", "subtitle", "game_over", "options", "cursor", "version", "hint", "hero_frame", "creature_frame"],
			slots: ["scene", "toasts"]),
		["sound_test"] = new("sound_test",
			["tracks", "list", "playing", "hint"],
			slots: ["scene"]),
		["inn"] = new("inn",
			["cost", "money", "cursor", "bed", "top", "options"]),
		["save_prompt"] = new("save_prompt",
			["leave", "leave_selected"],
			slots: ["backdrop", "slot_list"]),
		["confirm"] = new("confirm",
			["question", "on_yes", "options"]),
		["field"] = new("field",
			["hud", "area", "status", "banner_visible", "banner_title", "banner_kind", "banner_alpha", "banner_drop", "banner_ms", "banner_left_ms", "toasts", "hint"],
			itemFields: ["text", "alpha", "box_alpha", "width"]),
		["dialogue"] = new("dialogue",
			["banner_visible", "banner_title", "banner_kind", "banner_alpha", "banner_drop", "banner_ms", "banner_left_ms", "top", "box_visible", "has_line", "speaker",
				"portrait", "line", "revealed", "all_shown", "line_step", "hint", "next_arrow_ms", "arrow", "has_choice", "prompt", "options", "cursor"],
			tags: true),
		["pause"] = new("pause",
			["hud", "area", "status", "rows", "cursor", "hint"],
			slots: ["scene"]),
		["travel"] = new("travel",
			["title", "question", "labels", "list", "description", "description_missing", "hint"],
			slots: ["backdrop", "toasts"]),
		["shop"] = new("shop",
			["title", "open", "tab", "tabs", "money", "stall", "rows", "list", "description", "description_missing", "compare", "hint"],
			itemFields: ["text", "price", "have"],
			slots: ["backdrop", "toasts"]),
		["name_entry"] = new("name_entry",
			["portrait", "member_name", "entry_h", "hint"],
			slots: ["backdrop", "entry"]),
		["panels"] = new("panels",
			["dim", "panels"],
			itemFields: ["left", "top", "width", "height", "shown", "shadow_alpha", "skinned"],
			slots: ["frame", "scene"]),
		["menu"] = new("menu",
			["tabs", "tab", "money", "hint"],
			slots: ["backdrop", "page", "toasts"]),
		["menu_status"] = new("menu_status",
			[.. Area, "view", "members", "list", "portrait", "name", "level", "experience", "experience_next", "hp", "max_hp", "tp", "max_tp", "pages", "page",
				"sub_list", "stat_rows", "picking", "abilities", "description", "target_name", "targets", "target_list"],
			itemFields: ["cells", "shown", "label", "value", "usable", "kind", "cost", "uses"]),
		["menu_items"] = new("menu_items",
			[.. Area, "picking", "sort_name", "item_count", "item_total", "items", "list", "description", "target_name", "members", "member_list"]),
		["menu_equip"] = new("menu_equip",
			[.. Area, "view", "members", "list", "member_name", "slot_rows", "slot_list", "slot_name", "choices", "choice_list", "right", "worn_text", "has_change",
				"change_text", "removing", "has_preview", "piece_name", "description", "effects", "effect_lines", "wearers", "can_wear", "refusal", "with_it",
				"compare_rows", "notes"],
			itemFields: ["enabled", "label", "before", "after"],
			windows: ["piece"]),
		["save_slots"] = new("save_slots",
			[.. Area, "slots", "list", "active"],
			itemFields: ["head", "enabled", "detail", "detail_color"]),
		["menu_order"] = new("menu_order",
			[.. Area, "rows", "list"]),
		["menu_macros"] = new("menu_macros",
			[.. Area, "macro_count", "macro_rows", "list", "stage", "building", "has_macro", "title", "name_prompt", "sub_list", "actions", "step_notes", "steps",
				"what", "stage_rows", "stage_list"],
			itemFields: ["ok", "enabled"],
			slots: ["name_pad"]),
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
			[.. Area, "options"]),
		["quest_log"] = new("quest_log",
			[],
			slots: ["backdrop", "quests"]),
		["text_log"] = new("text_log",
			[],
			slots: ["backdrop", "log"]),
		["load_game"] = new("load_game",
			["has_saves", "hint"],
			slots: ["scene", "slot_list", "toasts"]),
		["options"] = new("options",
			["title_mode", "title", "on_controls", "rows", "list", "description", "hint"],
			itemFields: ["label", "value", "changeable", "enabled"],
			slots: ["scene", "backdrop", "controls", "toasts"]),
		["controls"] = new("controls",
			[.. Area, "actions", "list", "active", "picking", "action_name", "choices", "choice", "capturing", "ask"],
			itemFields: ["label", "keys", "buttons", "reset"]),
	};
}
