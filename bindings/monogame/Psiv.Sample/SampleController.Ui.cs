using System.Globalization;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The layout files' side of the screens (docs/ui-layout.md): the renderer, each frame's common fields, the logical
// resolution, and the probes and the overlay that show where the windows went.
public sealed partial class SampleController
{
	private UiRenderer? _uiRenderer;

	// The UI skin's page arrow, else the generated pointer: the dialogue's and the battle result's `page_arrow` field.
	private int PageArrow => _art?.Skin?.PageArrow ?? _art?.Pointer ?? -1;

	// A battle's intro or outro transition script is running.
	private bool TransitionPlaying => _outro is not null || _battle.Phase == BattlePhase.Playback && _timeline.Current is { Kind: StepKind.Transition };

	// The notices' boxes the code drew this frame through the screens' `toasts` slots.
	private readonly List<Rectangle> _toastBoxes = [];

	private UiRenderer Ui => _uiRenderer ??= new UiRenderer
	{
		Text = T,
		Px = Px,
		Role = UseTextRole,
		Log = _log,
		ProbeSelected = (selected, top) =>
		{
			_probeSelected = selected;
			_probeTop = top;
		},
	};

	public UiLayoutSet Layout => _assets?.Layout ?? UiLayoutSet.Defaults;

	// The logical frame the screens draw: the layout set's resolution.
	public (int Width, int Height) LogicalSize => (Layout.Styles.Width, Layout.Styles.Height);

	public UiScaling Scaling => Layout.Styles.Scaling;

	private int Timing(string key) => Layout.Styles.Timing(key, UiFields.Timings[key]);

	// A screen's data with the fields every screen has.
	private UiContext UiData() => new UiContext()
		.Set("clock_ms", _clockMs)
		.Set("text_scale", _s.Profile().TextSizePercent)
		.Set("dev", _dev)
		.Set("gamepad", _usingGamepad);

	private void BeginUiFrame(IDrawSurface s)
	{
		UiRenderer ui = Ui;
		ui.Set = Layout;
		ui.ClockMs = _clockMs;
		ui.TimeScale = _battle.Active ? (int)MathF.Round(SpeedOf(_s.Profile().BattleSpeed) * 100) : 100;
		ui.SliceImage = (file, border, scale) => _art?.LayoutSlice(file, border, scale);
		ui.Picture = file => _art?.LayoutPicture(file) ?? -1;
		ui.NamedImage = name => _art?.UiImage(name, _clockMs) ?? -1;
		ui.BeginFrame();
		_toastBoxes.Clear();
		int blink = Timing("cursor_blink_ms");
		Style.PointerHidden = blink > 0 && _clockMs / blink % 2 == 1;
	}

	// `windows`: each window the last frame drew as `SCREEN/NAME X Y WxH`; `window.NAME`: one of them, `-` when not drawn.
	private string WindowsProbe() => Ui.Drawn.Count == 0 ? "none" : string.Join(", ", Ui.Drawn.Select(DrawnText));

	private static string DrawnText(UiDrawn d) => $"{d.Screen}/{d.Window} {d.Rect.X} {d.Rect.Y} {d.Rect.Width}x{d.Rect.Height}";

	private string WindowProbe(string name) => Ui.Drawn.LastOrDefault(d => d.Window == name || $"{d.Screen}/{d.Window}" == name) is { Screen: not null } d ? DrawnText(d) : "-";

	// `overlaps` off the battle: each window of the last frame a notice's box covers part of, as `toast over SCREEN/NAME`.
	// A window holding the whole box (the screen-wide ones, the one the box stands in) is not covered by it. The field's
	// boxes are its layout's `box` elements named `toast`.
	private string ToastOverlaps()
	{
		IEnumerable<Rectangle> boxes = _toastBoxes.Concat(Ui.DrawnElements.Where(e => e.Element == "toast").Select(e => e.Rect));
		List<string> covered = [.. boxes.SelectMany(b => Ui.Drawn.Where(d => d.Rect.Intersects(b) && !d.Rect.Contains(b)).Select(d => $"toast over {d.Screen}/{d.Window}")).Distinct()];
		return covered.Count == 0 ? "none" : string.Join(", ", covered);
	}

	// `uifield.SCREEN.NAME` (or `.NAME.N.FIELD` for an item of a list field): a field as the screen was last drawn with it,
	// `-` when it has none; a list reads as its length.
	private string FieldProbe(string path)
	{
		string[] parts = path.Split('.');
		if (parts.Length < 2 || !Ui.LastData.TryGetValue(parts[0], out UiContext? ctx))
		{
			return "-";
		}
		object? value = ctx.Get(parts[1]);
		for (int i = 2; i + 1 < parts.Length; i += 2)
		{
			value = value is System.Collections.IList list && int.TryParse(parts[i], CultureInfo.InvariantCulture, out int n) && n >= 0 && n < list.Count && list[n] is UiContext item
				? item.Get(parts[i + 1]) : null;
		}
		return value switch
		{
			null => "-",
			bool b => b ? "true" : "false",
			System.Collections.IList l => l.Count.ToString(CultureInfo.InvariantCulture),
			IFormattable f => f.ToString(null, CultureInfo.InvariantCulture),
			_ => value.ToString() ?? "-",
		};
	}

	// `uiframes`: each frame style the layouts drew in the last frame as `NAME HOW COUNT`, by name.
	private string FramesProbe() => Ui.FramesDrawn.Count == 0 ? "none"
		: string.Join(", ", Ui.FramesDrawn.OrderBy(f => f.Key, StringComparer.Ordinal).Select(f => $"{f.Key} {f.Value.How} {f.Value.Count}"));

	// The developer overlay `windows`: every window drawn this frame outlined, with its screen and name.
	// Its labels at `zoom` of their size: the windows stand in logical pixels, the labels follow the styles' `dev_scale`.
	private void DrawWindowOverlay(IDrawSurface s, float zoom = 1f)
	{
		int Z(int v) => zoom == 1f ? v : Math.Max(1, (int)MathF.Round(v * zoom));
		// A bitmap font draws at its own scale, so its text keeps its size.
		int size = _bitmapFont is null ? Z(Style.Small) : Style.Small;
		foreach (UiDrawn d in Ui.Drawn)
		{
			Style.Frame(s, d.Rect.X, d.Rect.Y, Math.Max(2, d.Rect.Width), Math.Max(2, d.Rect.Height), new Color(255, 96, 220), 1);
			string label = $"{d.Screen}/{d.Window}";
			int w = s.Measure(label, size) + Z(8);
			int lx = Math.Clamp(d.Rect.X, 0, Math.Max(0, s.Width - w));
			int ly = Math.Clamp(d.Rect.Y, 0, Math.Max(0, s.Height - Z(22)));
			s.Fill(lx, ly, w, Z(22), Color.Black * 0.7f);
			s.Text(label, lx + Z(4), ly + Z(2), new Color(255, 160, 236), size);
		}
		_overlayDrawn["windows"] = Ui.Drawn.Count.ToString(CultureInfo.InvariantCulture);
	}

	private void DrawConfirm(IDrawSurface s, IDrawSurface t)
	{
		if (_confirm.Question is null)
		{
			return;
		}
		Ui.Draw("confirm", s, t, UiData()
			.Set("question", _confirm.Question)
			.Set("on_yes", _confirm.OnYes)
			.Set("cursor", _confirm.Cursor)
			.Set("options", _confirm.Choices?.ToList() ?? new List<string> { T("ui.confirm.yes"), T("ui.confirm.no") }));
	}
}
