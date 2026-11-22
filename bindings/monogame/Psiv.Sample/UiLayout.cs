using System.Globalization;
using System.Reflection;
using System.Text.Json;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// One element of a window: its type and its properties, each already parsed (expressions, colors, child elements).
public sealed class UiElement
{
	public required string Type { get; init; }
	public string Name { get; init; } = "";
	public Dictionary<string, UiExpr> Exprs { get; } = [];
	public Dictionary<string, string> Strings { get; } = [];
	public Dictionary<string, bool> Flags { get; } = [];
	public Dictionary<string, List<UiExpr>> Lists { get; } = [];
	public Dictionary<string, List<UiElement>> Children { get; } = [];

	public UiExpr? E(string name) => Exprs.GetValueOrDefault(name);
	public string? S(string name) => Strings.GetValueOrDefault(name);
	public bool F(string name, bool fallback) => Flags.TryGetValue(name, out bool v) ? v : fallback;
	public List<UiElement> C(string name) => Children.GetValueOrDefault(name) ?? [];
}

// Step: a growing window's size changes in whole multiples of this many logical pixels (0: smoothly).
public sealed record UiTransition(string Kind, int Ms, string From, int Step = 0);

public sealed class UiWindow
{
	public required string Name { get; init; }
	public UiExpr? X { get; init; }
	public UiExpr? Y { get; init; }
	public UiExpr? W { get; init; }
	public UiExpr? H { get; init; }
	public string Anchor { get; init; } = "top_left";
	public string? RelativeTo { get; init; }
	public int Z { get; init; }
	public string Frame { get; init; } = "none";
	public UiExpr? Opacity { get; init; }
	public UiElement? Title { get; init; }
	public UiExpr[] Padding { get; init; } = [];
	public UiExpr? Visible { get; init; }
	public UiTransition? Open { get; init; }
	public UiTransition? Close { get; init; }
	public bool BlocksClicks { get; init; }
	public List<UiElement> Elements { get; } = [];
}

public sealed class UiScreenLayout
{
	public required string Name { get; init; }
	public required string Source { get; init; }
	public List<UiWindow> Windows { get; } = [];
	// Screen-wide numbers and switches the screen's code reads (`params` in the file), such as a stage's spacing.
	public Dictionary<string, UiExpr> Params { get; } = [];
	// What each control tag of a line does (`tags` in the file), by the tag's name.
	public Dictionary<string, UiTag> Tags { get; } = [];
	// How a status shows on the party's slots (`statuses` in the file), by status id; one left out keeps the plain look.
	public Dictionary<string, UiStatusLook> Statuses { get; } = [];
	// The title's start state before its menu (`start` in the file), and the menu's entries in order (`options`; null: the
	// built-in order).
	public bool Start { get; set; }
	public List<string>? Options { get; set; }
	// A title entry's `when`: shown only while it holds, on top of the entry's own rule.
	public Dictionary<string, UiExpr> OptionWhen { get; } = [];
	// The Equip page's places in order, each with its label's text key (`places`; null: the built-in six).
	public List<(string Slot, string? Label)>? Places { get; set; }
	// The status page's profile rows in order (`profile`; null: every one UiFields.ProfileKeys lists).
	public List<string>? Profile { get; set; }
	// The field menu's or the Extras menu's entries in order, each with its label's text key (`entries`; null: the built-in ones).
	public List<(string Page, string? Label)>? Entries { get; set; }
	// A field menu entry's `children`, by the entry's page: the choices its tab lists in place of its own page.
	public Dictionary<string, List<(string Page, string? Label)>> Children { get; } = [];
	// The menu entries and children with `immediate`: a one-row page that acts as soon as it opens, or a tab whose one
	// child opens in place of the list.
	public HashSet<string> Immediate { get; } = [];
	// A child's `when` (left out of the list while it does not hold) and `enabled` (listed dimmed, not opened, while it
	// does not hold), by `PARENT/CHILD`.
	public Dictionary<string, UiExpr> ChildWhen { get; } = [];
	public Dictionary<string, UiExpr> ChildEnabled { get; } = [];
	// The system page's rows in order (`rows`; null: UiFields.SystemDefault).
	public List<string>? Rows { get; set; }
	// A battle layout's command rows per step with `command_top` (`command_rows`: `top`, `member`; a step left out lists all).
	public List<string>? TopCommands { get; set; }
	public List<string>? MemberCommands { get; set; }
	// A vehicle's rows in a vehicle battle (`command_rows.vehicle`), with or without `command_top`.
	public List<string>? VehicleCommands { get; set; }

	public bool HasSlot(string slot) => Windows.Any(w => HasSlot(w.Elements, slot));

	// Whether any element's expression reads the field, so the screen's code can tell what the layout shows.
	public bool Reads(string field) => Windows.Any(w => Reads(w.Elements, field));

	public bool WindowReads(string window, string field) => Windows.Any(w => w.Name == window && Reads(w.Elements, field));

	private static bool Reads(IEnumerable<UiElement> elements, string field) =>
		elements.Any(e => e.Exprs.Values.Concat(e.Lists.Values.SelectMany(l => l)).Any(x => x.Names.Contains(field)) || e.Children.Values.Any(c => Reads(c, field)));

	private static bool HasSlot(IEnumerable<UiElement> elements, string slot) =>
		elements.Any(e => e.Type == "slot" && e.S("slot") == slot || e.Children.Values.Any(c => HasSlot(c, slot)));
}

// One entry of a `tags` table: the action (UiFields.TagActions), the id it uses instead of the tag's value, and the slot
// a shown panel takes. Values: the id for each tag value, and Default the id for a value it leaves out.
public sealed record UiTag(string Action, string? Id, string? Slot, IReadOnlyDictionary<string, string>? Values = null, string? Default = null)
{
	// Per value, in order, before the entry's own action: the first case whose value or whole-number range holds the tag's
	// value acts instead.
	public IReadOnlyList<UiTagCase> Cases { get; init; } = [];
}

// One case of a tag: the exact value or the range From to To (whole numbers, both ends in), and what it does: an action
// with its id, or Ids, the id for each value of the range in order from From.
public sealed record UiTagCase(string? Value, int From, int To, string Action, string? Id, IReadOnlyList<string>? Ids, string? Slot)
{
	public bool Holds(string value) => Value is not null ? Value == value
		: int.TryParse(value, System.Globalization.NumberStyles.AllowLeadingSign, System.Globalization.CultureInfo.InvariantCulture, out int n) && n >= From && n <= To;

	public string? IdFor(string value) => Ids is not null && int.TryParse(value, System.Globalization.CultureInfo.InvariantCulture, out int n) && n - From >= 0 && n - From < Ids.Count
		? Ids[n - From] : Id;
}

// A status on a party slot: the name's color while it is the highest-priority status with one (null: none), its icon
// shown or not, and its priority, which also orders the icons.
public sealed record UiStatusLook(string? Color, bool Icon, int Priority);

public sealed record UiTextStyle(UiExpr? Size, string? Color, TextRole? Role);

// Skin: which of the UI skin's frames a generated frame takes, `menu` (ui.frame) or `battle` (ui.battle_frame, else ui.frame).
public sealed record UiFrameStyle(string Type, string? Image, int Border, int Scale, string? Color, int Thickness, string Skin = "menu");

public sealed record UiCursorStyle(string Type, string? Image, int Size, string Color, int BlinkMs);

public sealed record UiScaling(string Mode, float SafeLeft, float SafeTop, float SafeRight, float SafeBottom, string Filter);

// styles.json: the logical resolution and how it fills the window, named colors, text styles, frames, cursors and timings.
public sealed class UiStyles
{
	public int Width { get; set; } = 1280;
	public int Height { get; set; } = 720;
	public UiScaling Scaling { get; set; } = new("fit", 0, 0, 0, 0, "auto");
	public Dictionary<string, Color> Colors { get; } = [];
	public Dictionary<string, int> Sizes { get; } = [];
	public Dictionary<string, UiTextStyle> TextStyles { get; } = [];
	public Dictionary<string, UiFrameStyle> Frames { get; } = [];
	public Dictionary<string, UiCursorStyle> Cursors { get; } = [];
	public Dictionary<string, int> Timings { get; } = [];
	public Dictionary<string, Dictionary<string, int>> TimingTables { get; } = [];
	public Dictionary<string, int> Params { get; } = [];

	public int Timing(string key, int fallback) => Timings.TryGetValue(key, out int v) ? v : fallback;

