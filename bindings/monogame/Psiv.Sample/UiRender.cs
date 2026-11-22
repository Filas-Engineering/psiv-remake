using System.Globalization;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// Code drawing into a laid-out rectangle: a scene, a widget the layout language does not describe.
public delegate void UiSlot(int x, int y, int w, int h);

// The data a screen hands its layout for one frame: named values (numbers, strings, truth values, image handles,
// lists of strings or of item contexts, scroll lists) and slots. An item context falls back to its parent.
public sealed class UiContext(UiContext? parent = null)
{
	private readonly Dictionary<string, object?> _values = [];

	public UiContext Set(string name, object? value)
	{
		_values[name] = value;
		return this;
	}

	public bool TryGet(string name, out object? value)
	{
		if (_values.TryGetValue(name, out value))
		{
			return true;
		}
		if (parent is not null)
		{
			return parent.TryGet(name, out value);
		}
		value = null;
		return false;
	}

	public object? Get(string name) => TryGet(name, out object? v) ? v : null;
}

public readonly record struct UiDrawn(string Screen, string Window, Rectangle Rect);

// A named text, image or box element as drawn: a text's rectangle is its measured lines at the text size, a wrapped text's
// every line it shows, whatever has been revealed.
public readonly record struct UiDrawnElement(string Screen, string Window, string Element, Rectangle Rect);

// Draws a screen's layout: its windows in z order, each window's frame, title and elements, with the screen's data.
// The default layouts draw exactly what the screens drew before them, call for call.
public sealed class UiRenderer
{
	private readonly Dictionary<string, (bool Visible, int ChangedMs, Rectangle Rect)> _states = [];
	private readonly Dictionary<string, int> _lastDrawn = [];
	private readonly HashSet<string> _failed = [];
	private int _frame;

	public UiLayoutSet Set { get; set; } = UiLayoutSet.Defaults;
	public required Func<string, object[], string> Text { get; init; }
	public required Func<int, int> Px { get; init; }
	public required Action<IDrawSurface, TextRole> Role { get; init; }
	public required Action<string> Log { get; init; }
	public Func<string, int, int, NineSliceImage?>? SliceImage { get; set; }
	public Func<string, int>? Picture { get; set; }
	public Action<string, int>? Probe { get; set; }
	public int ClockMs { get; set; }
	// Transitions run this much faster (the battle's speed), in percent.
	public int TimeScale { get; set; } = 100;
	public List<UiDrawn> Drawn { get; } = [];
	public List<UiDrawnElement> DrawnElements { get; } = [];
	// Each frame style drawn this frame: how it drew (`generated`, `skin`, `nine_slice`, `fill`, `outline`) and how often.
	public Dictionary<string, (string How, int Count)> FramesDrawn { get; } = [];

	public UiStyles Styles => Set.Styles;

	public void BeginFrame()
	{
		_frame++;
		Drawn.Clear();
		DrawnElements.Clear();
		FramesDrawn.Clear();
	}

	public Color ColorOf(string name) => Styles.Colors.TryGetValue(name, out Color c) ? c : name.StartsWith('#') ? UiReader.ParseHex(name) : Color.Magenta;

	public int Timing(string key) => Styles.Timing(key, UiFields.Timings[key]);

	// A screen's param: the file's, else the built-in layout's, else `fallback`.
	public int Param(string screen, string name, UiContext ctx, int fallback)
	{
		UiExpr? e = Set.Screen(screen).Params.GetValueOrDefault(name) ?? UiLayoutSet.Defaults.Screen(screen).Params.GetValueOrDefault(name);
		return e is null ? fallback : (int)e.Eval(new Scope(this, ctx, null, null)).Int;
	}

	// A screen's param as text (a word such as `command_axis`'s): the file's, else the built-in layout's, else `fallback`.
	public string ParamText(string screen, string name, UiContext ctx, string fallback)
	{
		UiExpr? e = Set.Screen(screen).Params.GetValueOrDefault(name) ?? UiLayoutSet.Defaults.Screen(screen).Params.GetValueOrDefault(name);
		return e is null ? fallback : e.Eval(new Scope(this, ctx, null, null)).Text;
	}

	// A screen's param as a number with a fraction (an opacity, say): the file's, else the built-in layout's, else `fallback`.
	public double ParamNumber(string screen, string name, UiContext ctx, double fallback)
	{
		UiExpr? e = Set.Screen(screen).Params.GetValueOrDefault(name) ?? UiLayoutSet.Defaults.Screen(screen).Params.GetValueOrDefault(name);
		return e is null ? fallback : e.Eval(new Scope(this, ctx, null, null)).Number;
	}

	// Where a window of the screen lies for this context, null when it is hidden; for code that places by a window.
	public Rectangle? WindowRect(string screen, string window, IDrawSurface s, UiContext ctx)
	{
		var run = new Run(this, Set.Screen(screen), s, s, ctx);
		UiWindow? w = run.Layout.Windows.FirstOrDefault(x => x.Name == window);
		return w is null || (w.Visible is not null && !w.Visible.Eval(new Scope(this, ctx, null, run)).Truth) ? null : run.RectOf(w);
	}

