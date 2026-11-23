using System.Text.Encodings.Web;
using System.Text.Json;

using Psiv.Session;

namespace Psiv.Sample;

// `--capabilities`: what this build accepts, as JSON with a stable key order, read from the tables the checks and the
// script player use, so the report cannot say more or less than they do.
public static class Capabilities
{
	public const int SchemaVersion = 1;

	// The headless script's first words; the player refuses a line whose first word is not here.
	public static readonly string[] Verbs = ["animlog", "assets", "audio", "battlelayout", "click", "clickcell", "cmd", "combo", "controls", "datafile", "dev", "devrefused", "dump",
		"equip", "expect", "fieldinput", "flashing", "hold", "pad", "padhold", "padstick", "paduntil", "present", "press", "pressuntil", "savefile", "shot", "trace", "wait", "waitstate",
		"waitstatehas", "watch", "window"];

	public static readonly string[] ExpectKinds = ["actor", "animlog", "animloghas", "apart", "asset", "audio", "audiocount", "binding", "label", "letterbox", "mode", "noaudio", "notext", "notoast", "pose",
		"present", "presenthas", "screen", "state", "statehas", "statenot", "toast"];

	// The state probes (`expect state KEY VALUE`); SampleController.State answers `?` for any other key.
	public static readonly string[] Probes = ["aboard", "abovecover", "always_run", "ambient", "ambient_talk", "anim", "animated", "animframe", "animkey", "animsprites", "animtracks", "area", "auto_advance",
		"auto_macro", "auto_policy", "auto_trouble", "backdrop", "backdropdim", "backdropfx", "banner", "battle_speed", "battlelayout", "battlelines", "battlelog", "battlelogshown", "battlemenu", "battleoverlay",
		"battlejolt", "battlephase", "battleshake", "battlesync", "battleunusable", "bitmapfonts", "buffer", "camera", "cell", "clock", "commands", "compare", "console", "consolelast", "conveyors", "credits",
		"darkness", "description", "destinations", "dev", "devdraw", "deverror", "devmark", "dialogue", "dialogueoverlaps", "dialoguepage", "difficulty", "effectsize",
		"encounters", "enemygroups", "enemyplaces", "enemyicons", "enemymarks", "exitmarks", "fieldinput", "fieldlook", "fieldmarks", "fieldmotion", "fieldpicture", "figureposes", "figures", "figuresizes", "flash",
		"flashing", "flying", "follower", "fontmissing", "formation", "glide", "held", "hudmark", "interact_prompt", "interactprompt", "interacttarget", "itemsort", "keeper", "language",
		"leader", "leaderpos", "line", "lineportrait", "loglines", "macrocheck", "macros", "maphold", "marks", "menukind", "music", "narration", "narrationreveal", "numbers", "objects", "overlaps",
		"overlays", "page", "pagetext", "panels", "paneltiles", "party", "partyplaces", "partystatus", "paused", "picture", "pictures", "platforms", "playtime", "preview", "prompt", "ranks",
		"reduced_flash", "released", "reload", "reveal", "reward", "scenecamera", "screenlight", "selected", "shake", "shipped_names", "shown", "shownportrait", "speaker", "speed", "spritesteps", "step", "strip",
		"tab", "tags", "talk", "text_size", "text_speed", "tilehooks", "timescale", "titleart", "titlesizes", "toastboxes", "top", "transition", "uiframes", "vehicles", "victory", "voice",
		"volumes", "walk", "walkbuffer", "walkstep", "windows", "wrapped"];

	// Probes that take a name after the dot: `uifield.SCREEN.FIELD`, `window.NAME`, `figure.NAME` and so on.
	public static readonly string[] ProbePrefixes = ["actorlook.", "actorpos.", "actorroute.", "assetfile.", "cellpixel.", "enemy.", "figure.", "interactat.", "macro.", "overlay.", "pixel.", "preview.", "sheet.",
		"sheetpose.", "uifield.", "window."];

	public static bool IsProbe(string key) => Probes.Contains(key) || ProbePrefixes.Any(p => key.StartsWith(p, StringComparison.Ordinal));