	public int Param(string key) => Params.TryGetValue(key, out int v) ? v : UiFields.StyleParams[key];
}

// A complete set of layouts: the styles and one layout per screen. The built-in set is the sample's own look; an asset
// directory's `ui/` files replace it file by file, each file that has a problem falling back to the built-in one.
public sealed class UiLayoutSet
{
	public const int Version = 1;
	public const string Directory = "ui";
	public const string StylesFile = "styles";

	public required UiStyles Styles { get; init; }
	public Dictionary<string, UiScreenLayout> Screens { get; } = [];
	// The files that came from the asset directory rather than the built-in set, by name.
	public HashSet<string> FromFiles { get; } = [];
	public bool IsDefault => FromFiles.Count == 0;

	public UiScreenLayout Screen(string name) => Screens.TryGetValue(name, out UiScreenLayout? l) ? l : Defaults.Screens[name];

	private static UiLayoutSet? _defaults;

	// Every built-in file's name (styles first) and its text, as embedded in the sample.
	public static IEnumerable<(string Name, string Text)> BuiltInFiles()
	{
		Assembly assembly = typeof(UiLayoutSet).Assembly;
		foreach (string resource in assembly.GetManifestResourceNames().Where(r => r.StartsWith("ui/", StringComparison.Ordinal) && r.EndsWith(".json", StringComparison.Ordinal))
			.OrderBy(r => r == "ui/styles.json" ? 0 : 1).ThenBy(r => r, StringComparer.Ordinal))
		{
			using Stream stream = assembly.GetManifestResourceStream(resource)!;
			using var reader = new StreamReader(stream);
			yield return (resource[3..^5], reader.ReadToEnd());
		}
	}

	public static UiLayoutSet Defaults
	{
		get
		{
			if (_defaults is null)
			{
				var problems = new List<AssetProblem>();
				_defaults = Parse(BuiltInFiles().ToDictionary(f => f.Name, f => f.Text), null, problems, builtIn: true);
				if (problems.Count > 0)
				{
					throw new InvalidOperationException("the built-in layouts have problems: " + string.Join("; ", problems));
				}
			}
			return _defaults;
		}
	}

	// The layouts of an asset directory: `files` maps each file name (without .json) present to its text; `required`
	// when the manifest names the directory, so a missing screen file is a problem.
	public static UiLayoutSet Load(string dir, bool required, List<AssetProblem> problems, IReadOnlySet<string>? statuses = null)
	{
		var texts = new Dictionary<string, string>();
		string folder = Path.Combine(dir, Directory);
		if (System.IO.Directory.Exists(folder))
		{
			foreach (string file in System.IO.Directory.GetFiles(folder, "*.json").Order(StringComparer.Ordinal))
			{
				texts[Path.GetFileNameWithoutExtension(file)] = File.ReadAllText(file);
			}
		}
		if (required)
		{
			foreach (string name in Defaults.Screens.Keys.Prepend(StylesFile).Where(n => !texts.ContainsKey(n)))
			{
				problems.Add(new AssetProblem($"{Directory}/{name}.json", "-", "missing file: the built-in layout stands in"));
			}
		}
		return Parse(texts, Defaults, problems, builtIn: false, statuses);
	}

	public static IEnumerable<string> FilesIn(string dir)
	{
		string folder = Path.Combine(dir, Directory);
		return System.IO.Directory.Exists(folder) ? System.IO.Directory.GetFiles(folder, "*.json").Select(f => Path.Combine(Directory, Path.GetFileName(f))) : [];
	}

	private static UiLayoutSet Parse(Dictionary<string, string> texts, UiLayoutSet? fallback, List<AssetProblem> problems, bool builtIn, IReadOnlySet<string>? statuses = null)
	{
		UiStyles styles = fallback?.Styles ?? new UiStyles();
		bool stylesFromFile = false;
		if (texts.TryGetValue(StylesFile, out string? stylesText))
		{
			int before = problems.Count;
			var reader = new UiReader($"{Directory}/{StylesFile}.json", problems, null, null);
			UiStyles? read = reader.Read(stylesText, root => reader.Styles(root));
			if (read is not null && problems.Count == before)
			{
				styles = read;
				stylesFromFile = !builtIn;
			}
			else if (fallback is not null)
			{
				problems.Add(new AssetProblem($"{Directory}/{StylesFile}.json", "-", "the built-in styles stand in"));
			}
		}
		var set = new UiLayoutSet { Styles = styles };
		if (stylesFromFile)
		{
			set.FromFiles.Add(StylesFile);
		}
		foreach ((string name, string text) in texts.Where(t => t.Key != StylesFile).OrderBy(t => t.Key, StringComparer.Ordinal))
		{
			string file = $"{Directory}/{name}.json";
			if (!UiFields.Screens.TryGetValue(name, out UiFields.ScreenFields? fields))
			{
				problems.Add(new AssetProblem(file, "-", $"unknown screen '{name}': {string.Join(", ", UiFields.Screens.Keys.Order(StringComparer.Ordinal))} or {StylesFile}"));
				continue;
			}
			int before = problems.Count;
			var reader = new UiReader(file, problems, styles, fields, statuses);
			UiScreenLayout? screen = reader.Read(text, root => reader.Screen(root, name, file));
			if (screen is not null && problems.Count == before)
			{
				set.Screens[name] = screen;
				if (!builtIn)
				{
					set.FromFiles.Add(name);
				}
			}
			else if (fallback is not null)
			{
				problems.Add(new AssetProblem(file, "-", "the built-in layout of this screen stands in"));
			}
		}
		if (fallback is not null)
		{
			foreach ((string name, UiScreenLayout screen) in fallback.Screens.Where(s => !set.Screens.ContainsKey(s.Key)))
			{
				set.Screens[name] = screen;
			}
		}
		return set;
	}
}

// Reads one layout file, reporting each problem as the manifest's reader does: file, entry, reason. `statuses` are the
// game's status ids a `statuses` table may name; null skips that check (the built-in files).
internal sealed class UiReader(string file, List<AssetProblem> problems, UiStyles? styles, UiFields.ScreenFields? fields, IReadOnlySet<string>? statuses = null)
{
	public static readonly string[] Anchors = ["top_left", "top", "top_right", "left", "center", "right", "bottom_left", "bottom", "bottom_right"];
	public static readonly string[] TransitionKinds = ["none", "fade", "grow", "grow_x", "grow_y", "slide"];
	public static readonly string[] Edges = ["left", "right", "top", "bottom"];
	public static readonly string[] Modes = ["fit", "integer", "stretch"];
	public static readonly string[] Filters = ["auto", "point", "linear"];
	public static readonly string[] FrameTypes = ["generated", "nine_slice", "fill", "outline", "none"];

	// Each element type's own properties: e an expression, c a color, s a string, b true or false, a a list of
	// expressions, p a list of points, l a list of elements.
	public static readonly Dictionary<string, string> ElementTypes = new()
	{
		["fill"] = "color:c",
		["outline"] = "color:c thickness:e",
		["box"] = "frame:s opacity:e",
		["plate"] = "key:s args:a value:e color:c size:e style:s",
		["text"] = "key:s args:a value:e format:s color:c size:e style:s align:s max_width:e ellipsis:b wrap:b lines:e line_height:e reveal:e scale:e",
		["bar"] = "value:e max:e color:c",
		["value_bar"] = "key:s args:a value:e max:e color:c gap:e bar_h:e",
		["image"] = "image:e tint:c",
		["pointer"] = "color:c size:e cursor:s",
		["rows"] = "items:e selected:e row_height:e size:e height:e enabled:e text:e first:e most:e per_column:e column_w:e highlight:c cursor:s cursor_dx:e cursor_dy:e selected_color:c text_color:c disabled_color:c",
		["list"] = "items:e state:e row_height:e size:e active:e enabled:e text:e fit:e row_w:e height:e view:b probe:b row:l highlight:c cursor:s cursor_dx:e cursor_dy:e selected_color:c text_color:c disabled_color:c",
		["hints"] = "key:s args:a value:e",
		["tabs"] = "items:e selected:e",
		["scroll_bar"] = "top:e rows:e count:e",
		["repeater"] = "items:e count:e layout:s dx:e dy:e columns:e slots:p template:l empty:l",
		["group"] = "elements:l",
		["slot"] = "slot:s",
		["role"] = "role:s",
	};

	public const string Common = "type name visible x y w h scaled alpha";