	// The lines a window's named wrapped `text` element breaks its text into for this context, and the most it shows
	// (`lines`), as Draw would: the `role` elements and styled texts before it switch the font on the surfaces first, so
	// the caller puts the font back. Null when the window or the element is hidden or not there.
	public (List<(int Start, int Length)> Spans, int Lines)? WrapOf(string screen, string window, string element, IDrawSurface s, IDrawSurface t, UiContext ctx) =>
		new Run(this, Set.Screen(screen), s, t, ctx).WrapOf(window, element);

	// The window under a point among those drawn this frame that block clicks, for click hit-testing.
	public bool Blocks(int x, int y) => Drawn.Any(d => d.Rect.Contains(x, y) && Set.Screen(d.Screen).Windows.FirstOrDefault(w => w.Name == d.Window)?.BlocksClicks == true);

	// `s` draws at the logical resolution with text at 100%, `t` with the profile's text size.
	// The data each screen was last drawn with, for the `uifield` probe.
	public Dictionary<string, UiContext> LastData { get; } = [];

	public void Draw(string screen, IDrawSurface s, IDrawSurface t, UiContext ctx)
	{
		LastData[screen] = ctx;
		UiScreenLayout layout = Set.Screen(screen);
		bool fresh = !_lastDrawn.TryGetValue(screen, out int last) || last < _frame - 1;
		_lastDrawn[screen] = _frame;
		try
		{
			new Run(this, layout, s, t, ctx).Draw(fresh);
		}
		catch (Exception e) when (!_failed.Contains(screen) && !Set.IsDefault)
		{
			// A layout that still trips the code at run time is dropped for the built-in one, once, and said so.
			_failed.Add(screen);
			Log($"ui: {layout.Source}: {e.Message}; the built-in layout of this screen stands in");
			Set.Screens[screen] = UiLayoutSet.Defaults.Screens[screen];
		}
	}

	private sealed class Scope(UiRenderer r, UiContext ctx, Dictionary<string, UiValue>? locals, Run? run) : IUiScope
	{
		public bool TryGet(string name, out UiValue value)
		{
			if (locals is not null && locals.TryGetValue(name, out value))
			{
				return true;
			}
			if (ctx.TryGet(name, out object? v))
			{
				value = UiValue.Of(v);
				return true;
			}
			int dot = name.IndexOf('.');
			if (dot > 0 && run is not null && run.Layout.Windows.FirstOrDefault(w => w.Name == name[..dot]) is { } win)
			{
				Rectangle rect = run.RectOf(win);
				value = new UiValue((long)(name[(dot + 1)..] switch
				{
					"x" => rect.X,
					"y" => rect.Y,
					"w" => rect.Width,
					"h" => rect.Height,
					"right" => rect.Right,
					"bottom" => rect.Bottom,
					_ => 0,
				}));
				return true;
			}
			if (name.StartsWith("size_", StringComparison.Ordinal) && r.Styles.Sizes.TryGetValue(name[5..], out int size))
			{
				value = new UiValue((long)size);
				return true;
			}
			// A param, read outside a drawing run, takes the layout set's logical frame.
			if (name is "W" or "H")
			{
				value = new UiValue((long)(name == "W" ? run?.S.Width ?? r.Styles.Width : run?.S.Height ?? r.Styles.Height));
				return true;
			}
			value = UiValue.Zero;
			return false;
		}

		public int TextScaled(int value) => r.Px(value);

		public int Measure(string text, int size) => run?.T.Measure(text, size) ?? 0;

		public int MeasureUnscaled(string text, int size) => run?.S.Measure(text, size) ?? 0;
	}

	private sealed class Run(UiRenderer r, UiScreenLayout layout, IDrawSurface s, IDrawSurface t, UiContext ctx)
	{
		private readonly Dictionary<string, Rectangle> _rects = [];
		private readonly HashSet<string> _computing = [];
		// The window whose elements draw now, for the named elements' record.
		private string _window = "";

		public UiScreenLayout Layout => layout;
		public IDrawSurface S => s;
		public IDrawSurface T => t;

		private Scope Root(Dictionary<string, UiValue>? locals = null) => new(r, ctx, locals, this);

