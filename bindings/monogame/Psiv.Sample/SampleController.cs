using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public enum Screen { Title, Field, Dialogue, Battle, Menu, Shop, Inn, SavePrompt, NameEntry, QuestLog, TextLog }

// The sample's screens: reads the session after each update and draws it through the surface with the
// sample's own generated pictures (Art), rectangles and text. Holds no game state of its own beyond cursors,
// the battle playback queue and what the drawing needs to animate (a clock, the field's glides, and where a
// scene has put party members, which the game leaves to the front end).
public sealed partial class SampleController
{
	private const int TickerMs = 4000;

	private readonly GameSession _s;
	private readonly Action<string> _log;
	// Every script actor, wherever it stands, to tell one outside the party's area from a party member.
	private readonly HashSet<string> _actorIds;
	private readonly FieldRenderer _field = new();
	private Art? _art;
	// Milliseconds of frame time seen, for animations; advances only with the frame times Update gets.
	private int _clockMs;
	private readonly List<(string Text, int MsLeft)> _ticker = [];
	private Screen _lastScreen = (Screen)(-1);
	private string _lastLine = "";
	private bool _questOverlay;
	private bool _logOverlay;
	private int _cursor;
	private int _tab;
	private string? _pickMember;
	private string? _pickAction;
	private string _nameBuffer = "";

	// `controlsPath` is where the bindings load from and save to; null keeps the defaults and saves nothing.
	public SampleController(GameSession session, Action<string>? log = null, string? controlsPath = null)
	{
		_s = session;
		_log = log ?? (_ => { });
		_actorIds = [.. session.ActorIds()];
		ControlsPath = controlsPath;
		_bindings = Bindings.Load(controlsPath, _log);
	}

	public bool QuitRequested { get; private set; }

	// The people drawn in the last field frame (script actors and party members), for headless expectations.
	public IReadOnlyDictionary<string, DrawnPerson> DrawnActors => _field.DrawnActors;

	// The notices on screen (or waiting to be, on a screen without them), oldest first.
	public IEnumerable<string> Toasts => _ticker.Select(t => t.Text);

	public Screen Screen
	{
		get
		{
			if (_battle.Active)
			{
				return Screen.Battle;
			}
			return _s.Mode switch
			{
				Mode.Title => Screen.Title,
				Mode.Field when _questOverlay => Screen.QuestLog,
				Mode.Field when _logOverlay => Screen.TextLog,
				Mode.Field => Screen.Field,
				Mode.Event => Screen.Dialogue,
				Mode.Battle => Screen.Battle,
				Mode.Shop => Screen.Shop,
				Mode.Menu => _s.MenuKind switch
				{
					MenuKind.Inn => Screen.Inn,
					MenuKind.SavePrompt => Screen.SavePrompt,
					MenuKind.NameEntry => Screen.NameEntry,
					_ => Screen.Menu,
				},
				_ => Screen.Title,
			};
		}
	}

	private void Say(string text)
	{
		_ticker.Add((text, TickerMs));
		if (_ticker.Count > 6)
		{
			_ticker.RemoveAt(0);
		}
		_log("notice: " + text);
	}

	private void Report(PsivResult result)
	{
		if (result.Error is { } error)
		{
			Say(error.Text);
		}
	}

	private void TakeNotices()
	{
		foreach (Notice n in _s.DrainNotices())
		{
			if (_battle.Active)
			{
				_battle.Queue.Enqueue(n.Text);
			}
			else if (n.Kind == NoticeKind.Action)
			{
				// Every script action plays on screen (on a sprite, as a fade or as a label), so it stays out of the
				// toasts and only reaches the log.
				_log("notice: (" + n.Text + ")");
			}
			else
			{
				Say(n.Text);
			}
		}
		IReadOnlyList<PresentationAction> actions = _s.PollActions();
		if (actions.Count > 0)
		{
			FieldView? view = _s.Field();
			List<string> lineup = Lineup();
			foreach (PresentationAction a in actions)
			{
				Present(a, view, lineup);
			}
		}
		// Outside an event nothing is scripted: party members walk back into line and poses stop.
		if (_s.Mode == Mode.Field)
		{
			_field.EndScene(Lineup());
		}
		else if (_s.Mode == Mode.Title)
		{
			_field.ClearScene();
		}
	}

	private List<string> Lineup() => _s.Party().Where(m => m.InLineup).Select(m => m.Id).ToList();

	public void Update(int dtMs, InputState raw)
	{
		var input = new ActionInput(raw, _bindings);
		_usingGamepad = raw.UsingGamepad;
		_clockMs += dtMs;
		AdvanceAnimations(dtMs);
		for (int i = _ticker.Count - 1; i >= 0; i--)
		{
			_ticker[i] = (_ticker[i].Text, _ticker[i].MsLeft - dtMs);
			if (_ticker[i].MsLeft <= 0)
			{
				_ticker.RemoveAt(i);
			}
		}
		if (_battle.Active && _s.Mode == Mode.Title && _s.BattleInfo() is null)
		{
			// Quitting to the title drops the battle (after a game over the ended battle stays up until confirmed).
			_battle.Active = false;
		}
		if (_s.Mode == Mode.Battle && !_battle.Active)
		{
			StartBattle();
		}
		Screen screen = Screen;
		if (screen != _lastScreen)
		{
			_log($"screen: {screen}");
			_lastScreen = screen;
			_cursor = 0;
			_tab = 0;
			_pickMember = null;
			_pickAction = null;
			_nameBuffer = "";
		}
		if (Capturing is { } capturing)
		{
			Capture(capturing, raw);
		}
		else
		{
			UpdateScreen(screen, dtMs, input);
		}
		TakeNotices();
	}