	// The fields each object of a styles or screen file takes, by its place (`{}` a map's value, `[]` a list's entry); the
	// check refuses any other, and `--capabilities` reports these.
	public static readonly Dictionary<string, string[]> Schema = new()
	{
		["styles"] = ["version", "resolution", "scaling", "colors", "sizes", "text_styles", "frames", "cursors", "timings", "params"],
		["styles.resolution"] = ["width", "height"],
		["styles.scaling"] = ["mode", "safe_area", "filter"],
		["styles.text_styles{}"] = ["size", "color", "role"],
		["styles.frames{}"] = ["type", "image", "border", "scale", "color", "thickness", "skin"],
		["styles.cursors{}"] = ["type", "image", "size", "color", "blink_ms"],
		["screen"] = ["version", "windows", "params", "tags", "statuses", "start", "options", "places", "profile", "entries", "rows", "command_rows"],
		["screen.places[]"] = ["slot", "label"],
		["screen.entries[]"] = ["page", "label", "children", "immediate"],
		["screen.entries[].children[]"] = ["page", "label", "immediate", "when", "enabled"],
		["screen.command_rows"] = ["top", "member", "vehicle"],
		["screen.options[]"] = ["id", "when"],
		["screen.tags{}"] = ["action", "id", "slot", "values", "default", "cases"],
		["screen.tags{}.cases[]"] = ["value", "from", "to", "action", "id", "ids", "slot"],
		["screen.statuses{}"] = ["color", "icon", "priority"],
		["transition"] = ["kind", "ms", "from", "step"],
		["window"] = ["name", "x", "y", "w", "h", "anchor", "relative_to", "z", "frame", "opacity", "title", "padding", "visible", "open", "close", "blocks_clicks", "elements"],
	};

	private void Problem(string entry, string reason) => problems.Add(new AssetProblem(file, entry, reason));