		public Rectangle RectOf(UiWindow w)
		{
			if (_rects.TryGetValue(w.Name, out Rectangle known))
			{
				return known;
			}
			if (!_computing.Add(w.Name))
			{
				// A window placed by itself through others: it lies at the parent's origin.
				return Rectangle.Empty;
			}
			Rectangle parent = w.RelativeTo is { } rel && layout.Windows.FirstOrDefault(x => x.Name == rel) is { } p ? RectOf(p) : new Rectangle(0, 0, s.Width, s.Height);
			var locals = new Dictionary<string, UiValue> { ["pw"] = new((long)parent.Width), ["ph"] = new((long)parent.Height) };
			int width = w.W?.Int(Root(locals)) ?? parent.Width;
			int height = w.H?.Int(Root(locals)) ?? parent.Height;
			locals["w"] = new((long)width);
			locals["h"] = new((long)height);
			int ox = w.X?.Int(Root(locals)) ?? 0;
			int oy = w.Y?.Int(Root(locals)) ?? 0;
			int x = w.Anchor switch
			{
				"top" or "center" or "bottom" => parent.X + (parent.Width - width) / 2 + ox,
				"top_right" or "right" or "bottom_right" => parent.X + parent.Width - width - ox,
				_ => parent.X + ox,
			};
			int y = w.Anchor switch
			{
				"left" or "center" or "right" => parent.Y + (parent.Height - height) / 2 + oy,
				"bottom_left" or "bottom" or "bottom_right" => parent.Y + parent.Height - height - oy,
				_ => parent.Y + oy,
			};
			_computing.Remove(w.Name);
			return _rects[w.Name] = new Rectangle(x, y, width, height);
		}

		public void Draw(bool fresh)
		{
			foreach (UiWindow w in layout.Windows.OrderBy(w => w.Z))
			{
				bool visible = w.Visible is null || w.Visible.Eval(Root()).Truth;
				string key = layout.Name + "/" + w.Name;
				float fade = 1f;
				bool growing = false;
				Rectangle rect = visible ? RectOf(w) : Rectangle.Empty;
				if (w.Open is not null || w.Close is not null)
				{
					bool had = r._states.TryGetValue(key, out var state) && !fresh;
					if (!had || state.Visible != visible)
					{
						state = (visible, had || visible ? r.ClockMs : int.MinValue / 2, visible ? rect : state.Rect);
					}
					else if (visible)
					{
						state.Rect = rect;
					}
					r._states[key] = state;
					UiTransition? tr = visible ? w.Open : w.Close;
					int ms = tr is null ? 0 : tr.Ms * 100 / Math.Max(1, r.TimeScale);
					float p = ms <= 0 || tr!.Kind == "none" ? 1f : Math.Clamp((r.ClockMs - state.ChangedMs) / (float)ms, 0f, 1f);
					if (!visible)
					{
						if (p >= 1f || tr is null || tr.Kind == "none")
						{
							continue;
						}
						// A closing window shows its frame only, going the way it came.
						Transform(tr, 1f - p, state.Rect, out Rectangle closing, out float closingFade);
						Frame(w, closing, closingFade);
						continue;
					}
					if (p < 1f && tr is not null)
					{
						Transform(tr, p, rect, out rect, out fade);
						growing = tr.Kind.StartsWith("grow", StringComparison.Ordinal);
					}
				}
				else if (!visible)
				{
					continue;
				}
				r.Drawn.Add(new UiDrawn(layout.Name, w.Name, rect));
				Frame(w, rect, fade);
				// A growing window shows its frame only, its title and elements once it has its size.
				if (growing)
				{
					continue;
				}
				if (w.Title is { } title && Str(title, Root()) is { Length: > 0 } text)
				{
					Plate(title, text, rect.X, rect.Y, 16, -14, fade, Root());
				}
				int pl = w.Padding.Length == 4 ? w.Padding[0].Int(Root()) : 0, pt = w.Padding.Length == 4 ? w.Padding[1].Int(Root()) : 0;
				int pr = w.Padding.Length == 4 ? w.Padding[2].Int(Root()) : 0, pb = w.Padding.Length == 4 ? w.Padding[3].Int(Root()) : 0;
				var locals = new Dictionary<string, UiValue> { ["cw"] = new((long)(rect.Width - pl - pr)), ["ch"] = new((long)(rect.Height - pt - pb)) };
				_window = w.Name;
				Elements(w.Elements, rect.X + pl, rect.Y + pt, fade, locals, ctx);
			}
		}

		public (List<(int Start, int Length)> Spans, int Lines)? WrapOf(string window, string element)
		{
			if (layout.Windows.FirstOrDefault(x => x.Name == window) is not { } w || (w.Visible is not null && !w.Visible.Eval(Root()).Truth))
			{
				return null;
			}
			Rectangle rect = RectOf(w);
			int Pad(int side) => w.Padding.Length == 4 ? w.Padding[side].Int(Root()) : 0;
			var locals = new Dictionary<string, UiValue> { ["cw"] = new((long)(rect.Width - Pad(0) - Pad(2))), ["ch"] = new((long)(rect.Height - Pad(1) - Pad(3))) };
			return FindWrap(w.Elements, locals, element);
		}

