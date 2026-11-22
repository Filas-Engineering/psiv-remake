using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// A line's control tags (events/FORMAT.md "Control tags") are the host's: `{page}` ends a page, `{wait:N}` holds the
// reveal N times the `wait_unit_ms` timing, `{portrait:ID}` switches the portrait, `{var:ID}` shows a value the game
// holds, and every other tag goes through the dialogue layout's `tags` table, else to the transcript. None of them is
// drawn, measured or counted in the reveal. A page longer than the box's `lines` goes on in the next box.
public sealed partial class SampleController
{
	private sealed record DialogueTag(int At, string Name, string Value)
	{
		public override string ToString() => Value.Length == 0 ? Name : $"{Name}:{Value}";
	}

	// A page as shown, tags taken out and values put in, with each tag at the count of shown characters before it.
	private sealed record DialoguePage(string Text, List<DialogueTag> Tags);

	// The part of a page in the box: the page's text from `Start` on, of which the box shows `End` characters; the next
	// box starts `Next` characters on, -1 when the page ends in this one.
	private readonly record struct DialogueBox(DialoguePage Page, int Start, string Text, int End, int Next);

	[GeneratedRegex(@"\{([A-Za-z_][A-Za-z0-9_]*)(?::([^{}]*))?\}")]
	private static partial Regex ControlTag();

	private string _lastLine = "";
	// Frame time since the box came up, which reveals the text at the profile's speed, and since it was all shown.
	private int _lineMs;
	private int _shownMs;
	private List<DialoguePage> _pages = [];
	private string _pagesOf = "";
	private int _page;
	private int _pageStart;
	// How many of the page's tags were acted on, every tag the line acted on as written, and the logged ones.
	private int _tagsDone;
	private readonly List<string> _tagsActed = [];
	private readonly HashSet<string> _tagsLogged = [];
	private string _portraitId = "";
	private bool _portraitOff;
	// The still picture the dialogue's `picture` tags show (a name of the assets' `pictures`), "" for none, until hidden or
	// the scene ends.
	private string _picture = "";
	// Every still picture up, the top last, when the dialogue layout's `picture_stack` keeps them as a stack.
	private readonly List<string> _pictureStack = [];

	private bool PictureStack => Ui.Param("dialogue", "picture_stack", UiData(), 0) != 0;
	// The script's story pictures (GameSession.Pictures) already reported as lacking an asset picture, once a run.
	private readonly HashSet<string> _storyPictureProblems = [];

	// The one picture stack, bottom first: the script's story pictures in the session's order (saved, and up over the
	// field until a script hides them), then the dialogue tags' pictures (gone with the event) that the script has not
	// put up; the top one is last.
	private List<string> StoryPictures()
	{
		IReadOnlyList<string> script = _s.Mode is Mode.Field or Mode.Event ? _s.Pictures() : [];
		return [.. script, .. _pictureStack.Where(name => !script.Contains(name))];
	}

	// The stack's pictures the assets have, as the layout's `pictures` items, the top last; a script's picture the assets
	// lack is a problem, logged once.
	private List<int> StoryPictureImages(Art art, List<string> names)
	{
		var images = new List<int>();
		foreach (string name in names)
		{
			if (art.NamedPicture(name) is int image)
			{
				images.Add(image);
			}
			else if (_storyPictureProblems.Add(name))
			{
				_log($"picture problem: no picture '{name}' in the assets' pictures for the script's story picture");
			}
		}
		return images;
	}
	// The `{portrait:ID:EXPRESSION}` ids already reported as lacking their expression, once a run.
	private readonly HashSet<string> _portraitProblems = [];
	// The surface the last frame drew on, which measures the box's text between frames, and the box it last fitted.
	private IDrawSurface? _measure;
	private (string Text, int End, int Next) _fit = ("", 0, -1);

	// Characters a second for each text speed, from the layout's timings; Instant shows the line at once.
	private int CharsPerSecond(TextSpeed speed)
	{
		Dictionary<string, int>? speeds = Layout.Styles.TimingTables.GetValueOrDefault("text_speed");
		int Of(string name, int fallback) => Math.Max(1, speeds?.GetValueOrDefault(name, fallback) ?? fallback);
		return speed switch
		{
			TextSpeed.Slow => Of("slow", 25),
			TextSpeed.Normal => Of("normal", 50),
			TextSpeed.Fast => Of("fast", 100),
			_ => int.MaxValue,
		};
	}