	public T? Read<T>(string text, Func<JsonElement, T?> read) where T : class
	{
		try
		{
			using JsonDocument doc = JsonDocument.Parse(text, new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip });
			return read(doc.RootElement);
		}
		catch (JsonException e)
		{
			Problem("-", "not valid JSON: " + e.Message);
			return null;
		}
	}

	private bool Object(JsonElement e, string entry)
	{
		if (e.ValueKind != JsonValueKind.Object)
		{
			Problem(entry, "must be an object");
			return false;
		}
		return true;
	}

	private void Known(JsonElement e, string entry, IEnumerable<string> known)
	{
		var set = known.ToHashSet();
		foreach (JsonProperty p in e.EnumerateObject().Where(p => !set.Contains(p.Name)))
		{
			Problem(entry, $"unknown field '{p.Name}'");
		}
	}

	private void Version(JsonElement root)
	{
		if (!root.TryGetProperty("version", out JsonElement v) || v.ValueKind != JsonValueKind.Number || !v.TryGetInt32(out int n) || n != UiLayoutSet.Version)
		{
			Problem("-", $"field 'version' must be {UiLayoutSet.Version}");
		}
	}

	private int Int(JsonElement e, string name, string entry, int fallback, int min, int max)
	{
		if (!e.TryGetProperty(name, out JsonElement v))
		{
			return fallback;
		}
		if (v.ValueKind != JsonValueKind.Number || !v.TryGetInt32(out int n) || n < min || n > max)
		{
			Problem(entry, $"field '{name}' must be a whole number from {min} to {max}");
			return fallback;
		}
		return n;
	}

	private string? Str(JsonElement e, string name, string entry, IReadOnlyList<string>? allowed = null)
	{
		if (!e.TryGetProperty(name, out JsonElement v))
		{
			return null;
		}
		if (v.ValueKind != JsonValueKind.String || v.GetString()!.Length == 0)
		{
			Problem(entry, $"field '{name}' must be a non-empty string");
			return null;
		}
		string s = v.GetString()!;
		if (allowed is not null && !allowed.Contains(s))
		{
			Problem(entry, $"field '{name}' must be one of {string.Join(", ", allowed)}");
			return null;
		}
		return s;
	}

	private static bool IsColorLiteral(string s) => s.StartsWith('#') && (s.Length == 7 || s.Length == 9) && s[1..].All(Uri.IsHexDigit);

	public static Color ParseHex(string s)
	{
		uint v = uint.Parse(s[1..], NumberStyles.HexNumber, CultureInfo.InvariantCulture);
		return s.Length == 7 ? new Color((int)(v >> 16) & 255, (int)(v >> 8) & 255, (int)v & 255) : new Color((int)(v >> 24) & 255, (int)(v >> 16) & 255, (int)(v >> 8) & 255, (int)v & 255);
	}

	// An expression from a string or a number, its names checked against the screen's fields and windows.
	private UiExpr? Expr(JsonElement v, string entry, string name, HashSet<string>? locals = null)
	{
		string source;
		if (v.ValueKind == JsonValueKind.Number)
		{
			source = v.GetRawText();
		}
		else if (v.ValueKind == JsonValueKind.String)
		{
			source = v.GetString()!;
		}
		else if (v.ValueKind is JsonValueKind.True or JsonValueKind.False)
		{
			source = v.ValueKind == JsonValueKind.True ? "true" : "false";
		}
		else
		{
			Problem(entry, $"field '{name}' must be an expression (a number or a string)");
			return null;
		}
		UiExpr? expr = UiExpr.Parse(source, out string? error);
		if (expr is null)
		{
			Problem(entry, $"field '{name}': {error}");
			return null;
		}
		if (fields is not null)
		{
			foreach (string n in expr.Names.Where(n => !Resolves(n, locals)))
			{
				Problem(entry, $"field '{name}' reads '{n}', which is not a field of the {fields.Screen} screen, a window or a variable");
			}
		}
		return expr;
	}

	// Names a screen's window may read: its fields, window edges, the built-in variables and the sizes.
	private bool Resolves(string name, HashSet<string>? locals)
	{
		string head = name.Split('.')[0];
		if (UiFields.Variables.Contains(name) || fields!.Has(name) || (locals?.Contains(name) ?? false))
		{
			return true;
		}
		if (styles is not null && name.StartsWith("size_", StringComparison.Ordinal) && styles.Sizes.ContainsKey(name[5..]))
		{
			return true;
		}
		if (name.Contains('.') && _windowNames.Contains(head) && UiFields.WindowEdges.Contains(name[(head.Length + 1)..]))
		{
			return true;
		}
		return false;
	}

	private readonly HashSet<string> _windowNames = [];

	private string? Color(JsonElement v, string entry, string name, out UiExpr? expr, HashSet<string>? locals = null)
	{
		expr = null;
		if (v.ValueKind != JsonValueKind.String)
		{
			Problem(entry, $"field '{name}' must be a color name, #rrggbb, #rrggbbaa or an expression giving one");
			return null;
		}
		string s = v.GetString()!;
		if (IsColorLiteral(s) || (styles?.Colors.ContainsKey(s) ?? false))
		{
			return s;
		}
		expr = Expr(v, entry, name, locals);
		return null;
	}

	public UiStyles? Styles(JsonElement root)
	{
		if (!Object(root, "-"))
		{
			return null;
		}
		Known(root, "-", Schema["styles"]);
		Version(root);
		var st = new UiStyles();
		if (root.TryGetProperty("resolution", out JsonElement res) && Object(res, "resolution"))
		{
			Known(res, "resolution", Schema["styles.resolution"]);
			st.Width = Int(res, "width", "resolution", 1280, 160, 7680);
			st.Height = Int(res, "height", "resolution", 720, 120, 4320);
		}
		if (root.TryGetProperty("scaling", out JsonElement sc) && Object(sc, "scaling"))
		{
			Known(sc, "scaling", Schema["styles.scaling"]);
			float[] safe = [0, 0, 0, 0];
			if (sc.TryGetProperty("safe_area", out JsonElement sa))
			{
				if (sa.ValueKind != JsonValueKind.Array || sa.GetArrayLength() != 4 || sa.EnumerateArray().Any(x => x.ValueKind != JsonValueKind.Number || x.GetDouble() < 0 || x.GetDouble() > 25))
				{
					Problem("scaling", "field 'safe_area' must be [left, top, right, bottom], each a percent of the window from 0 to 25");
				}
				else
				{
					safe = [.. sa.EnumerateArray().Select(x => (float)x.GetDouble())];
				}
			}
			st.Scaling = new UiScaling(Str(sc, "mode", "scaling", Modes) ?? "fit", safe[0], safe[1], safe[2], safe[3], Str(sc, "filter", "scaling", Filters) ?? "auto");
		}
		if (root.TryGetProperty("colors", out JsonElement colors) && Object(colors, "colors"))
		{
			foreach (JsonProperty p in colors.EnumerateObject())
			{
				if (p.Value.ValueKind != JsonValueKind.String || !IsColorLiteral(p.Value.GetString()!))
				{
					Problem($"colors.{p.Name}", "must be #rrggbb or #rrggbbaa");
					continue;
				}
				st.Colors[p.Name] = ParseHex(p.Value.GetString()!);
			}
		}
		if (root.TryGetProperty("sizes", out JsonElement sizes) && Object(sizes, "sizes"))
		{
			foreach (JsonProperty p in sizes.EnumerateObject())
			{
				st.Sizes[p.Name] = Int(sizes, p.Name, "sizes", 20, 1, 512);
			}
		}
		var reader = new UiReader(file, problems, st, null);
		if (root.TryGetProperty("text_styles", out JsonElement ts) && Object(ts, "text_styles"))
		{
			foreach (JsonProperty p in ts.EnumerateObject())
			{
				string entry = $"text_styles.{p.Name}";
				if (!Object(p.Value, entry))
				{
					continue;
				}
				Known(p.Value, entry, Schema["styles.text_styles{}"]);
				UiExpr? size = p.Value.TryGetProperty("size", out JsonElement sv) ? reader.Expr(sv, entry, "size") : null;
				string? color = p.Value.TryGetProperty("color", out JsonElement cv) ? reader.Color(cv, entry, "color", out _) : null;
				string? role = Str(p.Value, "role", entry, AssetManifest.RoleNames);
				st.TextStyles[p.Name] = new UiTextStyle(size, color, role is null ? null : Enum.Parse<TextRole>(role.Replace("_", ""), ignoreCase: true));
			}
		}
		if (root.TryGetProperty("frames", out JsonElement frames) && Object(frames, "frames"))
		{
			foreach (JsonProperty p in frames.EnumerateObject())
			{
				string entry = $"frames.{p.Name}";
				if (!Object(p.Value, entry))
				{
					continue;
				}
				Known(p.Value, entry, Schema["styles.frames{}"]);
				string type = Str(p.Value, "type", entry, FrameTypes) ?? "generated";
				string? image = Str(p.Value, "image", entry);
				if (type == "nine_slice" && image is null)
				{
					Problem(entry, "a nine_slice frame needs 'image'");
				}
				string? skin = Str(p.Value, "skin", entry, ["menu", "battle"]);
				if (skin is not null && type != "generated")
				{
					Problem(entry, "'skin' is for a generated frame only");
				}
				string? color = p.Value.TryGetProperty("color", out JsonElement cv) ? reader.Color(cv, entry, "color", out _) : null;
				st.Frames[p.Name] = new UiFrameStyle(type, image, Int(p.Value, "border", entry, 4, 1, 256), Int(p.Value, "scale", entry, 1, 1, 16), color, Int(p.Value, "thickness", entry, 2, 1, 64),
					skin ?? "menu");
			}
		}
		// The built-in battle layouts name it, so a styles file that leaves it out still draws their windows.
		st.Frames.TryAdd("battle", new UiFrameStyle("generated", null, 4, 1, null, 2, "battle"));
		if (root.TryGetProperty("cursors", out JsonElement cursors) && Object(cursors, "cursors"))
		{
			foreach (JsonProperty p in cursors.EnumerateObject())
			{
				string entry = $"cursors.{p.Name}";
				if (!Object(p.Value, entry))
				{
					continue;
				}
				Known(p.Value, entry, Schema["styles.cursors{}"]);
				string type = Str(p.Value, "type", entry, ["generated", "image"]) ?? "generated";
				string? image = Str(p.Value, "image", entry);
				if (type == "image" && image is null)
				{
					Problem(entry, "an image cursor needs 'image'");
				}
				string color = (p.Value.TryGetProperty("color", out JsonElement cv) ? reader.Color(cv, entry, "color", out _) : null) ?? "accent";
				st.Cursors[p.Name] = new UiCursorStyle(type, image, Int(p.Value, "size", entry, 12, 1, 256), color, Int(p.Value, "blink_ms", entry, 0, 0, 10000));
			}
		}
		if (root.TryGetProperty("timings", out JsonElement timings) && Object(timings, "timings"))
		{
			Known(timings, "timings", UiFields.Timings.Keys.Concat(UiFields.TimingTables.Keys));
			foreach (JsonProperty p in timings.EnumerateObject())
			{
				if (UiFields.TimingTables.TryGetValue(p.Name, out string[]? keys))
				{
					string entry = $"timings.{p.Name}";
					if (!Object(p.Value, entry))
					{
						continue;
					}
					if (keys.Length > 0)
					{
						Known(p.Value, entry, keys);
					}
					var table = new Dictionary<string, int>();
					foreach (JsonProperty k in p.Value.EnumerateObject())
					{
						if (keys.Length == 0 && k.Name.Length != 1)
						{
							Problem(entry, $"'{k.Name}' must be one character");
							continue;
						}
						table[k.Name] = Int(p.Value, k.Name, entry, 0, 0, 100000);
					}
					st.TimingTables[p.Name] = table;
				}
				else if (UiFields.Timings.ContainsKey(p.Name))
				{
					st.Timings[p.Name] = Int(timings, p.Name, "timings", 0, 0, 100000);
				}
			}
		}
		if (root.TryGetProperty("params", out JsonElement ps) && Object(ps, "params"))
		{
			Known(ps, "params", UiFields.StyleParams.Keys);
			foreach (JsonProperty p in ps.EnumerateObject().Where(p => UiFields.StyleParams.ContainsKey(p.Name)))
			{
				// The field's cell divides every field position.
				st.Params[p.Name] = p.Name switch
				{
					"field_cell" => Int(ps, p.Name, "params", Art.Tile, 8, 256),
					"dev_scale" => Int(ps, p.Name, "params", 100, 10, 100),
					"dev_draw" or "dev_scale_bitmap" => Int(ps, p.Name, "params", p.Name == "dev_draw" ? 1 : 0, 0, 1),
					"dev_badge_x" => Int(ps, p.Name, "params", -1, -1, 4096),
					_ => Int(ps, p.Name, "params", 0, 0, 4096),
				};
			}
		}
		foreach (string needed in new[] { "panel", "none" }.Where(n => !st.Frames.ContainsKey(n)))
		{
			Problem("frames", $"needs a frame named '{needed}'");
		}
		return st;
	}

	// Screen params that take one of two words, the first being what the screen does without the param.
	private static readonly Dictionary<string, (string A, string B, string What)> WordParams = new()
	{
		["click_actor"] = ("walk", "interact", "whether a click on an actor walks up to it and faces it, or also interacts"),
		["continue"] = ("newest", "choose", "whether Continue loads the newest save or opens the Load list"),
		["item_order"] = ("items_then_equipment", "equipment_then_items", "whether the Item list puts the bag's items or the worn pieces first"),
		["macro_flow"] = ("reset", "remember", "whether the menus open at their first row after a macro's round or on Macro and the macro played last"),
		["unusable"] = ("dim", "hide", "whether abilities not usable in the field show dimmed or not at all"),
	};

	public UiScreenLayout? Screen(JsonElement root, string name, string source)
	{
		if (!Object(root, "-"))
		{
			return null;
		}
		Known(root, "-", Schema["screen"]);
		Version(root);
		var screen = new UiScreenLayout { Name = name, Source = source };
		if (!root.TryGetProperty("windows", out JsonElement windows) || windows.ValueKind != JsonValueKind.Array)
		{
			Problem("-", "field 'windows' must be a list");
			return screen;
		}
		foreach (JsonElement w in windows.EnumerateArray())
		{
			if (w.ValueKind == JsonValueKind.Object && w.TryGetProperty("name", out JsonElement n) && n.ValueKind == JsonValueKind.String && !_windowNames.Add(n.GetString()!))
			{
				Problem($"windows.{n.GetString()}", "a second window of that name");
			}
		}
		foreach (string missing in fields!.Windows.Where(r => !_windowNames.Contains(r)))
		{
			Problem("windows", $"needs a window named '{missing}' (the screen's code places things by it)");
		}
		if (root.TryGetProperty("params", out JsonElement ps) && Object(ps, "params"))
		{
			foreach (JsonProperty p in ps.EnumerateObject())
			{
				if (!fields.Params.Contains(p.Name))
				{
					Problem($"params.{p.Name}", fields.Params.Count == 0 ? "the screen takes no params" : $"unknown param: {string.Join(", ", fields.Params)}");
				}
				else if (p.Name == "narration_when" && p.Value.ValueKind == JsonValueKind.String && p.Value.GetString() is "all" or "tagged")
				{
					screen.Params[p.Name] = UiExpr.Parse($"'{p.Value.GetString()}'", out _)!;
				}
				else if (p.Name == "narration_wait")
				{
					if (p.Value.ValueKind != JsonValueKind.String || p.Value.GetString() is not ("press" or "auto"))
					{
						Problem($"params.{p.Name}", "must be \"press\" or \"auto\": whether narration lines wait for a press");
					}
					else
					{
						screen.Params[p.Name] = UiExpr.Parse($"'{p.Value.GetString()}'", out _)!;
					}
				}
				else if (p.Name == "followers_arrive")
				{
					if (p.Value.ValueKind != JsonValueKind.String || p.Value.GetString() is not ("stacked" or "behind"))
					{
						Problem($"params.{p.Name}", "must be \"stacked\" or \"behind\": where followers stand when the party arrives");
					}
					else
					{
						screen.Params[p.Name] = UiExpr.Parse($"'{p.Value.GetString()}'", out _)!;
					}
				}
				else if (p.Name == "macro_failed")
				{
					if (p.Value.ValueKind != JsonValueKind.String || p.Value.GetString() is not ("automatic" or "attack" or "lose_turn"))
					{
						Problem($"params.{p.Name}", "must be \"automatic\", \"attack\" or \"lose_turn\": what a macro step that cannot run does");
					}
					else
					{
						screen.Params[p.Name] = UiExpr.Parse($"'{p.Value.GetString()}'", out _)!;
					}
				}
				else if (p.Name == "damage_numbers")
				{
					if (p.Value.ValueKind != JsonValueKind.String || p.Value.GetString() is not ("applied" or "rolled"))
					{
						Problem($"params.{p.Name}", "must be \"applied\" or \"rolled\": whether floating numbers show the change made or what the roll gave");
					}
					else
					{
						screen.Params[p.Name] = UiExpr.Parse($"'{p.Value.GetString()}'", out _)!;
					}
				}
				else if (WordParams.TryGetValue(p.Name, out (string A, string B, string What) words))
				{
					(string a, string b, string what) = words;
					if (p.Value.ValueKind != JsonValueKind.String || p.Value.GetString() != a && p.Value.GetString() != b)
					{
						Problem($"params.{p.Name}", $"must be \"{a}\" or \"{b}\": {what}");
					}
					else
					{
						screen.Params[p.Name] = UiExpr.Parse($"'{p.Value.GetString()}'", out _)!;
					}
				}
				else if (p.Name == "party_talk")
				{
					if (p.Value.ValueKind != JsonValueKind.String || p.Value.GetString() is not ("close" or "page" or "speakers"))
					{
						Problem($"params.{p.Name}", "must be \"close\", \"page\" or \"speakers\": whether party talk closes the menu, shows its lines on the page or steps through the speakers");
					}
					else
					{
						screen.Params[p.Name] = UiExpr.Parse($"'{p.Value.GetString()}'", out _)!;
					}
				}
				else if (p.Name == "command_axis")
				{
					if (p.Value.ValueKind != JsonValueKind.String || p.Value.GetString() is not ("row" or "column"))
					{
						Problem($"params.{p.Name}", "must be \"column\" or \"row\": the keys that move the command cursor");
					}
					else
					{
						screen.Params[p.Name] = UiExpr.Parse($"'{p.Value.GetString()}'", out _)!;
					}
				}
				else if (Expr(p.Value, $"params.{p.Name}", p.Name) is { } e)
				{
					screen.Params[p.Name] = e;
				}
			}
		}
		if (root.TryGetProperty("tags", out JsonElement tags) && Object(tags, "tags"))
		{
			if (!fields.Tags)
			{
				Problem("tags", "the screen takes no tags table");
			}
			else
			{
				Tags(tags, screen);
			}
		}
		if (root.TryGetProperty("statuses", out JsonElement looks) && Object(looks, "statuses"))
		{
			if (!fields.Statuses)
			{
				Problem("statuses", "the screen takes no statuses table");
			}
			else
			{
				Statuses(looks, screen);
			}
		}
		TitleMenu(root, screen);
		RowModels(root, screen);
		MenuEntries(root, screen);
		int index = 0;
		foreach (JsonElement w in windows.EnumerateArray())
		{
			if (Window(w, $"windows[{index++}]") is { } window)
			{
				screen.Windows.Add(window);
			}
		}
		CommandAxis(screen);
		if (fields.Screen == "menu_items" && screen.Params.TryGetValue("actions", out UiExpr? actions) && actions.Names.Count == 0 && actions.Eval(null!).Int != 0
			&& !screen.Reads("actions"))
		{
			Problem("params.actions", "no element draws the field 'actions', so the action list an item opens would not show");
		}
		if (fields.Screen == "menu_items" && screen.Params.TryGetValue("discard", out UiExpr? discard) && discard.Names.Count == 0 && discard.Eval(null!).Int != 0
			&& !screen.Reads("discard_text"))
		{
			Problem("params.discard", "no element draws the field 'discard_text', so Discard's count and confirm would not show");
		}
		if (fields.Screen == "menu_options" && screen.Params.TryGetValue("party_talk", out UiExpr? talk) && talk.Eval(null!).Text == "speakers"
			&& !screen.WindowReads("talk_speaker", "talk_text"))
		{
			Problem("params.party_talk", "no element of a window 'talk_speaker' draws the field 'talk_text', so the speakers' lines would not show");
		}
		foreach (UiWindow w in screen.Windows.Where(w => w.RelativeTo is not null && !_windowNames.Contains(w.RelativeTo)))
		{
			Problem($"windows.{w.Name}", $"field 'relative_to' names no window of this screen: '{w.RelativeTo}'");
		}
		return screen;
	}

	private void RowModels(JsonElement root, UiScreenLayout screen)
	{
		if (root.TryGetProperty("places", out JsonElement places))
		{
			if (fields!.Screen != "menu_equip")
			{
				Problem("places", "only the menu_equip screen takes 'places'");
			}
			else if (places.ValueKind != JsonValueKind.Array || places.GetArrayLength() == 0)
			{
				Problem("places", "must be a non-empty list of places");
			}
			else
			{
				screen.Places = [];
				int i = 0;
				foreach (JsonElement p in places.EnumerateArray())
				{
					string entry = $"places[{i++}]";
					if (!Object(p, entry))
					{
						continue;
					}
					Known(p, entry, Schema["screen.places[]"]);
					string? slot = Str(p, "slot", entry, UiFields.EquipPlaces);
					if (slot is null)
					{
						if (!p.TryGetProperty("slot", out _))
						{
							Problem(entry, "missing field 'slot'");
						}
					}
					else if (screen.Places.Any(x => x.Slot == slot))
					{
						Problem(entry, $"'{slot}' is listed twice");
					}
					else
					{
						screen.Places.Add((slot, Str(p, "label", entry)));
					}
				}
			}
		}
		if (root.TryGetProperty("profile", out JsonElement profile))
		{
			if (fields!.Screen != "menu_status")
			{
				Problem("profile", "only the menu_status screen takes 'profile'");
			}
			else if (profile.ValueKind != JsonValueKind.Array)
			{
				Problem("profile", "must be a list of profile rows");
			}
			else
			{
				screen.Profile = [];
				int i = 0;
				foreach (JsonElement k in profile.EnumerateArray())
				{
					string entry = $"profile[{i++}]";
					string? key = k.ValueKind == JsonValueKind.String ? k.GetString() : null;
					if (key is null || !UiFields.ProfileKeys.Contains(key))
					{
						Problem(entry, $"must be a profile row: {string.Join(", ", UiFields.ProfileKeys)}");
					}
					else if (!screen.Profile.Contains(key))
					{
						screen.Profile.Add(key);
					}
					else
					{
						Problem(entry, $"'{key}' is listed twice");
					}
				}
			}
		}
	}

	// A list of ids from `allowed`, none twice, at least one.
	private List<string>? IdList(JsonElement list, string entry, string[] allowed, string what)
	{
		if (list.ValueKind != JsonValueKind.Array || list.GetArrayLength() == 0)
		{
			Problem(entry, $"must be a non-empty list of {what}");
			return null;
		}
		var ids = new List<string>();
		int i = 0;
		foreach (JsonElement k in list.EnumerateArray())
		{
			string at = $"{entry}[{i++}]";
			string? id = k.ValueKind == JsonValueKind.String ? k.GetString() : null;
			if (id is null || !allowed.Contains(id))
			{
				Problem(at, $"must be one of {string.Join(", ", allowed)}");
			}
			else if (ids.Contains(id))
			{
				Problem(at, $"'{id}' is listed twice");
			}
			else
			{
				ids.Add(id);
			}
		}
		return ids;
	}

	// `entries` (menu, extras), `rows` (menu_system) and `command_rows` (the battle layouts).
	private void MenuEntries(JsonElement root, UiScreenLayout screen)
	{
		string name = fields!.Screen;
		if (root.TryGetProperty("entries", out JsonElement entries))
		{
			string[]? allowed = name switch { "menu" => UiFields.MenuPages, "extras" => UiFields.ExtrasEntries, _ => null };
			if (allowed is null)
			{
				Problem("entries", "only the menu and extras screens take 'entries'");
			}
			else if (entries.ValueKind != JsonValueKind.Array || entries.GetArrayLength() == 0)
			{
				Problem("entries", "must be a non-empty list of entries");
			}
			else
			{
				screen.Entries = [];
				int i = 0;
				foreach (JsonElement e in entries.EnumerateArray())
				{
					string entry = $"entries[{i++}]";
					if (!Object(e, entry))
					{
						continue;
					}
					Known(e, entry, Schema["screen.entries[]"]);
					string? page = Str(e, "page", entry, allowed);
					if (page is null)
					{
						if (!e.TryGetProperty("page", out _))
						{
							Problem(entry, "missing field 'page'");
						}
					}
					else if (screen.Entries.Any(x => x.Page == page))
					{
						Problem(entry, $"'{page}' is listed twice");
					}
					else
					{
						screen.Entries.Add((page, Str(e, "label", entry)));
						if (e.TryGetProperty("children", out JsonElement children))
						{
							MenuChildren(children, $"{entry}.children", page, screen);
						}
						ImmediateEntry(e, entry, page, screen, screen.Children.ContainsKey(page));
					}
				}
			}
		}
		if (root.TryGetProperty("rows", out JsonElement rows))
		{
			if (name != "menu_system")
			{
				Problem("rows", "only the menu_system screen takes 'rows'");
			}
			else
			{
				screen.Rows = IdList(rows, "rows", UiFields.SystemRows, "system rows");
			}
		}
		if (root.TryGetProperty("command_rows", out JsonElement commands))
		{
			if (!fields.Statuses)
			{
				Problem("command_rows", "only the battle screens take 'command_rows'");
			}
			else if (Object(commands, "command_rows"))
			{
				Known(commands, "command_rows", Schema["screen.command_rows"]);
				if (commands.TryGetProperty("top", out JsonElement top))
				{
					screen.TopCommands = IdList(top, "command_rows.top", UiFields.TopCommands, "top-level commands");
				}
				if (commands.TryGetProperty("member", out JsonElement member))
				{
					screen.MemberCommands = IdList(member, "command_rows.member", UiFields.MemberCommands, "member commands");
				}
				if (commands.TryGetProperty("vehicle", out JsonElement vehicle))
				{
					screen.VehicleCommands = IdList(vehicle, "command_rows.vehicle", UiFields.VehicleCommands, "vehicle commands");
				}
				bool topOn = screen.Params.TryGetValue("command_top", out UiExpr? on) && (on.Names.Count > 0 || on.Eval(null!).Int != 0);
				if (!topOn && (screen.TopCommands is not null || screen.MemberCommands is not null))
				{
					Problem("command_rows", "needs the param 'command_top' on: the rows are per step of the top-level command flow");
				}
			}
		}
	}

	private void MenuChildren(JsonElement children, string entry, string page, UiScreenLayout screen)
	{
		if (fields!.Screen != "menu")
		{
			Problem(entry, "only the menu screen's entries take 'children'");
			return;
		}
		if (children.ValueKind != JsonValueKind.Array || children.GetArrayLength() == 0)
		{
			Problem(entry, "must be a non-empty list of entries");
			return;
		}
		var list = new List<(string Page, string? Label)>();
		int i = 0;
		foreach (JsonElement c in children.EnumerateArray())
		{
			string at = $"{entry}[{i++}]";
			if (!Object(c, at))
			{
				continue;
			}
			Known(c, at, Schema["screen.entries[].children[]"]);
			string? child = Str(c, "page", at, UiFields.MenuPages);
			if (child is null)
			{
				if (!c.TryGetProperty("page", out _))
				{
					Problem(at, "missing field 'page'");
				}
			}
			else if (list.Any(x => x.Page == child))
			{
				Problem(at, $"'{child}' is listed twice");
			}
			else
			{
				list.Add((child, Str(c, "label", at)));
				ImmediateEntry(c, at, child, screen, false);
				if (c.TryGetProperty("when", out JsonElement when) && Expr(when, at, "when") is { } shown)
				{
					screen.ChildWhen[$"{page}/{child}"] = shown;
				}
				if (c.TryGetProperty("enabled", out JsonElement enabled) && Expr(enabled, at, "enabled") is { } usable)
				{
					screen.ChildEnabled[$"{page}/{child}"] = usable;
				}
			}
		}
		screen.Children[page] = list;
	}

	// `immediate` (a boolean): a page with one row that acts on opening (`party_talk`, `extras`), or an entry with children
	// whose single child opens at once.
	private void ImmediateEntry(JsonElement e, string entry, string page, UiScreenLayout screen, bool parent)
	{
		if (!e.TryGetProperty("immediate", out JsonElement value))
		{
			return;
		}
		if (value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
		{
			Problem(entry, "field 'immediate' must be true or false");
		}
		else if (fields!.Screen != "menu" || !parent && page is not ("party_talk" or "extras"))
		{
			Problem(entry, "'immediate' acts only on the field menu's party_talk and extras pages and on an entry with children");
		}
		else if (value.GetBoolean())
		{
			screen.Immediate.Add(page);
		}
	}

	// A battle layout that lays the menu's rows out across (a repeater over `menu_items` in a row or grid, or rows one to a
	// column) must say so with `command_axis: "row"`, or Left and Right would not move the cursor it shows.
	private void CommandAxis(UiScreenLayout screen)
	{
		if (!fields!.Statuses || screen.Params.TryGetValue("command_axis", out UiExpr? axis) && axis.Names.Count == 0 && axis.Eval(null!).Text == "row")
		{
			return;
		}
		static bool Across(UiElement e) => e.E("items")?.Names.Contains("menu_items") == true && (e.Type == "repeater" && (e.S("layout") == "grid" || e.S("layout") is null or "row" && e.E("dy") is null)
			|| e.Type == "rows" && e.E("per_column") is { Names.Count: 0 } per && per.Eval(null!).Int == 1);
		IEnumerable<UiElement> All(IEnumerable<UiElement> list) => list.SelectMany(e => All(e.Children.Values.SelectMany(c => c)).Prepend(e));
		foreach (UiWindow w in screen.Windows.Where(w => All(w.Elements).Any(Across)))
		{
			Problem($"windows.{w.Name}", "lays the menu's rows out across: set the param 'command_axis' to \"row\" so Left and Right move the cursor");
		}
	}

	private void TitleMenu(JsonElement root, UiScreenLayout screen)
	{
		bool hasStart = root.TryGetProperty("start", out JsonElement start), hasOptions = root.TryGetProperty("options", out JsonElement options);
		if ((hasStart || hasOptions) && !fields!.Title)
		{
			Problem(hasStart ? "start" : "options", "only the title screen takes 'start' and 'options'");
			return;
		}
		if (hasStart)
		{
			if (start.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
			{
				Problem("start", "must be true or false");
			}
			screen.Start = start.ValueKind == JsonValueKind.True;
		}
		if (!hasOptions)
		{
			return;
		}
		if (options.ValueKind != JsonValueKind.Array || options.GetArrayLength() == 0)
		{
			Problem("options", "must be a non-empty list of title entries");
			return;
		}
		screen.Options = [];
		int i = 0;
		foreach (JsonElement o in options.EnumerateArray())
		{
			string entry = $"options[{i++}]";
			string? id = o.ValueKind == JsonValueKind.String ? o.GetString() : null;
			if (o.ValueKind == JsonValueKind.Object)
			{
				Known(o, entry, Schema["screen.options[]"]);
				id = Str(o, "id", entry);
				if (id is not null && o.TryGetProperty("when", out JsonElement when) && Expr(when, entry, "when") is { } condition)
				{
					screen.OptionWhen[id] = condition;
				}
			}
			if (id is null || !UiFields.TitleEntries.Contains(id))
			{
				Problem(entry, $"must be a title entry: {string.Join(", ", UiFields.TitleEntries)}");
			}
			else if (screen.Options.Contains(id))
			{
				Problem(entry, $"'{id}' is listed twice");
			}
			else
			{
				screen.Options.Add(id);
			}
		}
	}

	private void Tags(JsonElement tags, UiScreenLayout screen)
	{
		foreach (JsonProperty p in tags.EnumerateObject())
		{
			string entry = $"tags.{p.Name}";
			if (p.Name.Length == 0 || !(char.IsAsciiLetter(p.Name[0]) || p.Name[0] == '_') || !p.Name.All(c => char.IsAsciiLetterOrDigit(c) || c == '_'))
			{
				Problem(entry, "a tag's name is letters, digits and '_', not starting with a digit");
				continue;
			}
			if (UiFields.BuiltInTags.Contains(p.Name))
			{
				Problem(entry, $"the sample acts on '{p.Name}' itself: {string.Join(", ", UiFields.BuiltInTags)} cannot be mapped");
				continue;
			}
			if (!Object(p.Value, entry))
			{
				continue;
			}
			Known(p.Value, entry, Schema["screen.tags{}"]);
			List<UiTagCase> cases = p.Value.TryGetProperty("cases", out JsonElement cs) ? TagCases(cs, entry + ".cases") : [];
			string? action = Str(p.Value, "action", entry, UiFields.TagActions);
			if (action is null)
			{
				if (!p.Value.TryGetProperty("action", out _))
				{
					if (cases.Count > 0)
					{
						screen.Tags[p.Name] = new UiTag("log", null, null) { Cases = cases };
						continue;
					}
					Problem(entry, "missing field 'action'");
				}
				continue;
			}
			string? slot = Str(p.Value, "slot", entry);
			if (slot is not null && action != "panel_show")
			{
				Problem(entry, "field 'slot' is for panel_show only");
			}
			string? id = Str(p.Value, "id", entry);
			Dictionary<string, string>? values = null;
			if (p.Value.TryGetProperty("values", out JsonElement vs) && Object(vs, entry + ".values"))
			{
				values = [];
				foreach (JsonProperty v in vs.EnumerateObject())
				{
					if (v.Value.ValueKind != JsonValueKind.String || v.Value.GetString()!.Length == 0)
					{
						Problem($"{entry}.values.{v.Name}", "must be a non-empty string: the id the tag's value stands for");
						continue;
					}
					values[v.Name] = v.Value.GetString()!;
				}
			}
			string? fallback = Str(p.Value, "default", entry);
			if ((values is not null || fallback is not null) && action is "music_stop" or "music_pause" or "music_resume" or "panel_clear" or "picture_clear" or "portrait_off" or "log")
			{
				Problem(entry, $"fields 'values' and 'default' are for an action that takes an id, not {action}");
			}
			if (id is not null && (values is not null || fallback is not null))
			{
				Problem(entry, "field 'id' leaves the tag's value unread: give 'values' and 'default' without it");
			}
			screen.Tags[p.Name] = new UiTag(action, id, slot, values, fallback) { Cases = cases };
		}
	}

	private static readonly string[] NoIdActions = ["music_stop", "music_pause", "music_resume", "panel_clear", "picture_clear", "portrait_off", "log"];

	private List<UiTagCase> TagCases(JsonElement list, string entry)
	{
		var cases = new List<UiTagCase>();
		if (list.ValueKind != JsonValueKind.Array)
		{
			Problem(entry, "must be a list of cases");
			return cases;
		}
		int i = 0;
		foreach (JsonElement c in list.EnumerateArray())
		{
			string ce = $"{entry}[{i++}]";
			if (!Object(c, ce))
			{
				continue;
			}
			Known(c, ce, Schema["screen.tags{}.cases[]"]);
			string? value = Str(c, "value", ce);
			bool ranged = c.TryGetProperty("from", out _) || c.TryGetProperty("to", out _);
			int from = Int(c, "from", ce, int.MinValue, -1000000, 1000000), to = Int(c, "to", ce, int.MaxValue, -1000000, 1000000);
			if ((value is null) == !ranged)
			{
				Problem(ce, "needs 'value' or a range ('from', 'to'), not both");
				continue;
			}
			if (from > to)
			{
				Problem(ce, $"'from' {from} is past 'to' {to}");
				continue;
			}
			string? action = Str(c, "action", ce, UiFields.TagActions);
			if (action is null)
			{
				if (!c.TryGetProperty("action", out _))
				{
					Problem(ce, "missing field 'action'");
				}
				continue;
			}
			string? slot = Str(c, "slot", ce);
			if (slot is not null && action != "panel_show")
			{
				Problem(ce, "field 'slot' is for panel_show only");
			}
			string? id = Str(c, "id", ce);
			List<string>? ids = null;
			if (c.TryGetProperty("ids", out JsonElement idList))
			{
				if (idList.ValueKind != JsonValueKind.Array || !ranged || idList.EnumerateArray().Any(x => x.ValueKind != JsonValueKind.String || x.GetString()!.Length == 0))
				{
					Problem(ce, "field 'ids' must be a list of non-empty strings, on a range");
				}
				else
				{
					ids = [.. idList.EnumerateArray().Select(x => x.GetString()!)];
				}
			}
			if ((id is not null || ids is not null) && NoIdActions.Contains(action))
			{
				Problem(ce, $"fields 'id' and 'ids' are for an action that takes an id, not {action}");
			}
			cases.Add(new UiTagCase(value, from, to, action, id, ids, slot));
		}
		return cases;
	}

	private void Statuses(JsonElement looks, UiScreenLayout screen)
	{
		foreach (JsonProperty p in looks.EnumerateObject())
		{
			string entry = $"statuses.{p.Name}";
			if (statuses is not null && (!statuses.Contains(p.Name) || p.Name == "defeated"))
			{
				Problem(entry, $"unknown status '{p.Name}': not a status of the game data the party's slots show");
				continue;
			}
			if (!Object(p.Value, entry))
			{
				continue;
			}
			Known(p.Value, entry, Schema["screen.statuses{}"]);
			string? color = Str(p.Value, "color", entry);
			if (color is not null && !IsColorLiteral(color) && !(styles?.Colors.ContainsKey(color) ?? false))
			{
				Problem(entry, $"field 'color' must be a color name of the styles, #rrggbb or #rrggbbaa: '{color}'");
			}
			string icon = Str(p.Value, "icon", entry, ["picture", "none"]) ?? "picture";
			screen.Statuses[p.Name] = new UiStatusLook(color, icon == "picture", Int(p.Value, "priority", entry, 0, 0, 100));
		}
	}

	private UiTransition? Transition(JsonElement e, string entry, string name)
	{
		if (!e.TryGetProperty(name, out JsonElement t))
		{
			return null;
		}
		string tEntry = $"{entry}.{name}";
		if (!Object(t, tEntry))
		{
			return null;
		}
		Known(t, tEntry, Schema["transition"]);
		string kind = Str(t, "kind", tEntry, TransitionKinds) ?? "none";
		int step = Int(t, "step", tEntry, 0, 0, 1024);
		if (t.TryGetProperty("step", out _) && !kind.StartsWith("grow", StringComparison.Ordinal))
		{
			Problem(tEntry, "field 'step' is for grow, grow_x and grow_y only");
		}
		return new UiTransition(kind, Int(t, "ms", tEntry, 200, 0, 10000), Str(t, "from", tEntry, Edges) ?? "bottom", step);
	}

	private UiWindow? Window(JsonElement w, string entry)
	{
		if (!Object(w, entry))
		{
			return null;
		}
		string? name = Str(w, "name", entry);
		if (name is null)
		{
			Problem(entry, "missing field 'name'");
			return null;
		}
		entry = $"windows.{name}";
		Known(w, entry, Schema["window"]);
		UiExpr? E(string field, HashSet<string>? locals = null) => w.TryGetProperty(field, out JsonElement v) ? Expr(v, entry, field, locals) : null;
		HashSet<string> rectLocals = ["w", "h"];
		string frame = Str(w, "frame", entry) ?? "none";
		if (styles is not null && !styles.Frames.ContainsKey(frame))
		{
			Problem(entry, $"field 'frame' names no frame of the styles: '{frame}'");
		}
		UiExpr[] padding = [];
		if (w.TryGetProperty("padding", out JsonElement pad))
		{
			if (pad.ValueKind != JsonValueKind.Array || pad.GetArrayLength() != 4)
			{
				Problem(entry, "field 'padding' must be [left, top, right, bottom]");
			}
			else
			{
				padding = [.. pad.EnumerateArray().Select(p => Expr(p, entry, "padding") ?? UiExpr.Constant(0))];
			}
		}
		bool blocks = false;
		if (w.TryGetProperty("blocks_clicks", out JsonElement bc))
		{
			if (bc.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
			{
				Problem(entry, "field 'blocks_clicks' must be true or false");
			}
			blocks = bc.ValueKind == JsonValueKind.True;
		}
		UiElement? title = null;
		if (w.TryGetProperty("title", out JsonElement t))
		{
			title = t.ValueKind == JsonValueKind.String ? new UiElement { Type = "plate", Strings = { ["key"] = t.GetString()! } } : Element(t, $"{entry}.title", "plate", null);
		}
		var window = new UiWindow
		{
			Name = name,
			W = E("w"),
			H = E("h"),
			X = E("x", rectLocals),
			Y = E("y", rectLocals),
			Anchor = Str(w, "anchor", entry, Anchors) ?? "top_left",
			RelativeTo = Str(w, "relative_to", entry),
			Z = Int(w, "z", entry, 0, -1000, 1000),
			Frame = frame,
			Opacity = E("opacity"),
			Title = title,
			Padding = padding,
			Visible = E("visible"),
			Open = Transition(w, entry, "open"),
			Close = Transition(w, entry, "close"),
			BlocksClicks = blocks,
		};
		if (w.TryGetProperty("elements", out JsonElement els))
		{
			window.Elements.AddRange(Elements(els, entry, null));
		}
		return window;
	}

	private List<UiElement> Elements(JsonElement list, string entry, HashSet<string>? locals)
	{
		var result = new List<UiElement>();
		if (list.ValueKind != JsonValueKind.Array)
		{
			Problem(entry, "elements must be a list");
			return result;
		}
		int i = 0;
		foreach (JsonElement e in list.EnumerateArray())
		{
			if (Element(e, $"{entry}.elements[{i++}]", null, locals) is { } el)
			{
				result.Add(el);
			}
		}
		return result;
	}

	private UiElement? Element(JsonElement e, string entry, string? forcedType, HashSet<string>? locals = null)
	{
		if (!Object(e, entry))
		{
			return null;
		}
		string? type = forcedType ?? Str(e, "type", entry);
		if (type is null || !ElementTypes.TryGetValue(type, out string? props))
		{
			Problem(entry, type is null ? "missing field 'type'" : $"unknown element type '{type}': {string.Join(", ", ElementTypes.Keys)}");
			return null;
		}
		string? elName = Str(e, "name", entry);
		if (elName is not null)
		{
			entry = $"{entry}({elName})";
		}
		Dictionary<string, char> kinds = props.Split(' ').Select(p => p.Split(':')).ToDictionary(p => p[0], p => p[1][0]);
		Known(e, entry, Common.Split(' ').Concat(kinds.Keys).Where(k => forcedType is null || k != "type"));
		var el = new UiElement { Type = type, Name = elName ?? "" };
		// Inside a list's row or a repeater's templates the item's own fields and the row variables are names too.
		HashSet<string> inner = [.. locals ?? [], .. UiFields.ItemVariables, .. fields?.ItemFields ?? [],
			.. fields?.Statuses == true ? statuses?.Where(id => id != "defeated").Select(id => UiFields.StatusFlag + id) ?? [] : []];
		foreach (JsonProperty p in e.EnumerateObject().Where(p => p.Name != "type" && p.Name != "name"))
		{
			char kind = p.Name switch { "visible" or "x" or "y" or "w" or "h" or "alpha" => 'e', "scaled" => 'b', _ => kinds.GetValueOrDefault(p.Name) };
			switch (kind)
			{
			case 'e':
				// A list's or rows' `enabled` and `text` are read for each item.
				if (Expr(p.Value, entry, p.Name, type is "list" or "rows" && p.Name is "enabled" or "text" ? inner : locals) is { } x)
				{
					el.Exprs[p.Name] = x;
				}
				break;
			case 'c':
				if (Color(p.Value, entry, p.Name, out UiExpr? ce, locals) is { } literal)
				{
					el.Strings[p.Name] = literal;
				}
				else if (ce is not null)
				{
					el.Exprs[p.Name] = ce;
				}
				break;
			case 's':
				if (Str(e, p.Name, entry) is { } s)
				{
					el.Strings[p.Name] = s;
				}
				break;
			case 'b':
				if (p.Value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
				{
					Problem(entry, $"field '{p.Name}' must be true or false");
				}
				el.Flags[p.Name] = p.Value.ValueKind == JsonValueKind.True;
				break;
			case 'a':
				if (p.Value.ValueKind != JsonValueKind.Array)
				{
					Problem(entry, $"field '{p.Name}' must be a list of expressions");
					break;
				}
				el.Lists[p.Name] = [.. p.Value.EnumerateArray().Select(a => Expr(a, entry, p.Name, locals) ?? UiExpr.Constant(0))];
				break;
			case 'p':
				if (p.Value.ValueKind != JsonValueKind.Array || p.Value.EnumerateArray().Any(a => a.ValueKind != JsonValueKind.Array || a.GetArrayLength() != 2))
				{
					Problem(entry, $"field '{p.Name}' must be a list of [x, y] points");
					break;
				}
				el.Lists[p.Name] = [.. p.Value.EnumerateArray().SelectMany(a => a.EnumerateArray()).Select(a => Expr(a, entry, p.Name, inner) ?? UiExpr.Constant(0))];
				break;
			case 'l':
				el.Children[p.Name] = Elements(p.Value, $"{entry}.{p.Name}", type is "list" or "repeater" ? inner : locals);
				break;
			}
		}
		Validate(el, entry);
		return el;
	}

	private void Validate(UiElement el, string entry)
	{
		void Need(params string[] names)
		{
			foreach (string n in names.Where(n => !el.Exprs.ContainsKey(n) && !el.Strings.ContainsKey(n) && !el.Children.ContainsKey(n)))
			{
				Problem(entry, $"missing field '{n}'");
			}
		}
		switch (el.Type)
		{
		case "fill" or "outline":
			Need("w", "h", "color");
			break;
		case "box" or "bar":
			Need("w", "h");
			break;
		case "image":
			Need("w", "h", "image");
			break;
		case "text" or "plate" or "hints" or "value_bar":
			if (!el.Strings.ContainsKey("key") && !el.Exprs.ContainsKey("value"))
			{
				Problem(entry, "needs 'key' (a text key) or 'value' (an expression)");
			}
			break;
		case "rows" or "list" or "tabs":
			Need("items");
			break;
		case "repeater":
			if (!el.Exprs.ContainsKey("items") && !el.Exprs.ContainsKey("count"))
			{
				Problem(entry, "needs 'items' or 'count'");
			}
			break;
		case "slot":
			if (el.S("slot") is { } slot && fields is not null && !fields.Slots.Contains(slot))
			{
				Problem(entry, fields.Slots.Count == 0 ? $"the {fields.Screen} screen has no slots" : $"unknown slot '{slot}': {string.Join(", ", fields.Slots)}");
			}
			Need("slot");
			break;
		case "role":
			if (el.S("role") is { } role && !AssetManifest.RoleNames.Contains(role))
			{
				Problem(entry, $"unknown role '{role}': {string.Join(", ", AssetManifest.RoleNames)}");
			}
			break;
		}
		foreach (string styleName in new[] { el.S("style") }.OfType<string>().Where(s => styles is not null && !styles.TextStyles.ContainsKey(s)))
		{
			Problem(entry, $"unknown text style '{styleName}'");
		}
		if (el.S("frame") is { } frame && styles is not null && !styles.Frames.ContainsKey(frame))
		{
			Problem(entry, $"unknown frame '{frame}'");
		}
		if (el.S("cursor") is { } cursor && styles is not null && !styles.Cursors.ContainsKey(cursor))
		{
			Problem(entry, $"unknown cursor '{cursor}'");
		}
		if (el.S("align") is { } align && align is not ("left" or "center" or "right"))
		{
			Problem(entry, "field 'align' must be left, center or right");
		}
		if (el.S("layout") is { } layout && layout is not ("row" or "column" or "grid" or "slots"))
		{
			Problem(entry, "field 'layout' must be row, column, grid or slots");
		}
	}
}