	public static string Report(GameData data)
	{
		using var stream = new MemoryStream();
		using (var w = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true, Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping }))
		{
			w.WriteStartObject();
			w.WriteNumber("schema_version", SchemaVersion);
			w.WriteString("library_version", GameSession.LibraryVersion);
			w.WritePropertyName("assets");
			Tables(w, AssetManifest.Schema);
			w.WritePropertyName("ui_layout");
			w.WriteStartObject();
			w.WritePropertyName("files");
			Tables(w, UiReader.Schema);
			List(w, "element_common", UiReader.Common.Split(' '));
			w.WritePropertyName("elements");
			w.WriteStartObject();
			foreach ((string type, string props) in UiReader.ElementTypes.OrderBy(e => e.Key, StringComparer.Ordinal))
			{
				List(w, type, props.Split(' ').Select(p => p.Split(':')[0]));
			}
			w.WriteEndObject();
			List(w, "anchors", UiReader.Anchors);
			List(w, "transition_kinds", UiReader.TransitionKinds);
			List(w, "transition_from", UiReader.Edges);
			List(w, "frame_types", UiReader.FrameTypes);
			List(w, "scaling_modes", UiReader.Modes);
			List(w, "scaling_filters", UiReader.Filters);
			List(w, "functions", UiExpr.Arity.Keys);
			List(w, "variables", UiFields.Variables);
			List(w, "item_variables", UiFields.ItemVariables);
			List(w, "window_edges", UiFields.WindowEdges);
			List(w, "common_fields", UiFields.Common);
			List(w, "timings", UiFields.Timings.Keys);
			w.WritePropertyName("timing_tables");
			Tables(w, UiFields.TimingTables);
			List(w, "style_params", UiFields.StyleParams.Keys);
			List(w, "text_roles", AssetManifest.RoleNames);
			List(w, "image_names", UiFields.ImageNames);
			List(w, "travel_kinds", UiFields.TravelKinds);
			List(w, "result_phases", UiFields.ResultPhases);
			List(w, "shop_flow_steps", UiFields.ShopFlowSteps);
			List(w, "inn_flow_steps", UiFields.InnFlowSteps);
			List(w, "save_flow_steps", UiFields.SaveFlowSteps);
			List(w, "title_entries", UiFields.TitleEntries);
			List(w, "menu_pages", UiFields.MenuPages);
			List(w, "extras_entries", UiFields.ExtrasEntries);
			List(w, "system_rows", UiFields.SystemRows);
			List(w, "equip_places", UiFields.EquipPlaces);
			List(w, "profile_rows", UiFields.ProfileKeys);
			List(w, "top_commands", UiFields.TopCommands);
			List(w, "member_commands", UiFields.MemberCommands);
			List(w, "vehicle_commands", UiFields.VehicleCommands);
			w.WritePropertyName("screens");
			w.WriteStartObject();
			foreach ((string key, UiFields.ScreenFields s) in UiFields.Screens.OrderBy(s => s.Key, StringComparer.Ordinal))
			{
				w.WritePropertyName(key);
				w.WriteStartObject();
				List(w, "fields", s.Fields);
				List(w, "item_fields", s.ItemFields);
				List(w, "slots", s.Slots);
				List(w, "windows", s.Windows);
				List(w, "params", s.Params);
				w.WriteBoolean("tags", s.Tags);
				w.WriteBoolean("statuses", s.Statuses);
				w.WriteBoolean("title", s.Title);
				w.WriteEndObject();
			}
			w.WriteEndObject();
			w.WriteEndObject();
			w.WritePropertyName("dialogue");
			w.WriteStartObject();
			List(w, "tag_actions", UiFields.TagActions);
			List(w, "built_in_tags", UiFields.BuiltInTags);
			w.WriteEndObject();
			w.WritePropertyName("battle_animation");
			w.WriteStartObject();
			List(w, "track_types", BattleAnimation.TrackTypes);
			w.WritePropertyName("fields");
			Tables(w, BattleAnimation.FieldTables(new AnimCheckContext(data, [], [], null, [])));
			List(w, "turn_phases", BattleAnimation.TurnPhases);
			List(w, "battle_phases", BattleAnimation.BattlePhases);
			List(w, "subjects", BattleAnimation.SubjectNames);
			List(w, "anchors", BattleAnimation.Anchors);
			List(w, "conditions", BattleAnimation.Conditions);
			List(w, "eases", BattleAnimation.Eases);
			List(w, "number_kinds", BattleAnimation.NumberKinds);
			List(w, "defeat_kinds", BattleAnimation.DefeatKinds);
			List(w, "transition_kinds", BattleAnimation.TransitionKinds);
			List(w, "layers", BattleAnimation.Layers);
			List(w, "cancel_reasons", BattleAnimation.CancelReasons);
			List(w, "reaction_keys", BattleAnimation.ReactionKeys);
			List(w, "event_cues", BattleAnimation.EventCues);
			List(w, "changes", BattleAnimation.Changes);
			List(w, "outcomes", BattleAnimation.Outcomes);
			w.WriteEndObject();
			w.WritePropertyName("battle_presentation");
			w.WriteStartObject();
			List(w, "sections", BattlePresentation.Sections);
			List(w, "steps", BattlePresentation.Steps);
			List(w, "animations", BattlePresentation.Animations);
			List(w, "kinds", BattlePresentation.Kinds);
			List(w, "effects", BattlePresentation.Effects);
			List(w, "elements", BattlePresentation.Elements);
			List(w, "changes", BattlePresentation.Changes);
			w.WriteEndObject();
			List(w, "game_actions", Enum.GetNames<GameAction>().Select(Snake));
			w.WritePropertyName("script");
			w.WriteStartObject();
			List(w, "verbs", Verbs);
			List(w, "expect", ExpectKinds);
			List(w, "probes", Probes);
			List(w, "probe_prefixes", ProbePrefixes);
			w.WriteEndObject();
			w.WriteEndObject();
		}
		return System.Text.Encoding.UTF8.GetString(stream.ToArray()) + "\n";
	}

	private static string Snake(string name) => string.Concat(name.Select((c, i) => char.IsUpper(c) && i > 0 ? "_" + char.ToLowerInvariant(c) : char.ToLowerInvariant(c).ToString()));

	// Names sorted, so the report does not change with a table's order in the code.
	private static void List(Utf8JsonWriter w, string name, IEnumerable<string> values)
	{
		w.WritePropertyName(name);
		w.WriteStartArray();
		foreach (string v in values.Distinct().Order(StringComparer.Ordinal))
		{
			w.WriteStringValue(v);
		}
		w.WriteEndArray();
	}

	private static void Tables<T>(Utf8JsonWriter w, IReadOnlyDictionary<string, T> tables) where T : IEnumerable<string>
	{
		w.WriteStartObject();
		foreach ((string key, T fields) in tables.OrderBy(t => t.Key, StringComparer.Ordinal))
		{
			List(w, key, fields);
		}
		w.WriteEndObject();
	}
}