		private (List<(int Start, int Length)> Spans, int Lines)? FindWrap(List<UiElement> elements, Dictionary<string, UiValue> locals, string name)
		{
			foreach (UiElement e in elements)
			{
				var scope = new Scope(r, ctx, locals, this);
				if (e.E("visible") is { } visible && !visible.Eval(scope).Truth)
				{
					continue;
				}
				IDrawSurface surface = e.F("scaled", true) ? t : s;
				switch (e.Type)
				{
				case "role":
					r.Role(surface, Enum.Parse<TextRole>(e.S("role")!.Replace("_", ""), ignoreCase: true));
					break;
				case "group" when FindWrap(e.C("elements"), locals, name) is { } found:
					return found;
				case "text":
					if (e.S("style") is { } styleName && r.Styles.TextStyles.TryGetValue(styleName, out UiTextStyle? ts) && ts.Role is { } role)
					{
						r.Role(surface, role);
					}
					if (e.Name == name && e.F("wrap", false) && Str(e, scope) is { } text)
					{
						int width = e.E("max_width")?.Int(scope) ?? e.E("w")?.Int(scope) ?? 1;
						return (Style.WrapSpans(surface, text, width, SizeProp(e, scope, Style.Body)), e.E("lines")?.Int(scope) ?? int.MaxValue);
					}
					break;
				}
			}
			return null;
		}

		private static void Transform(UiTransition tr, float p, Rectangle rect, out Rectangle shown, out float fade)
		{
			fade = 1f;
			shown = rect;
			switch (tr.Kind)
			{
			case "fade":
				fade = p;
				break;
			case "grow" or "grow_x" or "grow_y":
				int gw = tr.Kind == "grow_y" ? rect.Width : Math.Max(1, (int)(rect.Width * p)), gh = tr.Kind == "grow_x" ? rect.Height : Math.Max(1, (int)(rect.Height * p));
				if (tr.Step > 0)
				{
					// Whole steps only, at least one, the full size never past.
					gw = tr.Kind == "grow_y" ? gw : Math.Min(rect.Width, Math.Max(tr.Step, gw / tr.Step * tr.Step));
					gh = tr.Kind == "grow_x" ? gh : Math.Min(rect.Height, Math.Max(tr.Step, gh / tr.Step * tr.Step));
				}
				shown = new Rectangle(rect.X + (rect.Width - gw) / 2, rect.Y + (rect.Height - gh) / 2, gw, gh);
				break;
			case "slide":
				int d = (int)((1f - p) * (tr.From is "left" or "right" ? rect.Width + rect.X : rect.Height + rect.Y));
				shown = tr.From switch
				{
					"left" => new Rectangle(rect.X - d, rect.Y, rect.Width, rect.Height),
					"right" => new Rectangle(rect.X + d, rect.Y, rect.Width, rect.Height),
					"top" => new Rectangle(rect.X, rect.Y - d, rect.Width, rect.Height),
					_ => new Rectangle(rect.X, rect.Y + d, rect.Width, rect.Height),
				};
				break;
			}
		}

		private void Frame(UiWindow w, Rectangle rect, float fade)
		{
			float opacity = (float)(w.Opacity?.Eval(Root()).Number ?? 1.0) * fade;
			FrameStyle(w.Frame, t, rect, opacity);
		}

		private void FrameStyle(string name, IDrawSurface surface, Rectangle rect, float opacity)
		{
			if (!r.Styles.Frames.TryGetValue(name, out UiFrameStyle? f))
			{
				return;
			}
			NineSliceImage? slice = f.Type == "nine_slice" ? r.SliceImage?.Invoke(f.Image!, f.Border, f.Scale) : null;
			if (f.Type != "none")
			{
				string how = f.Type is "generated" or "nine_slice" && slice is null ? Style.Skin?.Frame is null ? "generated" : "skin" : f.Type;
				r.FramesDrawn[name] = (how, r.FramesDrawn.GetValueOrDefault(name).Count + 1);
			}
			switch (f.Type)
			{
			case "generated" when f.Skin == "battle" && Style.Skin?.BattleFrame is { } battle:
				Style.NineSlice(surface, battle, rect.X, rect.Y, rect.Width, rect.Height, Color.White * opacity);
				break;
			case "generated":
				if (opacity == 1f)
				{
					Style.Box(surface, rect.X, rect.Y, rect.Width, rect.Height);
				}
				else
				{
					Style.Box(surface, rect.X, rect.Y, rect.Width, rect.Height, opacity);
				}
				break;
			case "nine_slice" when slice is not null:
				Style.NineSlice(surface, slice, rect.X, rect.Y, rect.Width, rect.Height, Color.White * opacity);
				break;
			case "nine_slice":
				Style.Box(surface, rect.X, rect.Y, rect.Width, rect.Height, opacity);
				break;
			case "fill":
				surface.Fill(rect.X, rect.Y, rect.Width, rect.Height, Fade(r.ColorOf(f.Color ?? "panel"), opacity));
				break;
			case "outline":
				Style.Frame(surface, rect.X, rect.Y, rect.Width, rect.Height, Fade(r.ColorOf(f.Color ?? "panel_edge"), opacity), f.Thickness);
				break;
			}
		}

