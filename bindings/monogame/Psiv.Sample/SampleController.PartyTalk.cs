using Psiv.Session;

namespace Psiv.Sample;

// The party talk one speaker at a time, with the menu_options layout's `party_talk` "speakers": PartyTalkLines' lines in
// the layout's `talk_speaker` window, each speaker's portrait and name and their line page by page, read as the dialogue
// reads a line (`{page}` breaks, boxes as many rows as the window's `talk_text` element shows, the reveal at the text
// speed with its waits, Confirm hurrying a box still revealing). Confirm goes on to the next box, page and speaker and
// leaves after the last; Back leaves at once. The page and the "page" mode are in SampleController.Menu.
public sealed partial class SampleController
{
	// The speakers' lines while they run, else null; the speaker shown, their pages, the page on screen and where its box
	// starts, the box's time since it came up, and how many of the page's tags were acted on.
	private List<TalkLine>? _talkSpeakers;
	private int _talkIndex;
	private List<DialoguePage> _talkPages = [];
	private int _talkPage;
	private int _talkStart;
	private int _talkMs;
	private int _talkTagsDone;
	// The portrait a `{portrait:ID}` tag of the line switched to (an expression as `ID:EXPRESSION`), "" for the speaker's own.
	private string _talkPortrait = "";
	// The rectangle the speaker's window was last drawn in, which its text is measured in.
	private (int X, int Y, int W, int H) _talkArea;

	private string PartyTalkMode => Ui.ParamText("menu_options", "party_talk", UiData(), "close");

	private bool TalkHidesMenu => Ui.Param("menu_options", "talk_hide_menu", UiData(), 0) != 0;

	private void StartTalkSpeakers(IReadOnlyList<TalkLine> lines)
	{
		_talkSpeakers = [.. lines];
		StartTalkSpeaker(0);
	}

	private void StartTalkSpeaker(int index)
	{
		_talkIndex = index;
		TalkLine line = _talkSpeakers![index];
		_talkPages = ShownTagPages(line.Text) is { Count: > 0 } pages ? pages : [new DialoguePage("", [])];
		_talkPage = 0;
		_talkStart = 0;
		_talkMs = 0;
		_talkTagsDone = 0;
		_talkPortrait = "";
		_log($"talk: {line.SpeakerName}: {string.Join(" ", _talkPages.Select(p => p.Text))}");
	}

	private void UpdateTalkSpeakers(int dtMs, ActionInput input)
	{
		if (input.Back)
		{
			_talkSpeakers = null;
			return;
		}
		DialogueBox box = TalkBox();
		if (!Revealed(box, _talkMs).Done)
		{
			_talkMs += dtMs + HeldRevealMs(dtMs, input, ProfileCps());
			if (_clockMs - _lastTextTickMs >= Timing("text_tick_ms"))
			{
				_audio.PlayCue("text");
				_lastTextTickMs = _clockMs;
			}
		}
		(int count, bool done) = Revealed(box, _talkMs);
		ActOnTalkTags(box.Page, box.Start + count);
		if (input.Confirm && !done && !RevealHolds)
		{
			_talkMs = int.MaxValue / 2;
			ActOnTalkTags(box.Page, box.Start + box.End);
		}
		else if (input.Confirm && (done || !RevealHolds))
		{
			NextTalkBox(box);
		}
	}

	// Only `{portrait:...}` acts here; `{wait:N}` holds the reveal (Revealed), and the rest are dropped as on the page.
	private void ActOnTalkTags(DialoguePage page, int upTo)
	{
		for (; _talkTagsDone < page.Tags.Count && page.Tags[_talkTagsDone].At <= upTo; _talkTagsDone++)
		{
			if (page.Tags[_talkTagsDone] is { Name: "portrait" } portrait)
			{
				_talkPortrait = portrait.Value;
			}
		}
	}

	private void NextTalkBox(DialogueBox box)
	{
		_talkMs = 0;
		_audio.PlayCue("line");
		if (box.Next >= 0)
		{
			_talkStart += box.Next;
		}
		else if (_talkPage < _talkPages.Count - 1)
		{
			_talkPage++;
			_talkStart = 0;
			_talkTagsDone = 0;
		}
		else if (_talkIndex < _talkSpeakers!.Count - 1)
		{
			StartTalkSpeaker(_talkIndex + 1);
		}
		else
		{
			_talkSpeakers = null;
		}
	}

