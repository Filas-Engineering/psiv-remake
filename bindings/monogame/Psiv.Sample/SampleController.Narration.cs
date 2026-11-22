using System.Globalization;
using System.Text.RegularExpressions;

using Psiv.Session;

namespace Psiv.Sample;

// Narration without a box: when the dialogue layout has a `narration` window, a line without a speaker joins the lines
// already in that window, up to the `narration_lines` it holds; the line that would not fit starts the window afresh.
public sealed partial class SampleController
{
	// The narration shown above the line revealing now, in the order it came.
	private readonly List<string> _narration = [];

	private bool NarrationWindow => Layout.Screen("dialogue").Windows.Any(w => w.Name == "narration");

	// Which lines the narration window takes, by the dialogue layout's `narration_when`: `all` (every line without a
	// speaker), `tagged` (a line that starts with `{narration}`) or an expression over the line's fields. A line starting
	// with `{narration:on}` or `{narration:off}` goes there or to the box whatever the mode.
	private bool IsNarration(Line line)
	{
		if (!NarrationWindow)
		{
			return false;
		}
		Match lead = ControlTag().Match(line.Text);
		string? tag = lead.Success && lead.Index == 0 && lead.Groups[1].Value == "narration" ? lead.Groups[2].Value : null;
		if (tag is "on" or "off")
		{
			return tag == "on";
		}
		UiValue when = Ui.ParamValue("dialogue", "narration_when", RouteFields(UiData(), line), new UiValue("all"));
		return when.Raw switch
		{
			"all" => line.SpeakerId.Length == 0 && line.SpeakerName.Length == 0,
			"tagged" => tag == "",
			_ => when.Truth,
		};
	}

	// The fields a `narration_when` expression and the layout read about the line and the picture stack.
	private UiContext RouteFields(UiContext ctx, Line? line) => ctx
		.Set("line_key", line?.TextId ?? "")
		.Set("speaker", line?.SpeakerName ?? "")
		.Set("picture_top", _picture)
		.Set("picture_names", _pictureStack.ToList());

	private string NarrationText(Line line) =>
		string.Join(" ", PagesOf(line).Select(page => page.Text.Trim()).Where(text => text.Length > 0));

	// Characters a second: the layout's `narration_cps` at Normal, scaled as the text speed option scales the dialogue's;
	// without it the dialogue's own speed.
	private int NarrationCps()
	{
		int cps = CharsPerSecond(_s.Profile().TextSpeed);
		int own = DialogueParam("narration_cps", 0);
		return cps == int.MaxValue || own <= 0 ? cps : (int)Math.Max(1, (long)own * cps / CharsPerSecond(TextSpeed.Normal));
	}

	private (int Count, bool Done) NarrationRevealed(string text)
	{
		int cps = NarrationCps();
		if (cps == int.MaxValue)
		{
			return (text.Length, true);
		}
		long ms = Math.Max(0L, (long)_lineMs - DialogueParam("narration_delay_ms", 0));
		int count = (int)Math.Min(text.Length, ms * cps / 1000);
		return (count, count == text.Length);
	}

	// Each narration entry as the window's rows: wrapped to `narration_width` (the window's width) at the `narration`
	// text style's size in the narration font.
	private List<string> NarrationRows(string text)
	{
		if (_measure is not { } s || text.Length == 0)
		{
			return text.Length == 0 ? [] : [text];
		}
		IDrawSurface t = _scale == 1f ? s : new ScaledSurface(s, _scale);
		UiContext ctx = UiData();
		int width = DialogueParam("narration_width", Ui.WindowRect("dialogue", "narration", s, ctx)?.Width ?? s.Width);
		int size = Layout.Styles.TextStyles.TryGetValue("narration", out UiTextStyle? ts) && ts.Size is not null
			? ts.Size.Int(new StylesScope(Layout.Styles)) : Style.Body;
		BitmapFont? font = _bitmapFont;
		UseTextRole(s, TextRole.Narration);
		List<string> rows = Style.Wrap(t, text, Math.Max(1, width), size);
		if (_bitmapFont != font)
		{
			s.UseBitmapFont(font);
			_bitmapFont = font;
		}
		return rows;
	}

	private void StartNarration(Line line)
	{
		StartLine(line);
		foreach (DialoguePage page in _pages)
		{
			_tagsDone = 0;
			ActOnTags(page, 0, int.MaxValue / 2);
		}
		int most = Math.Max(1, DialogueParam("narration_lines", 4));
		if (_narration.Sum(entry => NarrationRows(entry).Count) + NarrationRows(NarrationText(line)).Count > most)
		{
			_narration.Clear();
		}
	}

	private void UpdateNarration(Line line, int dtMs, ActionInput input)
	{
		bool fresh = line.Display != _lastLine;
		if (fresh)
		{
			StartNarration(line);
		}
		string text = NarrationText(line);
		if (!fresh && !NarrationRevealed(text).Done)
		{
			_lineMs += dtMs;
			if (_clockMs - _lastTextTickMs >= Timing("text_tick_ms"))
			{
				_audio.PlayCue("text");
				_lastTextTickMs = _clockMs;
			}
		}
		else if (!fresh)
		{
			_shownMs += dtMs;
		}
		bool done = NarrationRevealed(text).Done;
		if (input.Confirm && !done)
		{
			_lineMs = int.MaxValue / 2;
		}
		else if (input.Confirm || (done && _s.Profile().AutoAdvance && _shownMs >= Math.Max(Timing("auto_advance_min_ms"), text.Length * Timing("auto_advance_ms_per_char"))))
		{
			_narration.Add(text);
			_s.ConfirmLine();
			_lastLine = "";
			_audio.PlayCue("line");
		}
	}

	// The window's rows: the entries shown, then the line revealing now as far as it has come, at most `narration_lines`.
	private List<(string Text, string Full)> NarrationShown(Line? line)
	{
		var rows = new List<(string, string)>();
		foreach (string entry in _narration)
		{
			rows.AddRange(NarrationRows(entry).Select(row => (row, row)));
		}
		if (line is not null && IsNarration(line))
		{
			string text = NarrationText(line);
			int left = line.Display == _lastLine ? NarrationRevealed(text).Count : 0;
			foreach (string row in NarrationRows(text))
			{
				rows.Add((row[..Math.Clamp(left, 0, row.Length)], row));
				// The space the wrap took out between two rows counts as a character of the reveal.
				left -= row.Length + 1;
			}
		}
		return [.. rows.Take(Math.Max(1, DialogueParam("narration_lines", 4)))];
	}

	private UiContext NarrationFields(UiContext ctx, Line? line)
	{
		List<(string Text, string Full)> rows = NarrationWindow ? NarrationShown(line) : [];
		bool current = line is not null && IsNarration(line);
		ctx.Set("narration_visible", rows.Count > 0)
			.Set("narration_rows", rows.Select(r => new UiContext().Set("text", r.Text).Set("full", r.Full).Set("shown", r.Text == r.Full)).ToList())
			.Set("narration_all_shown", current && line!.Display == _lastLine && NarrationRevealed(NarrationText(line)).Done);
		_probeNarration = rows.Count == 0 ? "none" : string.Join(" | ", rows.Select(r => r.Text));
		return ctx;
	}

	private string _probeNarration = "-";

	private string NarrationState(string key) => key switch
	{
		"narration" => _probeNarration,
		"narrationreveal" => _s.Line() is { } line && IsNarration(line)
			? $"{(line.Display == _lastLine ? NarrationRevealed(NarrationText(line)).Count : 0)}/{NarrationText(line).Length}".ToString(CultureInfo.InvariantCulture) : "-",
		_ => "?",
	};
}