		private static Color Fade(Color c, float fade) => fade == 1f ? c : c * fade;

		private Color ColorProp(UiElement e, string name, string fallback, Scope scope)
		{
			if (e.Strings.TryGetValue(name, out string? literal))
			{
				return r.ColorOf(literal);
			}
			if (e.Exprs.TryGetValue(name, out UiExpr? expr))
			{
				return r.ColorOf(expr.Eval(scope).Text);
			}
			if (e.S("style") is { } style && r.Styles.TextStyles.TryGetValue(style, out UiTextStyle? ts) && ts.Color is not null)
			{
				return r.ColorOf(ts.Color);
			}
			return r.ColorOf(fallback);
		}

		private int SizeProp(UiElement e, Scope scope, int fallback)
		{
			int size = e.E("size")?.Int(scope) ?? (e.S("style") is { } style && r.Styles.TextStyles.TryGetValue(style, out UiTextStyle? ts) && ts.Size is not null ? ts.Size.Int(scope) : fallback);
			return e.E("scale") is { } scale ? (int)Math.Round(size * scale.Eval(scope).Number) : size;
		}

		private string? Str(UiElement e, Scope scope)
		{
			if (e.S("key") is { } key)
			{
				return r.Text(key, [.. (e.Lists.GetValueOrDefault("args") ?? []).Select(a => (object)Arg(a.Eval(scope)))]);
			}
			if (e.E("value") is { } value)
			{
				UiValue v = value.Eval(scope);
				return e.S("format") is { } format && v.IsNumber
					? (v.Raw is long l ? l.ToString(format, CultureInfo.InvariantCulture) : v.Number.ToString(format, CultureInfo.InvariantCulture))
					: v.Text;
			}
			return null;
		}

		private static string Arg(UiValue v) => v.Text;

		private void Plate(UiElement e, string text, int ox, int oy, int dx, int dy, float fade, Scope scope)
		{
			IDrawSurface surface = e.F("scaled", true) ? t : s;
			int x = e.E("x")?.Int(scope) ?? dx, y = e.E("y")?.Int(scope) ?? dy;
			Color color = ColorProp(e, "color", "accent", scope);
			int size = SizeProp(e, scope, Style.Small);
			Style.Plate(surface, text, ox + x, oy + y, Fade(color, fade), size);
		}

		private static IEnumerable<object?> Items(object? raw) => raw switch
		{
			null => [],
			string => [raw],
			System.Collections.IEnumerable list => list.Cast<object?>(),
			_ => [],
		};

		private UiContext ItemContext(object? item, int i, UiContext parent)
		{
			if (item is UiContext c)
			{
				return c;
			}
			return new UiContext(parent).Set("item", item).Set("text", item?.ToString() ?? "");
		}

		private string RowText(UiElement e, object? item, int i, UiContext itemCtx, Dictionary<string, UiValue> locals)
		{
			if (e.E("text") is { } text)
			{
				return text.Eval(new Scope(r, itemCtx, locals, this)).Text;
			}
			return item switch
			{
				string str => str,
				UiContext c => c.Get("text")?.ToString() ?? "",
				_ => item?.ToString() ?? "",
			};
		}

		private bool RowEnabled(UiElement e, UiContext itemCtx, Dictionary<string, UiValue> locals) => e.E("enabled")?.Eval(new Scope(r, itemCtx, locals, this)).Truth ?? true;

		private void Elements(List<UiElement> elements, int ox, int oy, float fade, Dictionary<string, UiValue> locals, UiContext context)
		{
			foreach (UiElement e in elements)
			{
				var scope = new Scope(r, context, locals, this);
				if (e.E("visible") is { } visible && !visible.Eval(scope).Truth)
				{
					continue;
				}
				Element(e, ox, oy, fade, locals, context, scope);
			}
		}