	private DialogueBox TalkBox()
	{
		DialoguePage page = _talkPages[_talkPage];
		int start = Math.Min(_talkStart, page.Text.Length);
		string text = page.Text[start..];
		(int end, int next) = TalkFit(text);
		return new DialogueBox(page, start, text, end, next);
	}

	private (int End, int Next) TalkFit(string text) => FitIn("menu_options", "talk_speaker", "talk_text",
		() => TalkFields(UiArea(_talkArea.X, _talkArea.Y, _talkArea.W, _talkArea.H), text, 0, -1, 1, 1, false), text);

	// The box on screen among the speaker's boxes, every page's counted as the window's rows break them: (number, total).
	private (int Box, int Boxes) TalkBoxNumber()
	{
		int Boxes(string text, int upTo)
		{
			int n = 1;
			for (int at = 0; ; n++)
			{
				(_, int next) = TalkFit(text[at..]);
				if (next <= 0 || at + next > upTo)
				{
					return n;
				}
				at += next;
			}
		}
		int before = _talkPages.Take(_talkPage).Sum(p => Boxes(p.Text, int.MaxValue)) + Boxes(_talkPages[_talkPage].Text, _talkStart) - 1;
		return (before + 1, _talkPages.Sum(p => Boxes(p.Text, int.MaxValue)));
	}

	private UiContext TalkFields(UiContext ctx, string text, int shown, int portrait, int box, int boxes, bool more)
	{
		TalkLine? line = _talkSpeakers?[_talkIndex];
		return ctx
			.Set("talk_speaker_name", line?.SpeakerName ?? "")
			.Set("talk_speaker_id", line?.Speaker ?? "")
			.Set("talk_portrait", portrait)
			.Set("talk_text", text)
			.Set("talk_shown", shown)
			.Set("talk_page", box)
			.Set("talk_pages", boxes)
			.Set("talk_index", line is null ? 0 : _talkIndex + 1)
			.Set("talk_count", _talkSpeakers?.Count ?? 0)
			.Set("talk_more", more)
			.Set("page_arrow", PageArrow);
	}

	// The page while the speakers run, or the `talk_speaker` window alone over the undimmed field with the layout's
	// `talk_hide_menu`, given the whole frame as its area.
	private void DrawTalkSpeakers(IDrawSurface s, IDrawSurface t, Art art, int x, int y, int w, int h, string? window)
	{
		_talkArea = (x, y, w, h);
		UiContext data = UiArea(x, y, w, h)
			.Set("options", new List<string> { T("ui.menu.party_talk") })
			.Set("talking", false)
			.Set("talk", new List<UiContext>());
		if (_talkSpeakers is { } speakers)
		{
			DialogueBox box = TalkBox();
			(int shown, bool done) = Revealed(box, _talkMs);
			(int number, int boxes) = TalkBoxNumber();
			TalkFields(data, box.Text, shown, art.Portrait(_talkPortrait, speakers[_talkIndex].Speaker), number, boxes,
				done && (number < boxes || _talkIndex < speakers.Count - 1));
		}
		else
		{
			TalkFields(data, "", 0, -1, 0, 0, false);
		}
		Ui.Draw("menu_options", s, t, data, window);
		_probeSelected = T("ui.menu.party_talk");
	}

	// The `talk` probe: `SPEAKER/SPEAKERS ID page BOX/BOXES KEY SHOWN/END` (the portrait's key, as `shownportrait` reads
	// it, and the box's characters revealed of those it holds), `-` while the speakers do not run.
	private string TalkProbe()
	{
		if (_talkSpeakers is not { } speakers)
		{
			return "-";
		}
		(int number, int boxes) = TalkBoxNumber();
		DialogueBox box = TalkBox();
		string id = speakers[_talkIndex].Speaker;
		return $"{_talkIndex + 1}/{speakers.Count} {id} page {number}/{boxes} {_art?.PortraitOf(_talkPortrait, id).Key ?? "-"} {Revealed(box, _talkMs).Count}/{box.End}";
	}
}
