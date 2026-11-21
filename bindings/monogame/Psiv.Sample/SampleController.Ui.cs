using System.Globalization;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// The layout files' side of the screens (docs/ui-layout.md): the renderer, each frame's common fields, the logical
// resolution, and the probes and the overlay that show where the windows went.
public sealed partial class SampleController
{
	private UiRenderer? _uiRenderer;
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

	// `uiframes`: each frame style the layouts drew in the last frame as `NAME HOW COUNT`, by name.
	private string FramesProbe() => Ui.FramesDrawn.Count == 0 ? "none"
		: string.Join(", ", Ui.FramesDrawn.OrderBy(f => f.Key, StringComparer.Ordinal).Select(f => $"{f.Key} {f.Value.How} {f.Value.Count}"));

	// The developer overlay `windows`: every window drawn this frame outlined, with its screen and name.
	private void DrawWindowOverlay(IDrawSurface s)
	{
		foreach (UiDrawn d in Ui.Drawn)
		{
			Style.Frame(s, d.Rect.X, d.Rect.Y, Math.Max(2, d.Rect.Width), Math.Max(2, d.Rect.Height), new Color(255, 96, 220), 1);
			string label = $"{d.Screen}/{d.Window}";
			int w = s.Measure(label, Style.Small) + 8;
			int lx = Math.Clamp(d.Rect.X, 0, Math.Max(0, s.Width - w));
			int ly = Math.Clamp(d.Rect.Y, 0, Math.Max(0, s.Height - 22));
			s.Fill(lx, ly, w, 22, Color.Black * 0.7f);
			s.Text(label, lx + 4, ly + 2, new Color(255, 160, 236), Style.Small);
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
			.Set("options", new List<string> { T("ui.confirm.yes"), T("ui.confirm.no") }));
	}
}