		private void Element(UiElement e, int ox, int oy, float fade, Dictionary<string, UiValue> locals, UiContext context, Scope scope)
		{
			IDrawSurface surface = e.F("scaled", true) ? t : s;
			int X() => ox + (e.E("x")?.Int(scope) ?? 0);
			int Y() => oy + (e.E("y")?.Int(scope) ?? 0);
			int Wd() => e.E("w")?.Int(scope) ?? (int)locals.GetValueOrDefault("cw", UiValue.Zero).Int;
			int Ht() => e.E("h")?.Int(scope) ?? (int)locals.GetValueOrDefault("ch", UiValue.Zero).Int;
			float alpha = (float)(e.E("alpha")?.Eval(scope).Number ?? 1.0) * fade;
			Color C(string name, string fallback) => Fade(ColorProp(e, name, fallback, scope), alpha);
			switch (e.Type)
			{
			case "fill":
				surface.Fill(X(), Y(), Wd(), Ht(), C("color", "panel"));
				break;
			case "outline":
				Style.Frame(surface, X(), Y(), Wd(), Ht(), C("color", "panel_edge"), e.E("thickness")?.Int(scope) ?? 2);
				break;
			case "box":
				var box = new Rectangle(X(), Y(), Wd(), Ht());
				FrameStyle(e.S("frame") ?? "panel", surface, box, (float)(e.E("opacity")?.Eval(scope).Number ?? 1.0) * fade);
				Record(e, box);
				break;
			case "plate":
				if (Str(e, scope) is { } plate)
				{
					Plate(e, plate, ox, oy, 0, 0, alpha, scope);
				}
				break;
			case "text":
				Text(e, surface, ox, oy, alpha, locals, scope);
				break;
			case "bar":
				Style.Bar(surface, X(), Y(), Wd(), Ht(), e.E("value")?.Int(scope) ?? 0, e.E("max")?.Int(scope) ?? 0, C("color", "good"));
				break;
			case "value_bar":
				Widgets.ValueBar(surface, X(), Y(), Wd(), r.Text(e.S("key") ?? "", []), e.E("value")?.Int(scope) ?? 0, e.E("max")?.Int(scope) ?? 0, C("color", "good"),
					e.E("gap")?.Int(scope) ?? 22, e.E("bar_h")?.Int(scope) ?? 10);
				break;
			case "image":
				int image = e.E("image")!.Int(scope);
				if (image >= 0)
				{
					surface.Image(image, X(), Y(), Wd(), Ht(), C("tint", "white"));
					Record(e, new Rectangle(X(), Y(), Wd(), Ht()));
				}
				break;
			case "pointer":
				Pointer(e, surface, X(), Y(), C("color", "accent"), e.E("size")?.Int(scope) ?? 12);
				break;
			case "hints":
				Widgets.Hints(surface, X(), Y(), Wd(), Str(e, scope) ?? "");
				break;
			case "tabs":
				Widgets.Tabs(surface, X(), Y(), Wd(), [.. Items(e.E("items")!.Eval(scope).Raw).Select(i => i?.ToString() ?? "")], e.E("selected")?.Int(scope) ?? 0, r.Px);
				break;
			case "scroll_bar":
				Widgets.ScrollBar(surface, X(), Y(), Ht(), e.E("top")?.Int(scope) ?? 0, e.E("rows")?.Int(scope) ?? 1, Math.Max(1, e.E("count")?.Int(scope) ?? 1));
				break;
			case "rows":
				Rows(e, surface, X(), Y(), Wd(), alpha, locals, context, scope);
				break;
			case "list":
				List(e, surface, X(), Y(), Wd(), Ht(), alpha, locals, context, scope);
				break;
			case "repeater":
				Repeater(e, ox + (e.E("x")?.Int(scope) ?? 0), oy + (e.E("y")?.Int(scope) ?? 0), fade, locals, context, scope);
				break;
			case "group":
				Elements(e.C("elements"), X(), Y(), alpha, locals, context);
				break;
			case "slot":
				if (context.Get(e.S("slot")!) is UiSlot slot)
				{
					slot(X(), Y(), Wd(), Ht());
				}
				break;
			case "role":
				r.Role(surface, Enum.Parse<TextRole>(e.S("role")!.Replace("_", ""), ignoreCase: true));
				break;
			}
		}

		private void Pointer(UiElement e, IDrawSurface surface, int x, int y, Color color, int size)
		{
			UiCursorStyle? cursor = e.S("cursor") is { } name ? r.Styles.Cursors.GetValueOrDefault(name) : null;
			if (cursor is not null && cursor.BlinkMs > 0 && r.ClockMs / cursor.BlinkMs % 2 == 1)
			{
				return;
			}
			if (cursor is { Type: "image", Image: { } file } && r.Picture?.Invoke(file) is int handle and >= 0)
			{
				surface.Image(handle, x, y, size, size, Color.White);
				return;
			}
			Style.Pointer(surface, x, y, cursor is null ? color : r.ColorOf(cursor.Color), cursor?.Size ?? size);
		}