	// The box's revealed characters and whether it is done. Each character takes 1000 / cps ms, and a punctuation
	// character with a pause holds the next one back that much longer, as a wait does at its place; a wait after the last
	// character holds the box from being done. Without pauses and waits that is the box's time times the speed.
	private (int Count, bool Done) Revealed(DialogueBox box)
	{
		int cps = CharsPerSecond(_s.Profile().TextSpeed);
		if (cps == int.MaxValue)
		{
			return (box.End, true);
		}
		List<(int At, int Ms)> waits = [.. box.Page.Tags.Where(t => t.Name == "wait" && t.At >= box.Start && t.At - box.Start <= box.End)
			.Select(t => (t.At - box.Start, WaitMs(t.Value) ?? 0))];
		Dictionary<string, int>? pauses = Layout.Styles.TimingTables.GetValueOrDefault("punctuation_pauses");
		if (pauses is not { Count: > 0 } && waits.Count == 0)
		{
			int count = (int)Math.Min(box.End, (long)_lineMs * cps / 1000);
			return (count, count == box.End);
		}
		long held = 0;
		int w = 0;
		for (int i = 0; i < box.End; i++)
		{
			for (; w < waits.Count && waits[w].At <= i; w++)
			{
				held += waits[w].Ms;
			}
			if ((long)(_lineMs - held) * cps / 1000 <= i)
			{
				return (i, false);
			}
			held += pauses?.GetValueOrDefault(box.Text[i].ToString(), 0) ?? 0;
		}
		if (w == waits.Count)
		{
			return (box.End, true);
		}
		held += waits.Skip(w).Sum(x => (long)x.Ms);
		return (box.End, (long)(_lineMs - held) * cps / 1000 >= box.End);
	}