	private void UpdateScreen(Screen screen, int dtMs, ActionInput input)
	{
		switch (screen)
		{
		case Screen.Title: UpdateTitle(input); break;
		case Screen.Field: UpdateField(dtMs, input); break;
		case Screen.Dialogue: UpdateDialogue(dtMs, input); break;
		case Screen.Battle: UpdateBattle(dtMs, input); break;
		case Screen.Menu: UpdateMenu(input); break;
		case Screen.Shop: UpdateShop(input); break;
		case Screen.Inn: UpdateInn(input); break;
		case Screen.SavePrompt: UpdateSavePrompt(input); break;
		case Screen.NameEntry: UpdateNameEntry(input); break;
		case Screen.QuestLog:
		case Screen.TextLog:
			if (input.Back || input.Confirm || input.Pressed(GameAction.QuestLog) || input.Pressed(GameAction.TextLog))
			{
				_questOverlay = _logOverlay = false;
			}
			break;
		}
	}

	private int Move(ActionInput input, int count)
	{
		if (count == 0)
		{
			return 0;
		}
		if (input.Pressed(GameAction.Up))
		{
			_cursor = (_cursor + count - 1) % count;
		}
		if (input.Pressed(GameAction.Down))
		{
			_cursor = (_cursor + 1) % count;
		}
		return _cursor = Math.Clamp(_cursor, 0, count - 1);
	}

	private static void DrawHint(IDrawSurface s, string hint)
	{
		s.Fill(0, s.Height - 30, s.Width, 30, Color.Black * 0.6f);
		s.Text(hint, (s.Width - s.Measure(hint, Style.Small)) / 2, s.Height - 25, Style.Dim, Style.Small);
	}

	// The field menu, shop, logs, save prompt and name entry share this: the field dimmed behind a window.
	private void DrawBackdrop(IDrawSurface s, Art art)
	{
		if (DrawField(s, art))
		{
			s.Fill(0, 0, s.Width, s.Height, Color.Black * 0.55f);
		}
	}

	// The newest notices as small boxes stacked up from `bottom`, newest lowest, fading as they expire.
	private void DrawToasts(IDrawSurface s, int x, int bottom, int most = 4)
	{
		int y = bottom;
		for (int i = _ticker.Count - 1; i >= Math.Max(0, _ticker.Count - most); i--)
		{
			(string text, int msLeft) = _ticker[i];
			float alpha = Math.Clamp(msLeft / 400f, 0f, 1f);
			string shown = Style.Fit(s, text, s.Width - 2 * x - 28, Style.Small);
			y -= 38;
			Style.Box(s, x, y, s.Measure(shown, Style.Small) + 28, 32, alpha * 0.95f);
			s.Text(shown, x + 14, y + 7, Style.Text * alpha, Style.Small);
		}
	}

	private void DrawList(IDrawSurface s, int x, int y, IReadOnlyList<string> items, bool cursor = true, int width = 0)
	{
		width = width > 0 ? width : s.Width - x - 70;
		for (int i = 0; i < items.Count; i++)
		{
			Style.ListRow(s, Style.Fit(s, items[i], width - 30), x, y + i * 30, width, cursor && i == _cursor);
		}
	}

	public void Draw(IDrawSurface s)
	{
		Art art = _art ??= new Art(s);
		s.Fill(0, 0, s.Width, s.Height, Style.Background);
		switch (Screen)
		{
		case Screen.Title: DrawTitle(s, art); break;
		case Screen.Field:
			DrawField(s, art);
			DrawHud(s);
			_field.DrawBanner(s);
			DrawToasts(s, 16, s.Height - 40);
			DrawHint(s, $"{Walk()} walk   {Hint(GameAction.Confirm)} talk   {Hint(GameAction.Menu)} menu   {Hint(GameAction.QuestLog)} quests   " +
				$"{Hint(GameAction.PartyTalk)} party talk   {Hint(GameAction.TextLog)} log");
			break;
		case Screen.Dialogue: DrawDialogue(s, art); break;
		case Screen.Battle: DrawBattle(s, art); break;
		case Screen.Menu:
			DrawMenu(s, art);
			DrawToasts(s, 48, s.Height - 92);
			break;
		case Screen.Shop: DrawShop(s, art); break;
		case Screen.Inn: DrawInn(s, art); break;
		case Screen.SavePrompt: DrawSavePrompt(s, art); break;
		case Screen.NameEntry: DrawNameEntry(s, art); break;
		case Screen.QuestLog:
			DrawBackdrop(s, art);
			Style.Window(s, 24, 40, s.Width - 48, s.Height - 72, "Quest log");
			DrawQuestLog(s, 56, 80);
			break;
		case Screen.TextLog:
			DrawBackdrop(s, art);
			Style.Window(s, 24, 40, s.Width - 48, s.Height - 72, "Text log");
			DrawTextLog(s, 56, 72, s.Height - 140);
			break;
		}
		DrawAnimations(s);
	}
}