		private void Text(UiElement e, IDrawSurface surface, int ox, int oy, float alpha, Dictionary<string, UiValue> locals, Scope scope)
		{
			string? text = Str(e, scope);
			if (text is null)
			{
				return;
			}
			int size = SizeProp(e, scope, Style.Body);
			Color color = Fade(ColorProp(e, "color", "text", scope), alpha);
			if (e.S("style") is { } styleName && r.Styles.TextStyles.TryGetValue(styleName, out UiTextStyle? ts) && ts.Role is { } role)
			{
				r.Role(surface, role);
			}
			if (e.E("max_width") is { } maxWidth && !e.F("wrap", false))
			{
				int mw = maxWidth.Int(scope);
				text = e.F("ellipsis", true) ? Style.Fit(surface, text, mw, size) : Cut(surface, text, mw, size);
			}
			if (e.F("wrap", false))
			{
				int width = e.E("max_width")?.Int(scope) ?? e.E("w")?.Int(scope) ?? 1;
				int most = e.E("lines")?.Int(scope) ?? int.MaxValue;
				int step = e.E("line_height")?.Int(scope) ?? r.Px(r.Styles.Sizes.GetValueOrDefault("line", Style.LineHeight));
				List<(int Start, int Length)> spans = Style.WrapSpans(surface, text, width, size);
				int reveal = e.E("reveal")?.Int(scope) ?? text.Length;
				bool all = reveal >= text.Length;
				r.Probe?.Invoke("wrapped", Math.Min(spans.Count, most));
				if (r.Probe is not null)
				{
					// Each shown line's width after the count, measured with the font and size that draw it.
					foreach ((int start, int length) in spans.Take(most))
					{
						r.Probe("wrapped_width", surface.Measure(text.Substring(start, length), size));
					}
				}
				int ly = oy + (e.E("y")?.Int(scope) ?? 0);
				int lx = ox + (e.E("x")?.Int(scope) ?? 0);
				if (e.Name.Length > 0 && spans.Count > 0)
				{
					int shown = Math.Min(spans.Count, most);
					Record(e, new Rectangle(lx, ly, spans.Take(most).Max(l => surface.Measure(text.Substring(l.Start, l.Length), size)), (shown - 1) * step + TextHeight(e, size)));
				}
				foreach ((int start, int length) in spans.Take(most))
				{
					if (reveal <= start && !all)
					{
						break;
					}
					string line = text.Substring(start, length);
					surface.Text(all ? line : line[..Math.Min(line.Length, reveal - start)], lx, ly, color, size);
					ly += step;
				}
				return;
			}
			locals["tw"] = new UiValue((long)surface.Measure(text, size));
			int x = ox + (e.E("x")?.Int(scope) ?? 0);
			switch (e.S("align"))
			{
			case "center":
				x += ((e.E("w")?.Int(scope) ?? 0) - (int)locals["tw"].Int) / 2;
				break;
			case "right":
				x += (e.E("w")?.Int(scope) ?? 0) - (int)locals["tw"].Int;
				break;
			}
			surface.Text(text, x, oy + (e.E("y")?.Int(scope) ?? 0), color, size);
			Record(e, new Rectangle(x, oy + (e.E("y")?.Int(scope) ?? 0), (int)locals["tw"].Int, TextHeight(e, size)));
			locals.Remove("tw");
		}

		private int TextHeight(UiElement e, int size) => e.F("scaled", true) ? r.Px(size) : size;

		private void Record(UiElement e, Rectangle rect)
		{
			if (e.Name.Length > 0)
			{
				r.DrawnElements.Add(new UiDrawnElement(layout.Name, _window, e.Name, rect));
			}
		}

		private static string Cut(IDrawSurface s, string text, int width, int size)
		{
			while (text.Length > 0 && s.Measure(text, size) > width)
			{
				text = text[..^1];
			}
			return text;
		}

		// One menu row as Style.ListRow draws it, or with the element's own highlight, cursor and colors.
		private void Row(UiElement e, IDrawSurface surface, string text, int x, int y, int width, bool selected, bool enabled, int size, int height, float alpha, Scope scope)
		{
			bool styled = e.Strings.Keys.Concat(e.Exprs.Keys).Any(k => k is "highlight" or "cursor" or "cursor_dx" or "cursor_dy" or "selected_color" or "text_color" or "disabled_color");
			if (!styled && alpha == 1f)
			{
				Style.ListRow(surface, text, x, y, width, selected, enabled, size, height);
				return;
			}
			if (selected)
			{
				height = height > 0 ? height : size + 8;
				surface.Fill(x - 6, y - 3, width, height, Fade(ColorProp(e, "highlight", "row", scope), alpha));
				Pointer(e, surface, x - 4 + (e.E("cursor_dx")?.Int(scope) ?? 0), y + (height - 8) / 2 - 6 + (e.E("cursor_dy")?.Int(scope) ?? 0), Fade(r.ColorOf("accent"), alpha), 12);
			}
			Color color = !enabled ? ColorProp(e, "disabled_color", "dim", scope) : selected ? ColorProp(e, "selected_color", "accent", scope) : ColorProp(e, "text_color", "text", scope);
			surface.Text(text, x + 14, y, Fade(color, alpha), size);
		}