	private int? WaitMs(string value) => int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out int n) ? n * Timing("wait_unit_ms") : null;

	// The line split at its `{page}` tags; `problem` hears each value that cannot be shown.
	private List<DialoguePage> Parse(string text, Action<string>? problem)
	{
		var pages = new List<DialoguePage>();
		var shown = new StringBuilder();
		var tags = new List<DialogueTag>();
		int copied = 0;
		foreach (Match m in ControlTag().Matches(text))
		{
			string name = m.Groups[1].Value, value = m.Groups[2].Value;
			// A name token the session left is one the glossary lacks: shown as written.
			if (name is "name" or "name_upper" or "name_lower")
			{
				continue;
			}
			shown.Append(text, copied, m.Index - copied);
			copied = m.Index + m.Length;
			if (name == "page")
			{
				pages.Add(new DialoguePage(shown.ToString(), tags));
				shown.Clear();
				tags = [];
				continue;
			}
			var tag = new DialogueTag(shown.Length, name, value);
			tags.Add(tag);
			if (name == "var")
			{
				(string shownValue, bool known) = VarValue(value);
				shown.Append(shownValue);
				if (!known)
				{
					problem?.Invoke($"{tag}: the game holds no value '{value}'");
				}
			}
			else if (name == "wait" && WaitMs(value) is null)
			{
				problem?.Invoke($"{tag}: the wait needs a whole number");
			}
		}
		shown.Append(text, copied, text.Length - copied);
		pages.Add(new DialoguePage(shown.ToString(), tags));
		return pages;
	}

	// `money`, `leader` (the first member in the lineup's name) or an id the game names: a member's chosen name, a
	// character's, an item's, an area's and so on. Unknown, the id itself.
	private (string Text, bool Known) VarValue(string id)
	{
		switch (id)
		{
		case "money":
			return (_s.WorldInfo().Money.ToString(CultureInfo.InvariantCulture), true);
		case "leader":
			return _s.Party().FirstOrDefault(m => m.InLineup) is { } leader ? (leader.Name, true) : (id, false);
		}
		string name = _s.DisplayName(id);
		return (name, name != id);
	}

	private List<DialoguePage> PagesOf(Line line)
	{
		if (_pagesOf != line.Display)
		{
			_pages = Parse(line.Text, null);
			_pagesOf = line.Display;
		}
		return _pages;
	}

	// The box on screen; a line the dialogue has not taken up yet shows from its start. Its fit is the last one measured.
	private DialogueBox BoxOf(Line line)
	{
		bool started = line.Display == _lastLine;
		List<DialoguePage> pages = PagesOf(line);
		DialoguePage page = pages[started ? Math.Min(_page, pages.Count - 1) : 0];
		int start = started ? Math.Min(_pageStart, page.Text.Length) : 0;
		string text = page.Text[start..];
		(int end, int next) = _fit.Text == text ? (_fit.End, _fit.Next) : (text.Length, -1);
		return new DialogueBox(page, start, text, end, next);
	}

	// The fields the line's text element reads, for measuring and drawing it.
	private UiContext LineFields(UiContext ctx, Line line, string text) => ctx
		.Set("has_line", true)
		.Set("speaker", line.SpeakerName)
		.Set("line", text)
		// The step of the dialogue role's font, which the layout switches to before the line.
		.Set("line_step", _art?.BitmapFontFor(_s.Profile().TextLanguage, TextRole.Dialogue)?.LineStep((int)MathF.Round(Style.Body * _scale)) ?? Px(Style.LineHeight));

	// How much of `text` the box holds: the lines the layout's `line` element shows, measured as it draws them.
	private (int End, int Next) Fit(Line line, string text)
	{
		if (_measure is not { } s || text.Length == 0)
		{
			return (text.Length, -1);
		}
		IDrawSurface t = _scale == 1f ? s : new ScaledSurface(s, _scale);
		BitmapFont? font = _bitmapFont;
		var wrap = Ui.WrapOf("dialogue", "box", "line", s, t, LineFields(UiData().Set("box_visible", true), line, text));
		if (_bitmapFont != font)
		{
			s.UseBitmapFont(font);
			_bitmapFont = font;
		}
		if (wrap is not { } w || w.Lines <= 0 || w.Spans.Count <= w.Lines)
		{
			return (text.Length, -1);
		}
		(int start, int length) = w.Spans[w.Lines - 1];
		return (start + length, w.Spans[w.Lines].Start);
	}

	private DialogueBox FitBox(Line line)
	{
		DialogueBox box = BoxOf(line);
		(int end, int next) = Fit(line, box.Text);
		_fit = (box.Text, end, next);
		return box with { End = end, Next = next };
	}

	private void StartLine(Line line)
	{
		_lastLine = line.Display;
		_lineMs = 0;
		_shownMs = 0;
		_log("line: " + line.Display);
		_pages = Parse(line.Text, problem => _log("tag problem: " + problem));
		_pagesOf = line.Display;
		_page = 0;
		_pageStart = 0;
		_tagsDone = 0;
		_tagsActed.Clear();
		_tagsLogged.Clear();
		_portraitId = line.Portrait;
		_portraitOff = false;
		SkipEmptyPages();
	}

	// A page with nothing to show acts on its tags and gives way to the next; the last page stays.
	private void SkipEmptyPages()
	{
		while (_page < _pages.Count - 1 && string.IsNullOrWhiteSpace(_pages[_page].Text))
		{
			ActOnTags(_pages[_page], 0, int.MaxValue / 2);
			_tagsActed.Add("page");
			_page++;
			_tagsDone = 0;
		}
	}

	private void ActOnTags(DialoguePage page, int start, int count)
	{
		while (_tagsDone < page.Tags.Count && page.Tags[_tagsDone].At <= start + count)
		{
			ActOnTag(page.Tags[_tagsDone++]);
		}
	}

	private void ActOnTag(DialogueTag tag)
	{
		_tagsActed.Add(tag.ToString());
		switch (tag.Name)
		{
		case "wait":
		case "var":
		case "narration":
			return;
		case "portrait":
			_portraitId = tag.Value;
			_portraitOff = false;
			if (tag.Value.IndexOf(':') is int colon and > 0 && KnownPortrait(tag.Value[..colon]) && !KnownPortrait(tag.Value))
			{
				if (_portraitProblems.Add(tag.Value))
				{
					_log($"tag problem: {tag}: no expression '{tag.Value[(colon + 1)..]}' of portrait '{tag.Value[..colon]}' in the assets: its portrait stands in");
				}
			}
			else if (!KnownPortrait(tag.Value))
			{
				TagProblem(tag, $"no portrait '{tag.Value}' in game.json's presentation portraits, the people or the assets");
			}
			return;
		}
		UiTag? hook = Layout.Screen("dialogue").Tags.GetValueOrDefault(tag.Name) ?? UiLayoutSet.Defaults.Screen("dialogue").Tags.GetValueOrDefault(tag.Name);
		UiTagCase? match = hook?.Cases.FirstOrDefault(c => c.Holds(tag.Value));
		string id = match is not null ? match.IdFor(tag.Value) ?? tag.Value : hook?.Values?.GetValueOrDefault(tag.Value) ?? hook?.Default ?? hook?.Id ?? tag.Value;
		string? slot = match is not null ? match.Slot : hook?.Slot;
		switch (match?.Action ?? hook?.Action)
		{
		case "cue" when _data.Cues.Contains(id) || Assets.TagIds(Layout, "cue").Contains(id):
			_audio.PlayCue(id);
			break;
		case "cue":
			TagProblem(tag, $"no cue '{id}' the game or the sample plays");
			break;
		case "music" when _data.Music.Contains(id) || Assets.TagIds(Layout, "music").Contains(id):
			_scriptMusic = id;
			_audio.PlayMusic(id, CrossfadeMs);
			break;
		case "music":
			TagProblem(tag, $"no music '{id}' the game or the sample plays");
			break;
		case "music_stop":
			_scriptMusic = "";
			_audio.StopMusic(StopFadeMs);
			break;
		case "music_pause":
			_audio.PauseMusic();
			break;
		case "music_resume":
			_audio.ResumeMusic();
			break;
		case "fx" when Anim.FxProblem(id) is null:
			StartFx(id);
			break;
		case "fx":
			TagProblem(tag, Anim.FxProblem(id)!);
			break;
		case "panel_show" when _data.Cameras.Contains(id) || _data.Actors.Contains(id) || _data.Characters.Contains(id):
			_panels.RemoveAll(p => p.Id == id);
			_panels.Add(new Panel(id, id, KnownSlot(slot ?? DefaultSlot)));
			break;
		case "panel_show":
			TagProblem(tag, $"no camera or person '{id}' for a panel to show");
			break;
		case "panel_hide":
			if (_panels.RemoveAll(p => p.Id == id) == 0)
			{
				TagProblem(tag, $"no panel '{id}' is up");
			}
			break;
		case "panel_clear":
			_panels.Clear();
			break;
		case "picture" when _assets?.Manifest.Pictures.ContainsKey(id) == true:
			if (!PictureStack)
			{
				_pictureStack.Clear();
			}
			_pictureStack.Remove(id);
			_pictureStack.Add(id);
			_picture = id;
			break;
		case "picture":
			TagProblem(tag, $"no picture '{id}' in the assets' pictures");
			break;
		// Without an id the top one goes; only picture_clear takes them all.
		case "picture_hide" when id.Length == 0 && _picture.Length > 0:
		case "picture_hide" when PictureStack ? _pictureStack.Contains(id) : _picture == id:
			_pictureStack.Remove(id.Length == 0 ? _picture : id);
			_picture = PictureStack && _pictureStack.Count > 0 ? _pictureStack[^1] : "";
			if (!PictureStack)
			{
				_pictureStack.Clear();
			}
			break;
		case "picture_hide":
			TagProblem(tag, $"no picture '{id}' is up");
			break;
		case "picture_clear":
			_picture = "";
			_pictureStack.Clear();
			break;
		case "portrait_off":
			_portraitOff = true;
			break;
		default:
			if (_tagsLogged.Add(tag.ToString()))
			{
				_log(tag.Value.Length == 0 ? $"tag: {tag.Name}" : $"tag: {tag.Name} {tag.Value}");
			}
			break;
		}
	}

	private void TagProblem(DialogueTag tag, string reason)
	{
		if (_tagsLogged.Add("problem " + tag))
		{
			_log($"tag problem: {tag}: {reason}");
		}
	}

	private bool KnownPortrait(string id) => _data.Portraits.Contains(id) || _data.Actors.Contains(id) || _data.Characters.Contains(id) ||
		_assets?.Manifest.Portraits.ContainsKey(id) == true;

	// After a box: the next box of the page, else the next page, else the next line (whose first frame drawn before the
	// dialogue takes it up still reveals by this one's time).
	private void NextBox(DialogueBox box)
	{
		if (box.Next >= 0)
		{
			_lineMs = 0;
			_shownMs = 0;
			_pageStart += box.Next;
			_audio.PlayCue("line");
			ActOnTags(box.Page, _pageStart, 0);
			return;
		}
		if (_page < _pages.Count - 1)
		{
			_lineMs = 0;
			_shownMs = 0;
			_tagsActed.Add("page");
			_page++;
			_pageStart = 0;
			_tagsDone = 0;
			SkipEmptyPages();
			_audio.PlayCue("line");
			ActOnTags(_pages[_page], 0, 0);
			return;
		}
		_s.ConfirmLine();
		_lastLine = "";
		_audio.PlayCue("line");
	}

	private void UpdateDialogue(int dtMs, ActionInput input)
	{
		if (input.Pressed(GameAction.Skip) && _s.CanSkip)
		{
			// The same line coming up again later starts from its first page.
			_lastLine = "";
			Report(_s.Skip());
			return;
		}
		if (_s.Line() is { } narrated && IsNarration(narrated))
		{
			UpdateNarration(narrated, dtMs, input);
			return;
		}
		if (_narrationHeld && _narration.Count > 0 && (_s.Line() is not null || _s.Choice() is not null))
		{
			_narrationHeld = !input.Confirm;
			return;
		}
		_narrationHeld = false;
		_narration.Clear();
		if (_s.Line() is { } line)
		{
			bool fresh = line.Display != _lastLine;
			if (fresh)
			{
				StartLine(line);
			}
			DialogueBox box = FitBox(line);
			if (!fresh && !Revealed(box).Done)
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
			(int count, bool done) = Revealed(box);
			ActOnTags(box.Page, box.Start, count);
			if (input.Confirm && !done)
			{
				_lineMs = int.MaxValue / 2;
				ActOnTags(box.Page, box.Start, box.End);
			}
			else if (input.Confirm || (done && _s.Profile().AutoAdvance && _shownMs >= Math.Max(Timing("auto_advance_min_ms"), box.End * Timing("auto_advance_ms_per_char"))))
			{
				NextBox(box);
			}
			return;
		}
		if (_s.Choice() is { } choice)
		{
			Move(input, choice.Options.Count);
			if (input.Confirm)
			{
				_log($"choose: {_cursor} {choice.Options[_cursor]}");
				Report(_s.Choose(_cursor));
				_cursor = 0;
			}
			return;
		}
		_s.Update(dtMs);
	}

	private const int DialogueBoxHeight = 176;

	// The dialogue file's param, else `fallback`.
	private int DialogueParam(string name, int fallback) => Ui.Param("dialogue", name, UiData(), fallback);

	private int DialogueHeight => Px(DialogueParam("box_h", DialogueBoxHeight));

	// Where the dialogue box was last drawn, for headless expectations: top, bottom or - without one.
	private string _dialogueAt = "-";

	// The box sits at the bottom of the screen, `box_margin` over its edge, or at `box_top` when the leader stands where
	// it would cover them; over panels, where it covers the fewest of them.
	private int DialogueTop(IDrawSurface s, bool field)
	{
		int bottom = s.Height - DialogueHeight - DialogueParam("box_margin", 16);
		int top = DialogueParam("box_top", 36);
		if (_panels.Count > 0)
		{
			return DialogueOnTop(s, bottom, top) ? top : bottom;
		}
		return field && _field.LeaderScreenY + _field.Cell > bottom - 8 ? top : bottom;
	}

	// `s` draws the field and `t` the box, with the profile's text size.
	private void DrawDialogue(IDrawSurface s, IDrawSurface t, Art art)
	{
		bool field = DrawField(s, art);
		DrawDarkness(s, "dialogue");
		DrawDialogueLayer(s, t, art, DialogueTop(s, field), _s.Line() is { } said && IsNarration(said) || _narrationHeld && _narration.Count > 0);
	}

	// The script's story pictures over the field outside an event, through the dialogue layout with no box; nothing is
	// drawn while there are none, so the dialogue layout's windows stay off the field.
	private void DrawFieldPictures(IDrawSurface s, IDrawSurface t, Art art)
	{
		if (_s.Pictures().Count > 0)
		{
			DrawDialogueLayer(s, t, art, DialogueTop(s, true), false, boxless: true);
		}
	}

	private void DrawDialogueLayer(IDrawSurface s, IDrawSurface t, Art art, int y, bool narrating, bool boxless = false)
	{
		// Between lines (a script actor walking, a fade) the field shows without an empty box.
		bool box = !boxless && !narrating && (_s.LinePending || _s.Choice() is not null);
		if (box)
		{
			_dialogueAt = y > s.Height / 2 ? "bottom" : "top";
		}
		UiContext ctx = SetBanner(UiData(), !boxless && y > s.Height / 2 && _panels.Count == 0)
			.Set("top", y)
			.Set("box_visible", box)
			.Set("next_arrow_ms", Timing("next_arrow_ms"));
		NarrationFields(ctx, narrating ? _s.Line() : null);
		Line? line = box ? _s.Line() : null;
		Choice? choice = box && line is null ? _s.Choice() : null;
		DialogueBox? shown = line is null ? null : BoxOf(line);
		(int revealed, bool done) = shown is { } b ? Revealed(b) : (0, false);
		string portrait = line is null ? "" : line.Display == _lastLine ? _portraitId : line.Portrait;
		LineFields(ctx, line ?? new Line("", "", "", "", "", ""), shown?.Text ?? "")
			.Set("has_line", line is not null)
			.Set("portrait", line is { SpeakerName.Length: > 0 } && !(_portraitOff && line.Display == _lastLine)
				? art.Portrait(portrait, line.SpeakerId.Length > 0 ? line.SpeakerId : line.SpeakerName) : -1)
			.Set("revealed", revealed)
			.Set("all_shown", line is not null && done)
			.Set("shown_ms", line is not null && done ? _shownMs : 0)
			.Set("hint", line is null ? "" : T("ui.dialogue.next", Hint(GameAction.Confirm)) + (_s.CanSkip ? "   " + T("ui.dialogue.skip", Hint(GameAction.Skip)) : ""))
			.Set("arrow", art.Pointer)
			.Set("page_arrow", PageArrow)
			.Set("has_choice", choice is not null)
			.Set("prompt", choice is null ? "" : choice.Prompt.Length == 0 ? T("ui.dialogue.choose") : choice.Prompt)
			.Set("options", choice?.Options.ToList() ?? [])
			.Set("cursor", _cursor);
		List<string> names = StoryPictures();
		List<int> images = StoryPictureImages(art, names);
		int picture = names.Count > 0 && art.NamedPicture(names[^1]) is int top ? top : -1;
		RouteFields(ctx, _s.Line()).Set("picture", picture)
			.Set("picture_w", picture >= 0 ? art.SizeOf(picture).W : 0)
			.Set("picture_h", picture >= 0 ? art.SizeOf(picture).H : 0)
			.Set("pictures", images.Select(image => new UiContext()
				.Set("picture", image).Set("picture_w", art.SizeOf(image).W).Set("picture_h", art.SizeOf(image).H)).ToList());
		// The wrapped line's probe, measured while the dialogue's font draws it: the line count, then each line's width.
		var widths = new List<int>();
		int count = -1;
		Ui.Probe = (name, value) =>
		{
			if (name == "wrapped")
			{
				count = value;
				widths.Clear();
			}
			else if (name == "wrapped_width")
			{
				widths.Add(value);
			}
		};
		Ui.Draw("dialogue", s, t, ctx);
		Ui.Probe = null;
		if (count >= 0)
		{
			_probeWrapped = $"{count}: " + string.Join(" ", widths.Select(w => w.ToString(CultureInfo.InvariantCulture)));
		}
	}

	// The dialogue's state probes: `reveal` (characters shown of the line's, tags left out), `fontmissing`, `tags` (each
	// tag the line acted on, as written), `pagetext` (what the box holds), `dialoguepage` (the box among the line's
	// boxes), `lineportrait` (the portrait id shown, `off`), `shownportrait` (the key the shown portrait came by, `off`)
	// and `dialogueoverlaps` (named box elements over the frame or each other, or `none`).
	private string DialogueState(string key)
	{
		Line? line = _s.Line();
		if (key == "dialogueoverlaps")
		{
			return DialogueOverlaps();
		}
		if (key == "picture")
		{
			return StoryPictures() is { Count: > 0 } up ? up[^1] : "-";
		}
		if (key == "pictures")
		{
			return StoryPictures() is { Count: > 0 } stack ? string.Join(" ", stack) : "-";
		}
		if (line is null)
		{
			return key == "tags" && _tagsActed.Count > 0 ? string.Join(", ", _tagsActed) : key == "tags" ? "none" : "-";
		}
		List<DialoguePage> pages = PagesOf(line);
		DialogueBox box = BoxOf(line);
		bool started = line.Display == _lastLine;
		int page = started ? Math.Min(_page, pages.Count - 1) : 0;
		switch (key)
		{
		case "reveal":
			return $"{pages.Take(page).Sum(p => p.Text.Length) + box.Start + Revealed(box).Count}/{pages.Sum(p => p.Text.Length)}";
		case "fontmissing":
			return _art?.BitmapFontFor(_s.Profile().TextLanguage, TextRole.Dialogue) is { } font
				? (font.Missing(string.Concat(pages.Select(p => p.Text))) is { Length: > 0 } missing ? missing : "none") : "-";
		case "tags":
			return started && _tagsActed.Count > 0 ? string.Join(", ", _tagsActed) : "none";
		case "pagetext":
			return box.Text[..box.End];
		case "lineportrait":
			return !started ? (line.Portrait.Length > 0 ? line.Portrait : "-") : _portraitOff ? "off" : _portraitId.Length > 0 ? _portraitId : "-";
		case "shownportrait":
			return started && _portraitOff ? "off"
				: _art?.PortraitOf(started ? _portraitId : line.Portrait, line.SpeakerId.Length > 0 ? line.SpeakerId : line.SpeakerName).Key ?? "-";
		case "dialoguepage":
			// Boxes before this one: every box of the earlier pages, and this page's up to its start, fitted afresh.
			int Boxes(string text, int upTo)
			{
				int n = 1;
				for (int at = 0; ; n++)
				{
					(_, int next) = Fit(line, text[at..]);
					if (next < 0 || at + next > upTo)
					{
						return n;
					}
					at += next;
				}
			}
			int before = pages.Take(page).Sum(p => Boxes(p.Text, int.MaxValue)) + Boxes(pages[page].Text, box.Start) - 1;
			return $"{before + 1}/{pages.Sum(p => Boxes(p.Text, int.MaxValue))}";
		}
		return "?";
	}

	// The named elements of the dialogue box drawn last frame: any outside the frame's inside, and the hint over another.
	private string DialogueOverlaps()
	{
		if (Ui.Drawn.LastOrDefault(d => d.Screen == "dialogue" && d.Window == "box") is not { Screen: not null } drawn)
		{
			return "-";
		}
		string frameName = Layout.Screen("dialogue").Windows.FirstOrDefault(w => w.Name == "box")?.Frame ?? "none";
		int inset = Layout.Styles.Frames.GetValueOrDefault(frameName) switch
		{
			{ Type: "generated" } => 4,
			{ Type: "nine_slice" } f => f.Border * f.Scale,
			{ Type: "outline" } f => f.Thickness,
			_ => 0,
		};
		Rectangle inside = drawn.Rect;
		inside.Inflate(-inset, -inset);
		List<UiDrawnElement> elements = [.. Ui.DrawnElements.Where(e => e.Screen == "dialogue" && e.Window == "box")];
		var found = new List<string>();
		foreach (UiDrawnElement e in elements.Where(e => e.Element is "line" or "hint" or "arrow" or "portrait" && !inside.Contains(e.Rect)))
		{
			found.Add($"{e.Element} frame");
		}
		foreach (UiDrawnElement hint in elements.Where(e => e.Element == "hint"))
		{
			found.AddRange(elements.Where(e => e.Element is "line" or "arrow" or "portrait" && e.Rect.Intersects(hint.Rect)).Select(e => $"hint {e.Element}"));
		}
		return found.Count == 0 ? "none" : string.Join(", ", found.Distinct());
	}
}
