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
}

// One entry of a `tags` table: the action (UiFields.TagActions), the id it uses instead of the tag's value, and the slot
// a shown panel takes. Values: the id for each tag value, and Default the id for a value it leaves out.
public sealed record UiTag(string Action, string? Id, string? Slot, IReadOnlyDictionary<string, string>? Values = null, string? Default = null);

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
	private static readonly string[] Anchors = ["top_left", "top", "top_right", "left", "center", "right", "bottom_left", "bottom", "bottom_right"];
	private static readonly string[] TransitionKinds = ["none", "fade", "grow", "grow_x", "grow_y", "slide"];
	private static readonly string[] Edges = ["left", "right", "top", "bottom"];
	private static readonly string[] Modes = ["fit", "integer", "stretch"];
	private static readonly string[] Filters = ["auto", "point", "linear"];
	private static readonly string[] FrameTypes = ["generated", "nine_slice", "fill", "outline", "none"];

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

	private const string Common = "type name visible x y w h scaled alpha";

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
		Known(root, "-", ["version", "resolution", "scaling", "colors", "sizes", "text_styles", "frames", "cursors", "timings", "params"]);
		Version(root);
		var st = new UiStyles();
		if (root.TryGetProperty("resolution", out JsonElement res) && Object(res, "resolution"))
		{
			Known(res, "resolution", ["width", "height"]);
			st.Width = Int(res, "width", "resolution", 1280, 160, 7680);
			st.Height = Int(res, "height", "resolution", 720, 120, 4320);
		}
		if (root.TryGetProperty("scaling", out JsonElement sc) && Object(sc, "scaling"))
		{
			Known(sc, "scaling", ["mode", "safe_area", "filter"]);
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
				Known(p.Value, entry, ["size", "color", "role"]);
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
				Known(p.Value, entry, ["type", "image", "border", "scale", "color", "thickness", "skin"]);
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
				Known(p.Value, entry, ["type", "image", "size", "color", "blink_ms"]);
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
				st.Params[p.Name] = p.Name == "field_cell" ? Int(ps, p.Name, "params", Art.Tile, 8, 256) : Int(ps, p.Name, "params", 0, 0, 4096);
			}
		}
		foreach (string needed in new[] { "panel", "none" }.Where(n => !st.Frames.ContainsKey(n)))
		{
			Problem("frames", $"needs a frame named '{needed}'");
		}
		return st;
	}

	public UiScreenLayout? Screen(JsonElement root, string name, string source)
	{
		if (!Object(root, "-"))
		{
			return null;
		}
		Known(root, "-", ["version", "windows", "params", "tags", "statuses"]);
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
		int index = 0;
		foreach (JsonElement w in windows.EnumerateArray())
		{
			if (Window(w, $"windows[{index++}]") is { } window)
			{
				screen.Windows.Add(window);
			}
		}
		foreach (UiWindow w in screen.Windows.Where(w => w.RelativeTo is not null && !_windowNames.Contains(w.RelativeTo)))
		{
			Problem($"windows.{w.Name}", $"field 'relative_to' names no window of this screen: '{w.RelativeTo}'");
		}
		return screen;
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
			Known(p.Value, entry, ["action", "id", "slot", "values", "default"]);
			string? action = Str(p.Value, "action", entry, UiFields.TagActions);
			if (action is null)
			{
				if (!p.Value.TryGetProperty("action", out _))
				{
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
			if ((values is not null || fallback is not null) && action is "music_stop" or "panel_clear" or "picture_clear" or "portrait_off" or "log")
			{
				Problem(entry, $"fields 'values' and 'default' are for an action that takes an id, not {action}");
			}
			if (id is not null && (values is not null || fallback is not null))
			{
				Problem(entry, "field 'id' leaves the tag's value unread: give 'values' and 'default' without it");
			}
			screen.Tags[p.Name] = new UiTag(action, id, slot, values, fallback);
		}
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
			Known(p.Value, entry, ["color", "icon", "priority"]);
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
		Known(t, tEntry, ["kind", "ms", "from", "step"]);
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
		Known(w, entry, ["name", "x", "y", "w", "h", "anchor", "relative_to", "z", "frame", "opacity", "title", "padding", "visible", "open", "close", "blocks_clicks", "elements"]);
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
		HashSet<string> inner = [.. locals ?? [], .. UiFields.ItemVariables, .. fields?.ItemFields ?? []];
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