		private void Rows(UiElement e, IDrawSurface surface, int x, int y, int w, float alpha, Dictionary<string, UiValue> locals, UiContext context, Scope scope)
		{
			List<object?> items = [.. Items(e.E("items")!.Eval(scope).Raw)];
			int selected = e.E("selected")?.Int(scope) ?? -1;
			int size = e.E("size")?.Int(scope) ?? Style.Body;
			int rowH = e.E("row_height")?.Int(scope) ?? r.Px(size + 10);
			int height = e.E("height")?.Int(scope) ?? 0;
			// A page of the rows: from `first`, at most `most`, filling columns of `per_column` rows `column_w` apart.
			int first = Math.Clamp(e.E("first")?.Int(scope) ?? 0, 0, items.Count);
			int end = e.E("most") is { } most ? Math.Min(items.Count, first + most.Int(scope)) : items.Count;
			int perColumn = Math.Max(1, e.E("per_column")?.Int(scope) ?? int.MaxValue);
			int columnW = e.E("column_w")?.Int(scope) ?? 0;
			for (int i = first; i < end; i++)
			{
				UiContext itemCtx = ItemContext(items[i], i, context);
				var rowLocals = new Dictionary<string, UiValue>(locals) { ["i"] = new((long)i), ["selected"] = new(i == selected), ["count"] = new((long)items.Count) };
				int k = i - first;
				Row(e, surface, RowText(e, items[i], i, itemCtx, rowLocals), x + k / perColumn * columnW, y + k % perColumn * rowH, w, i == selected, RowEnabled(e, itemCtx, rowLocals), size, height, alpha, scope);
			}
		}

		private void List(UiElement e, IDrawSurface surface, int x, int y, int w, int h, float alpha, Dictionary<string, UiValue> locals, UiContext context, Scope scope)
		{
			if (e.E("state")?.Eval(scope).Raw is not ScrollList list)
			{
				return;
			}
			List<object?> items = [.. Items(e.E("items")!.Eval(scope).Raw)];
			int size = e.E("size")?.Int(scope) ?? Style.Body;
			int rowH = e.E("row_height")?.Int(scope) ?? r.Px(size + 10);
			bool active = e.E("active")?.Eval(scope).Truth ?? true;
			int fit = e.E("fit")?.Int(scope) ?? w - 40;
			int rowW = e.E("row_w")?.Int(scope) ?? w - 14;
			int height = e.E("height")?.Int(scope) ?? r.Px(size + 8);
			List<UiElement> template = e.C("row");
			void DrawRow(int i, int ry, bool on)
			{
				UiContext itemCtx = ItemContext(items[i], i, context);
				var rowLocals = new Dictionary<string, UiValue>(locals) { ["i"] = new((long)i), ["selected"] = new(on), ["row_y"] = new((long)ry), ["count"] = new((long)items.Count) };
				if (template.Count > 0)
				{
					Elements(template, x, ry, alpha, rowLocals, itemCtx);
					return;
				}
				Row(e, surface, Style.Fit(surface, RowText(e, items[i], i, itemCtx, rowLocals), fit, size), x, ry, rowW, on, RowEnabled(e, itemCtx, rowLocals), size, height, alpha, scope);
			}
			if (e.F("view", false))
			{
				list.DrawView(surface, x, y, w, h, rowH, items.Count, DrawRow);
				return;
			}
			list.Draw(surface, x, y, w, h, rowH, items.Count, DrawRow, active);
			if (active && e.F("probe", true))
			{
				object? item = items.Count > 0 ? items[Math.Clamp(list.Cursor, 0, items.Count - 1)] : null;
				r.ProbeSelected?.Invoke(item is null ? "" : RowText(e, item, list.Cursor, ItemContext(item, list.Cursor, context), locals), list.Top);
			}
		}

		private void Repeater(UiElement e, int x, int y, float fade, Dictionary<string, UiValue> locals, UiContext context, Scope scope)
		{
			List<object?> items = e.E("items") is { } source ? [.. Items(source.Eval(scope).Raw)] : [];
			int count = e.E("count")?.Int(scope) ?? items.Count;
			int dx = e.E("dx")?.Int(scope) ?? 0, dy = e.E("dy")?.Int(scope) ?? 0;
			int columns = Math.Max(1, e.E("columns")?.Int(scope) ?? 1);
			List<UiExpr> slots = e.Lists.GetValueOrDefault("slots") ?? [];
			string layoutRule = e.S("layout") ?? (dx != 0 && dy == 0 ? "row" : "column");
			for (int i = 0; i < count; i++)
			{
				bool has = i < items.Count;
				UiContext itemCtx = has ? ItemContext(items[i], i, context) : context;
				var itemLocals = new Dictionary<string, UiValue>(locals) { ["i"] = new((long)i), ["count"] = new((long)count) };
				var itemScope = new Scope(r, itemCtx, itemLocals, this);
				int col = layoutRule == "grid" ? i % columns : layoutRule == "row" ? i : 0;
				int row = layoutRule == "grid" ? i / columns : layoutRule == "column" ? i : 0;
				itemLocals["col"] = new((long)col);
				itemLocals["row"] = new((long)row);
				int ix = x + col * dx, iy = y + row * dy;
				if (layoutRule == "slots")
				{
					if (2 * i + 1 >= slots.Count)
					{
						break;
					}
					ix = x + slots[2 * i].Int(itemScope);
					iy = y + slots[2 * i + 1].Int(itemScope);
				}
				Elements(has ? e.C("template") : e.C("empty"), ix, iy, fade, itemLocals, itemCtx);
			}
		}
	}

	public Action<string, int>? ProbeSelected { get; set; }
}
