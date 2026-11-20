using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

public enum Screen { Title, Field, Dialogue, Battle, Menu, Shop, Inn, SavePrompt, NameEntry, QuestLog, TextLog }

// Keys down this frame and the ones that went down since the last frame.
public sealed class InputState
{
	private readonly HashSet<Keys> _down = [];
	private readonly HashSet<Keys> _pressed = [];

	public void Set(IEnumerable<Keys> down)
	{
		var now = new HashSet<Keys>(down);
		_pressed.Clear();
		foreach (Keys k in now)
		{
			if (!_down.Contains(k))
			{
				_pressed.Add(k);
			}
		}
		_down.Clear();
		_down.UnionWith(now);
	}

	public bool Down(Keys key) => _down.Contains(key);
	public bool Pressed(Keys key) => _pressed.Contains(key);
	public bool Confirm => Pressed(Keys.Enter) || Pressed(Keys.Z) || Pressed(Keys.Space);
	public bool Back => Pressed(Keys.Escape) || Pressed(Keys.X) || Pressed(Keys.Back);
	public IEnumerable<Keys> PressedKeys => _pressed;

	public Direction Held =>
		Down(Keys.Up) ? Direction.North : Down(Keys.Right) ? Direction.East : Down(Keys.Down) ? Direction.South : Down(Keys.Left) ? Direction.West : Direction.None;
}

// The sample's screens: reads the session after each update and draws it through the surface with the
// sample's own generated pictures (Art), rectangles and text. Holds no game state of its own beyond cursors,
// the battle playback queue and what the drawing needs to animate (a clock, the field's glides).
public sealed class SampleController
{
	private const int PlaybackLineMs = 350;
	private const int TickerMs = 4000;
	// Script actions play for their own length, held between these so the sample stays brisk.
	private const int ShortestAnimationMs = 150;
	private const int LongestAnimationMs = 600;

	// A presentation action being played: the session waits on it until FinishAction.
	// An actor action on an actor in view plays on its sprite instead of as a label.
	private sealed class Animation(PresentationAction action, int lengthMs, bool onSprite = false)
	{
		public PresentationAction Action { get; } = action;
		public int LengthMs { get; } = lengthMs;
		public bool OnSprite { get; } = onSprite;
		public int ElapsedMs { get; set; }
		public float Progress => LengthMs == 0 ? 1f : Math.Clamp(ElapsedMs / (float)LengthMs, 0f, 1f);
	}

	private readonly List<Animation> _animations = [];
	// The screen's darkness after the last fade, 0 to 1.
	private float _darkness;

	private readonly GameSession _s;
	private readonly Action<string> _log;
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

	public SampleController(GameSession session, Action<string>? log = null)
	{
		_s = session;
		_log = log ?? (_ => { });
	}

	public bool QuitRequested { get; private set; }

	// The script actors drawn in the last field frame, for headless expectations.
	public IReadOnlyDictionary<string, (float X, float Y, bool Walking)> DrawnActors => _field.DrawnActors;

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
			else
			{
				Say(n.Kind == NoticeKind.Action ? "(" + n.Text + ")" : n.Text);
			}
		}
		foreach (PresentationAction a in _s.PollActions())
		{
			if (a.Phase == ActionPhase.Cancel)
			{
				_animations.RemoveAll(x => x.Action.Handle == a.Handle);
				_log($"action cancelled: {a.Handle}");
				continue;
			}
			_log("action: " + a.Description + (a.Instant ? " (instant)" : ""));
			FieldActor? actor = a.Type is ActionType.Move or ActionType.Face or ActionType.ShowActor or ActionType.HideActor
				? _s.Field()?.Actors.FirstOrDefault(x => x.Id == a.Subject)
				: null;
			(int X, int Y)? drawnAt = actor is null ? null : _field.ActorCell(actor.Id);
			if (a.Instant)
			{
				EndAnimation(a);
				continue;
			}
			if (actor is not null && drawnAt is { } from)
			{
				int length = ShortestAnimationMs;
				if (a.Type == ActionType.Move)
				{
					// The sprite walks there a cell per step time (faster or slower with the move's speed); the event
					// waits until it arrives.
					int cellMs = (int)(Math.Max(1, _s.FieldInfo()!.StepMs) / (a.Speed > 0 ? a.Speed : 1.0));
					_field.Pace(actor.Id, cellMs);
					int cells = Math.Abs(actor.X - from.X) + Math.Abs(actor.Y - from.Y);
					length = cells > 0 ? cells * cellMs : ShortestAnimationMs;
				}
				_animations.Add(new Animation(a, length, onSprite: true));
				continue;
			}
			int labelLength = a.Type is ActionType.Move or ActionType.FadeIn or ActionType.FadeOut or ActionType.CameraMove
				? Math.Clamp((int)(a.Seconds * 1000), ShortestAnimationMs, LongestAnimationMs)
				: ShortestAnimationMs;
			_animations.Add(new Animation(a, labelLength));
		}
	}

	// The end state of an action, for one that finished or was put up at once.
	private void EndAnimation(PresentationAction a)
	{
		if (a.Type == ActionType.FadeOut)
		{
			_darkness = 1f;
		}
		else if (a.Type == ActionType.FadeIn)
		{
			_darkness = 0f;
		}
	}

	private void AdvanceAnimations(int dtMs)
	{
		for (int i = 0; i < _animations.Count; i++)
		{
			Animation animation = _animations[i];
			animation.ElapsedMs += dtMs;
			if (animation.ElapsedMs >= animation.LengthMs)
			{
				EndAnimation(animation.Action);
				_s.FinishAction(animation.Action.Handle);
				_animations.RemoveAt(i--);
			}
		}
	}

	private void DrawAnimations(IDrawSurface s)
	{
		float darkness = _darkness;
		foreach (Animation a in _animations)
		{
			switch (a.Action.Type)
			{
			case ActionType.FadeIn:
				darkness = 1f - a.Progress;
				break;
			case ActionType.FadeOut:
				darkness = a.Progress;
				break;
			case var _ when a.OnSprite:
				break;
			default:
				// Anything else is a label sliding across the top of the screen with its subject.
				string label = a.Action.Subject.Length > 0 ? a.Action.Subject : a.Action.Description;
				int x = 20 + (int)((s.Width - s.Measure(label, Style.Small) - 64) * a.Progress);
				Style.Plate(s, label, x, 52, Style.Accent);
				break;
			}
		}
		if (darkness > 0f)
		{
			s.Fill(0, 0, s.Width, s.Height, Color.Black * darkness);
		}
	}

	public void Update(int dtMs, InputState input)
	{
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
			if (input.Back || input.Confirm || input.Pressed(Keys.Q) || input.Pressed(Keys.L))
			{
				_questOverlay = _logOverlay = false;
			}
			break;
		}
		TakeNotices();
	}

	private int Move(InputState input, int count)
	{
		if (count == 0)
		{
			return 0;
		}
		if (input.Pressed(Keys.Up))
		{
			_cursor = (_cursor + count - 1) % count;
		}
		if (input.Pressed(Keys.Down))
		{
			_cursor = (_cursor + 1) % count;
		}
		return _cursor = Math.Clamp(_cursor, 0, count - 1);
	}

	// Title

	private List<(string Label, Func<PsivResult> Run)> TitleOptions()
	{
		var options = new List<(string, Func<PsivResult>)>();
		if (_s.WorldInfo().CanRetry)
		{
			options.Add(("Retry the battle", _s.RetryBattle));
		}
		options.Add(("New game", _s.NewGame));
		options.Add(("Continue", _s.ContinueGame));
		foreach (SaveSummary save in _s.Saves()?.Where(x => !x.Damaged && !x.Autosave) ?? [])
		{
			options.Add(($"Load {save.Slot} ({save.Area})", () => _s.Load(save.Slot)));
		}
		options.Add(("Quit", () =>
		{
			QuitRequested = true;
			return PsivResult.Success;
		}));
		return options;
	}

	private void UpdateTitle(InputState input)
	{
		var options = TitleOptions();
		Move(input, options.Count);
		if (input.Confirm)
		{
			Report(options[_cursor].Run());
		}
	}

	private void DrawTitle(IDrawSurface s, Art art)
	{
		Art.TileSet tiles = art.Tiles(Theme.Field);
		static CellFlags Terrain(int x, int y) => y <= 1 ? CellFlags.Wall : y is 11 or 12 ? CellFlags.Water : CellFlags.Floor;
		int frame = _clockMs / 220 % Art.WaterFrames;
		for (int y = 0; y * Art.Tile < s.Height; y++)
		{
			for (int x = 0; x * Art.Tile < s.Width; x++)
			{
				FieldRenderer.DrawCell(s, tiles, Terrain, x, y, x * Art.Tile, y * Art.Tile, Noise.Hash(x, y, 7), frame);
			}
		}
		s.Fill(0, 0, s.Width, s.Height, Style.Background * 0.35f);

		var options = TitleOptions();
		int w = 600;
		int h = 176 + options.Count * 32;
		int bx = (s.Width - w) / 2;
		int by = (s.Height - h) / 2;
		Style.Box(s, bx, by, w, h);
		string title = _s.GameInfo().Title;
		s.Text(title, (s.Width - s.Measure(title, Style.Title)) / 2, by + 26, Style.Accent, Style.Title);
		const string subtitle = "A placeholder adventure played through libpsiv_session";
		s.Text(subtitle, (s.Width - s.Measure(subtitle, Style.Small)) / 2, by + 66, Style.Dim, Style.Small);
		int walk = 1 + _clockMs / 260 % 2;
		s.Image(art.ActorFrame(Art.Hero, Direction.East, walk == 1 ? 1 : 0), bx + 28, by + 18, 64, 64, Color.White);
		s.Image(art.Creature(Noise.Hash("title"), _clockMs / 400 % 2), bx + w - 92, by + 18, 64, 64, Color.White);
		if (_s.WorldInfo().GameOver)
		{
			const string fallen = "The party has fallen.";
			s.Text(fallen, (s.Width - s.Measure(fallen)) / 2, by + 96, Style.Bad);
		}
		for (int i = 0; i < options.Count; i++)
		{
			Style.ListRow(s, options[i].Label, bx + 80, by + 132 + i * 32, w - 140, i == _cursor);
		}
		DrawHint(s, "Up/Down choose   Enter confirm");
	}

	// Field

	private void UpdateField(int dtMs, InputState input)
	{
		if (input.Confirm)
		{
			Report(_s.Interact());
		}
		else if (input.Pressed(Keys.M) || input.Pressed(Keys.Escape))
		{
			Report(_s.OpenMenu());
		}
		else if (input.Pressed(Keys.T))
		{
			Report(_s.PartyTalk());
		}
		else if (input.Pressed(Keys.Q))
		{
			_questOverlay = true;
		}
		else if (input.Pressed(Keys.L))
		{
			_logOverlay = true;
		}
		_s.Update(dtMs, input.Held);
	}

	private bool DrawField(IDrawSurface s, Art art)
	{
		FieldView? view = _s.Field();
		if (view is null)
		{
			return false;
		}
		List<string> lineup = _s.Party().Where(m => m.InLineup).Select(m => m.Id).ToList();
		_field.Draw(s, art, view, _clockMs, lineup);
		return true;
	}

	private void DrawHud(IDrawSurface s)
	{
		FieldInfo? f = _s.FieldInfo();
		if (f is null)
		{
			return;
		}
		WorldInfo w = _s.WorldInfo();
		long seconds = w.PlayTimeMs / 1000;
		Style.Plate(s, f.AreaName, 16, 12, Style.Accent);
		string status = $"Money {w.Money}    {seconds / 3600}:{seconds / 60 % 60:00}:{seconds % 60:00}";
		Style.Plate(s, status, s.Width - s.Measure(status, Style.Small) - 40, 12, Style.Text);
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

	// Dialogue

	private void UpdateDialogue(int dtMs, InputState input)
	{
		if (input.Pressed(Keys.Tab) && _s.CanSkip)
		{
			Report(_s.Skip());
			return;
		}
		if (_s.Line() is { } line)
		{
			if (line.Display != _lastLine)
			{
				_lastLine = line.Display;
				_log("line: " + line.Display);
			}
			if (input.Confirm)
			{
				_s.ConfirmLine();
				_lastLine = "";
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

	private const int DialogueHeight = 176;

	// The box sits at the bottom of the screen, or at the top when the leader stands where it would cover them.
	private int DialogueTop(IDrawSurface s, bool field)
	{
		int bottom = s.Height - DialogueHeight - 16;
		return field && _field.LeaderScreenY + Art.Tile > bottom - 8 ? 36 : bottom;
	}

	private void DrawDialogue(IDrawSurface s, Art art)
	{
		bool field = DrawField(s, art);
		int y = DialogueTop(s, field);
		if (y > s.Height / 2)
		{
			_field.DrawBanner(s);
		}
		int x = 24;
		int w = s.Width - 48;
		// Between lines (a script actor walking, a fade) the field shows without an empty box.
		if (!_s.LinePending && _s.Choice() is null)
		{
			return;
		}
		Style.Box(s, x, y, w, DialogueHeight);
		if (_s.Line() is { } line)
		{
			int tx = x + 26;
			if (line.SpeakerName.Length > 0)
			{
				Style.Plate(s, line.SpeakerName, x + 18, y - 16, Style.Accent, Style.Body);
				int px = x + 22;
				int py = y + 34;
				s.Fill(px - 2, py - 2, 100, 100, Style.PanelEdge);
				s.Fill(px, py, 96, 96, Style.Track);
				s.Fill(px, py + 56, 96, 40, Style.PanelTop);
				ActorLook look = Art.LookFor(line.SpeakerId.Length > 0 ? line.SpeakerId : line.SpeakerName);
				s.Image(art.ActorFrame(look, Direction.South, 0), px + 8, py + 8, 80, 80, Color.White);
				tx = px + 122;
			}
			int ty = y + 30;
			foreach (string l in Style.Wrap(s, line.Text, x + w - tx - 30))
			{
				s.Text(l, tx, ty, Style.Text);
				ty += Style.LineHeight;
			}
			string hint = "Enter next" + (_s.CanSkip ? "   Tab skip" : "");
			s.Text(hint, x + w - s.Measure(hint, Style.Small) - 58, y + DialogueHeight - 32, Style.Dim, Style.Small);
			s.Image(art.Pointer, x + w - 48, y + DialogueHeight - 40 + _clockMs / 260 % 2 * 3, 24, 24, Color.White);
		}
		else if (_s.Choice() is { } choice)
		{
			s.Text(choice.Prompt.Length == 0 ? "Choose:" : choice.Prompt, x + 26, y + 22, Style.Accent);
			for (int i = 0; i < choice.Options.Count; i++)
			{
				Style.ListRow(s, choice.Options[i], x + 44, y + 62 + i * 30, 420, i == _cursor);
			}
		}
	}

	// Battle

	private enum BattlePhase { Command, Option, Target, Macro, Playback, Ended }

	private sealed class BattleUi
	{
		public bool Active;
		public BattlePhase Phase;
		public int Actor;
		public string MenuChoice = "";
		public List<BattleOption> Options = [];
		public BattleOption? Option;
		public List<int> Targets = [];
		public List<string> Labels = [];
		public readonly Queue<string> Queue = new();
		public readonly List<string> Log = [];
		public int PlaybackMs;
		public bool Auto;
	}

	private readonly BattleUi _battle = new();

	private static readonly string[] CommandMenu = ["Attack", "Technique", "Skill", "Item", "Defend", "Escape", "Macro", "Auto"];

	private void StartBattle()
	{
		_battle.Active = true;
		_battle.Labels = _s.Combatants().Select(c => c.Label).ToList();
		_battle.Log.Clear();
		_battle.Queue.Clear();
		_battle.Auto = false;
		string enemies = string.Join(", ", _s.Combatants().Where(c => c.Side == Side.Enemy).Select(c => c.Label));
		_battle.Queue.Enqueue($"Enemies: {enemies}.");
		_battle.Phase = BattlePhase.Playback;
	}

	private void NextActor(int from)
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		BattleInfo? info = _s.BattleInfo();
		if (info is not null && info.Live && info.PartyActsNextRound)
		{
			for (int i = from; i < info.PartyCount; i++)
			{
				if (all[i].Alive)
				{
					_battle.Actor = i;
					_battle.Phase = BattlePhase.Command;
					_cursor = 0;
					return;
				}
			}
		}
		Resolve();
	}

	private void Resolve()
	{
		if (_battle.Auto && !_s.FillFromAuto())
		{
			_battle.Auto = false;
			TakeNotices();
			NextActor(0);
			return;
		}
		foreach (BattleEvent e in _s.ResolveRound())
		{
			if (BattleText.Describe(_s, _battle.Labels, e) is { } line)
			{
				_battle.Queue.Enqueue(line);
			}
		}
		TakeNotices();
		_battle.Phase = BattlePhase.Playback;
		_battle.PlaybackMs = 0;
	}

	private void SetAndAdvance(CommandKind kind, string? id, int target)
	{
		PsivResult result = _s.SetCommand(_battle.Actor, kind, id, target);
		if (!result.Ok)
		{
			_battle.Log.Add("! " + result.Error!.Text);
			_battle.Phase = BattlePhase.Command;
			return;
		}
		NextActor(_battle.Actor + 1);
	}

	private void ChooseTargets(Targeting targeting)
	{
		IReadOnlyList<Combatant> all = _s.Combatants();
		_battle.Targets = targeting switch
		{
			Targeting.OneEnemy => all.Where(c => c.Side == Side.Enemy && c.Alive).Select(c => c.Index).ToList(),
			Targeting.OneAlly => all.Where(c => c.Side == Side.Party && c.Alive).Select(c => c.Index).ToList(),
			Targeting.OneFallenAlly => all.Where(c => c.Side == Side.Party && !c.Alive).Select(c => c.Index).ToList(),
			_ => [],
		};
	}

	private void UpdateBattle(int dtMs, InputState input)
	{
		BattleUi b = _battle;
		switch (b.Phase)
		{
		case BattlePhase.Playback:
			if (b.Auto && input.Back)
			{
				_s.CancelAuto();
				b.Log.Add("Auto-battle will stop after this round.");
			}
			b.PlaybackMs += dtMs;
			bool hurry = input.Confirm;
			while (b.Queue.Count > 0 && (hurry || b.PlaybackMs >= PlaybackLineMs))
			{
				b.PlaybackMs = 0;
				string line = b.Queue.Dequeue();
				b.Log.Add(line);
				_log("battle: " + line);
			}
			if (b.Queue.Count == 0 && b.PlaybackMs >= PlaybackLineMs)
			{
				if (_s.Mode != Mode.Battle)
				{
					b.Phase = BattlePhase.Ended;
				}
				else if (b.Auto)
				{
					Resolve();
				}
				else
				{
					NextActor(0);
				}
			}
			break;
		case BattlePhase.Ended:
			if (input.Confirm || input.Back)
			{
				b.Active = false;
			}
			break;
		case BattlePhase.Command:
			Move(input, CommandMenu.Length);
			if (!input.Confirm)
			{
				break;
			}
			b.MenuChoice = CommandMenu[_cursor];
			_cursor = 0;
			switch (b.MenuChoice)
			{
			case "Attack":
				ChooseTargets(Targeting.OneEnemy);
				b.Option = null;
				b.Phase = BattlePhase.Target;
				break;
			case "Technique":
			case "Skill":
			case "Item":
				b.Options = _s.BattleOptions(b.Actor).Where(o => b.MenuChoice switch
				{
					"Technique" => o.AbilityKind == AbilityKind.Technique,
					"Skill" => o.AbilityKind == AbilityKind.Skill,
					_ => o.Command == CommandKind.Item,
				}).ToList();
				b.Phase = b.Options.Count > 0 ? BattlePhase.Option : BattlePhase.Command;
				if (b.Options.Count == 0)
				{
					b.Log.Add($"No {b.MenuChoice.ToLowerInvariant()} to use.");
				}
				break;
			case "Defend":
				SetAndAdvance(CommandKind.Defend, null, -1);
				break;
			case "Escape":
				SetAndAdvance(CommandKind.Escape, null, -1);
				break;
			case "Macro":
				b.Phase = BattlePhase.Macro;
				break;
			case "Auto":
				if (_s.Settings().AutoMacro.Length == 0 && _s.Macros().Count > 0)
				{
					Report(_s.SetAutoMacro(_s.Macros()[0]));
				}
				PsivResult auto = _s.StartAuto();
				if (auto.Ok)
				{
					b.Auto = true;
					TakeNotices();
					Resolve();
				}
				else
				{
					b.Log.Add("! " + auto.Error!.Text);
				}
				break;
			}
			break;
		case BattlePhase.Option:
			Move(input, b.Options.Count);
			if (input.Back)
			{
				b.Phase = BattlePhase.Command;
			}
			else if (input.Confirm)
			{
				b.Option = b.Options[_cursor];
				_cursor = 0;
				ChooseTargets(b.Option.Targeting);
				if (b.Targets.Count == 0 && b.Option.Targeting is not (Targeting.OneEnemy or Targeting.OneAlly or Targeting.OneFallenAlly))
				{
					SetAndAdvance(b.Option.Command, b.Option.Id, -1);
				}
				else
				{
					b.Phase = BattlePhase.Target;
				}
			}
			break;
		case BattlePhase.Target:
			Move(input, b.Targets.Count);
			if (input.Back || b.Targets.Count == 0)
			{
				b.Phase = BattlePhase.Command;
			}
			else if (input.Confirm)
			{
				int target = b.Targets[_cursor];
				if (b.Option is null)
				{
					SetAndAdvance(CommandKind.Attack, null, target);
				}
				else
				{
					SetAndAdvance(b.Option.Command, b.Option.Id, target);
				}
			}
			break;
		case BattlePhase.Macro:
			IReadOnlyList<string> macros = _s.Macros();
			Move(input, macros.Count);
			if (input.Back || macros.Count == 0)
			{
				b.Phase = BattlePhase.Command;
			}
			else if (input.Confirm)
			{
				PsivResult applied = _s.ApplyMacro(macros[_cursor], out IReadOnlyList<MacroStepResult> steps);
				if (!applied.Ok)
				{
					b.Log.Add("! " + applied.Error!.Text);
					b.Phase = BattlePhase.Command;
				}
				else
				{
					foreach (MacroStepResult step in steps.Where(x => x.Status != MacroStepStatus.Applied))
					{
						b.Log.Add($"  {step.Member}: {GameSession.NameOf(NameKind.MacroStepStatus, (int)step.Status)} ({GameSession.NameOf(NameKind.MacroIssue, (int)step.Issue)})");
					}
					Resolve();
				}
			}
			break;
		}
	}

	private const int BattleGroundY = 300;
	private const int BattleFootY = 366;
	private const int BattlePanelTop = 420;
	private const int BattleMenuWidth = 300;
	private const int BattleLogLines = 5;

	private static void DrawBattleBackdrop(IDrawSurface s, Art art, Theme theme)
	{
		(Color top, Color bottom, Color far) = theme switch
		{
			Theme.Town => (new Color(48, 54, 112), new Color(226, 160, 136), new Color(60, 54, 96)),
			Theme.Dungeon => (new Color(12, 10, 18), new Color(54, 46, 68), new Color(30, 26, 40)),
			_ => (new Color(92, 156, 222), new Color(204, 230, 242), new Color(70, 128, 90)),
		};
		const int bands = 20;
		for (int i = 0; i < bands; i++)
		{
			s.Fill(0, i * BattleGroundY / bands, s.Width, BattleGroundY / bands + 1, Color.Lerp(top, bottom, i / (bands - 1f)));
		}
		for (int x = 0; x < s.Width; x += 8)
		{
			switch (theme)
			{
			case Theme.Town:
				int roof = 50 + (int)(Noise.Hash(x / 72, 3) % 5) * 22;
				s.Fill(x, BattleGroundY - roof, 8, roof, far);
				if (x % 72 == 24 && Noise.Hash(x, 4) % 3 != 0)
				{
					s.Fill(x, BattleGroundY - roof + 16, 10, 10, new Color(250, 214, 120));
				}
				break;
			case Theme.Dungeon:
				int span = (int)(Noise.Hash(x / 48, 5) % 90) + 20;
				int hang = span * (24 - Math.Abs(x % 48 - 24)) / 24;
				s.Fill(x, 0, 8, hang, far);
				int ridge = 26 + (int)(16 * MathF.Sin(x / 60f)) + (int)(Noise.Hash(x / 16, 9) % 10);
				s.Fill(x, BattleGroundY - ridge, 8, ridge, far);
				break;
			default:
				int hill = 44 + (int)(26 * MathF.Sin(x / 110f) + 14 * MathF.Sin(x / 41f + 1.3f));
				s.Fill(x, BattleGroundY - hill, 8, hill, far);
				break;
			}
		}
		Art.TileSet tiles = art.Tiles(theme);
		for (int y = BattleGroundY; y < s.Height; y += Art.Tile)
		{
			for (int x = 0; x < s.Width; x += Art.Tile)
			{
				s.Image(tiles.Floor[Noise.Hash(x, y) % (uint)tiles.Floor.Length], x, y, Art.Tile, Art.Tile, Color.White);
			}
		}
		s.Fill(0, BattleGroundY, s.Width, 18, Color.Black * 0.3f);
	}

	private static string OutcomeText(Outcome outcome) => outcome switch
	{
		Outcome.Victory => "Victory!",
		Outcome.Defeat => "Defeated...",
		Outcome.Escaped => "Got away.",
		Outcome.ScriptedEnd => "The battle ends.",
		_ => "",
	};

	private void DrawBattle(IDrawSurface s, Art art)
	{
		BattleUi b = _battle;
		IReadOnlyList<Combatant> all = _s.Combatants();
		DrawBattleBackdrop(s, art, Art.ThemeOf(_s.FieldInfo()?.AreaKind ?? AreaKind.Field));
		bool commanding = b.Phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro;
		int target = b.Phase == BattlePhase.Target && b.Targets.Count > 0 ? b.Targets[Math.Clamp(_cursor, 0, b.Targets.Count - 1)] : -1;
		float pulse = 0.5f + 0.5f * MathF.Sin(_clockMs / 140f);
		static string Statuses(Combatant c) => string.Join(" ", c.Statuses.Where(st => st.Id != "defeated").Select(st => st.Name));

		// Enemies stand on the ground, spread across the screen; a fallen one leaves its place empty.
		var enemies = all.Where(c => c.Side == Side.Enemy).ToList();
		int size = enemies.Count <= 4 ? 112 : 96;
		int spacing = Math.Min(240, (s.Width - 80) / Math.Max(1, enemies.Count));
		for (int i = 0; i < enemies.Count; i++)
		{
			Combatant e = enemies[i];
			if (!e.Alive)
			{
				continue;
			}
			int cx = s.Width / 2 + (int)((i - (enemies.Count - 1) / 2f) * spacing);
			int top = BattleFootY - size;
			s.Image(art.Shadow, cx - size / 2, top + 10, size, size, Color.White);
			s.Image(art.Creature(Noise.Hash(e.Id), (_clockMs / 400 + i) % 2), cx - size / 2, top, size, size, Color.White);
			bool targeted = e.Index == target;
			if (targeted)
			{
				s.Image(art.Pointer, cx - 18, top - 40 + (int)(5 * pulse), 36, 36, Color.White);
			}
			string statuses = Statuses(e);
			string label = Style.Fit(s, e.Label + (statuses.Length > 0 ? "  " + statuses : ""), spacing - 24, Style.Small);
			int lw = s.Measure(label, Style.Small);
			s.Fill(cx - lw / 2 - 8, BattleFootY + 4, lw + 16, 24, Color.Black * 0.6f);
			s.Text(label, cx - lw / 2, BattleFootY + 7, targeted ? Style.Accent : Style.Text, Style.Small);
			Style.Bar(s, cx - 50, BattleFootY + 34, 100, 6, e.Hp, e.MaxHp, Style.Bad);
		}

		// The event log across the top: the newest entry bright, the ones before it dimmer.
		Style.Box(s, 16, 12, s.Width - 32, BattleLogLines * 22 + 24, 0.94f);
		var wrapped = new List<(string Text, bool Newest)>();
		for (int i = Math.Max(0, b.Log.Count - BattleLogLines); i < b.Log.Count; i++)
		{
			foreach (string l in Style.Wrap(s, b.Log[i], s.Width - 80, Style.Small))
			{
				wrapped.Add((l, i == b.Log.Count - 1));
			}
		}
		var shown = wrapped.Skip(Math.Max(0, wrapped.Count - BattleLogLines)).ToList();
		for (int i = 0; i < shown.Count; i++)
		{
			s.Text(shown[i].Text, 36, 24 + i * 22, shown[i].Newest ? Style.Text : Style.Dim, Style.Small);
		}

		// The party, one column each: portrait, name, statuses, HP and TP bars, the command already chosen.
		var party = all.Where(c => c.Side == Side.Party).ToList();
		int panelW = s.Width - 32 - BattleMenuWidth - 12;
		int panelH = s.Height - BattlePanelTop - 16;
		Style.Box(s, 16, BattlePanelTop, panelW, panelH);
		int colW = (panelW - 16) / Math.Max(1, party.Count);
		for (int i = 0; i < party.Count; i++)
		{
			Combatant c = party[i];
			int cx = 24 + i * colW;
			int cy = BattlePanelTop + 10;
			int cw = colW - 8;
			int ch = panelH - 20;
			bool acting = commanding && c.Index == b.Actor;
			bool targeted = c.Index == target;
			if (acting)
			{
				s.Fill(cx, cy, cw, ch, Style.Row);
			}
			if (acting || targeted)
			{
				Style.Frame(s, cx, cy, cw, ch, targeted ? Style.Accent * (0.4f + 0.6f * pulse) : Style.Accent);
			}
			ActorLook look = Art.LookFor(c.Id);
			int frame = acting ? 1 + _clockMs / 260 % 2 : 0;
			s.Image(art.ActorFrame(look, Direction.South, frame), cx + 8, cy + 8, 64, 64, c.Alive ? Color.White : new Color(80, 80, 92));
			s.Text(Style.Fit(s, c.Name, cw - 90), cx + 80, cy + 12, !c.Alive ? Style.Dim : acting ? Style.Accent : Style.Text);
			string state = !c.Alive ? "Fallen" : ((c.Defending ? "Defending " : "") + Statuses(c)).Trim();
			if (state.Length > 0)
			{
				s.Text(Style.Fit(s, state, cw - 90, Style.Small), cx + 80, cy + 42, c.Alive ? Style.Dim : Style.Bad, Style.Small);
			}
			int bw = cw - 24;
			void Gauge(string name, int value, int max, int y, Color color)
			{
				string numbers = $"{value}/{max}";
				s.Text(name, cx + 12, y, Style.Dim, Style.Small);
				s.Text(numbers, cx + 12 + bw - s.Measure(numbers, Style.Small), y, Style.Text, Style.Small);
				Style.Bar(s, cx + 12, y + 22, bw, 10, value, max, color);
			}
			Gauge("HP", c.Hp, c.MaxHp, cy + 86, c.Hp * 4 <= c.MaxHp ? Style.Bad : Style.Good);
			Gauge("TP", c.Tp, c.MaxTp, cy + 130, Style.Tp);
			if (c.CommandKind != CommandKind.None)
			{
				string command = c.CommandId.Length > 0 ? _s.DisplayName(c.CommandId) : GameSession.NameOf(NameKind.CommandKind, (int)c.CommandKind);
				s.Text(Style.Fit(s, "Next: " + command, bw, Style.Small), cx + 12, cy + 180, Style.Dim, Style.Small);
			}
		}

		int mx = s.Width - 16 - BattleMenuWidth;
		string actor = b.Actor < all.Count ? all[b.Actor].Name : "";
		string title = b.Phase switch
		{
			BattlePhase.Command => actor,
			BattlePhase.Option => b.MenuChoice,
			BattlePhase.Target => "Target",
			BattlePhase.Macro => "Macro",
			BattlePhase.Ended => "Battle over",
			_ => "Battle",
		};
		Style.Window(s, mx, BattlePanelTop, BattleMenuWidth, panelH, title);
		void Menu(IReadOnlyList<string> items, IReadOnlyList<bool>? usable = null)
		{
			int rows = (panelH - 44) / 28;
			int first = Math.Clamp(_cursor - rows + 1, 0, Math.Max(0, items.Count - rows));
			for (int i = first; i < Math.Min(items.Count, first + rows); i++)
			{
				Style.ListRow(s, Style.Fit(s, items[i], BattleMenuWidth - 70, Style.Small), mx + 24, BattlePanelTop + 30 + (i - first) * 28,
					BattleMenuWidth - 36, i == _cursor, usable is null || usable[i], Style.Small);
			}
		}
		switch (b.Phase)
		{
		case BattlePhase.Command:
			Menu(CommandMenu);
			break;
		case BattlePhase.Option:
			Menu(b.Options.Select(o => o.Name + (o.Cost > 0 ? $" ({o.Cost} TP)" : "") + (o.Uses >= 0 ? $" x{o.Uses}" : "")).ToList(), b.Options.Select(o => o.Usable).ToList());
			break;
		case BattlePhase.Target:
			Menu(b.Targets.Select(t => t < all.Count ? (all[t].Side == Side.Party ? all[t].Name : all[t].Label) : "?").ToList());
			break;
		case BattlePhase.Macro:
			Menu(_s.Macros());
			break;
		case BattlePhase.Playback:
			s.Text(b.Auto ? "Auto-battle" : "...", mx + 24, BattlePanelTop + 34, Style.Text);
			if (b.Auto)
			{
				s.Text("Esc stops after this round", mx + 24, BattlePanelTop + 64, Style.Dim, Style.Small);
			}
			s.Text("Enter hurries", mx + 24, BattlePanelTop + panelH - 34, Style.Dim, Style.Small);
			break;
		case BattlePhase.Ended:
			s.Text(OutcomeText(_s.BattleInfo()?.Outcome ?? Outcome.Ongoing), mx + 24, BattlePanelTop + 34, Style.Accent, Style.Title);
			s.Text("Enter: continue", mx + 24, BattlePanelTop + 84, Style.Text);
			break;
		}
	}

	// Field menu

	private static readonly string[] MenuTabs = ["Party", "Items", "Equip", "Save", "Quests", "Codex", "Log"];

	private void UpdateMenu(InputState input)
	{
		if (_pickMember is null && _pickAction is null)
		{
			if (input.Pressed(Keys.Left))
			{
				_tab = (_tab + MenuTabs.Length - 1) % MenuTabs.Length;
				_cursor = 0;
			}
			if (input.Pressed(Keys.Right))
			{
				_tab = (_tab + 1) % MenuTabs.Length;
				_cursor = 0;
			}
			if (input.Back)
			{
				Report(_s.CloseMenu());
				return;
			}
		}
		else if (input.Back)
		{
			_pickMember = null;
			_pickAction = null;
			_cursor = 0;
			return;
		}
		List<Member> lineup = _s.Party().Where(m => m.InLineup).ToList();
		switch (MenuTabs[_tab])
		{
		case "Party":
			if (_pickMember is null)
			{
				Move(input, lineup.Count);
				if (input.Confirm && lineup.Count > 0)
				{
					_pickMember = lineup[_cursor].Id;
					_cursor = 0;
				}
			}
			else if (_pickAction is null)
			{
				var techniques = _s.MemberAbilities(_pickMember).Where(a => a.Field).ToList();
				Move(input, techniques.Count);
				if (input.Confirm && techniques.Count > 0)
				{
					AbilityInfo ability = techniques[_cursor];
					if (ability.Targeting is Targeting.OneAlly or Targeting.OneFallenAlly)
					{
						_pickAction = ability.Id;
						_cursor = 0;
					}
					else
					{
						Report(_s.UseAbility(_pickMember, ability.Id));
					}
				}
			}
			else
			{
				Move(input, lineup.Count);
				if (input.Confirm)
				{
					Report(_s.UseAbility(_pickMember, _pickAction, lineup[_cursor].Id));
					_pickAction = null;
					_cursor = 0;
				}
			}
			break;
		case "Items":
			var items = _s.Inventory().Where(i => !i.IsEquipment).ToList();
			if (_pickAction is null)
			{
				Move(input, items.Count);
				if (input.Confirm && items.Count > 0)
				{
					InventoryEntry item = items[_cursor];
					if (item.Targeting is Targeting.OneAlly or Targeting.OneFallenAlly)
					{
						_pickAction = item.Id;
						_cursor = 0;
					}
					else
					{
						Report(_s.UseItem(item.Id));
					}
				}
			}
			else
			{
				Move(input, lineup.Count);
				if (input.Confirm)
				{
					Report(_s.UseItem(_pickAction, lineup[_cursor].Id));
					_pickAction = null;
					_cursor = 0;
				}
			}
			break;
		case "Equip":
			if (_pickMember is null)
			{
				Move(input, lineup.Count);
				if (input.Confirm && lineup.Count > 0)
				{
					_pickMember = lineup[_cursor].Id;
					_cursor = 0;
				}
			}
			else
			{
				List<(string Label, Func<PsivResult> Run)> choices = EquipChoices(_pickMember);
				Move(input, choices.Count);
				if (input.Confirm && choices.Count > 0)
				{
					Report(choices[_cursor].Run());
					_cursor = 0;
				}
			}
			break;
		case "Save":
			int slots = _s.GameInfo().ManualSlots;
			Move(input, slots);
			if (input.Confirm)
			{
				Report(_s.SaveManual($"manual_{_cursor}"));
			}
			break;
		}
	}

	private List<(string Label, Func<PsivResult> Run)> EquipChoices(string memberId)
	{
		var choices = new List<(string, Func<PsivResult>)>();
		Member? member = _s.Party().FirstOrDefault(m => m.Id == memberId);
		if (member is null)
		{
			return choices;
		}
		foreach (InventoryEntry piece in _s.Inventory().Where(i => i.IsEquipment))
		{
			choices.Add(($"Equip {piece.Name} ({GameSession.NameOf(NameKind.EquipSlot, (int)piece.Slot)})", () => _s.Equip(memberId, piece.Id)));
		}
		for (int slot = 0; slot < member.Equipment.Count; slot++)
		{
			if (member.Equipment[slot].Length > 0)
			{
				var equipSlot = (EquipSlot)slot;
				choices.Add(($"Remove {member.EquipmentNames[slot]}", () => _s.Unequip(memberId, equipSlot)));
			}
		}
		return choices;
	}

	private void DrawList(IDrawSurface s, int x, int y, IReadOnlyList<string> items, bool cursor = true, int width = 0)
	{
		width = width > 0 ? width : s.Width - x - 70;
		for (int i = 0; i < items.Count; i++)
		{
			Style.ListRow(s, Style.Fit(s, items[i], width - 30), x, y + i * 30, width, cursor && i == _cursor);
		}
	}

	private void DrawMenu(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		Style.Window(s, 24, 56, s.Width - 48, s.Height - 72, "Menu");
		Style.Tabs(s, 48, 64, MenuTabs, _tab);
		string money = $"Money {_s.WorldInfo().Money}";
		s.Text(money, s.Width - 64 - s.Measure(money), 69, Style.Text);
		List<Member> lineup = _s.Party().Where(m => m.InLineup).ToList();
		int y = 124;
		switch (MenuTabs[_tab])
		{
		case "Party":
			if (_pickMember is null || _pickAction is not null)
			{
				DrawList(s, 56, y, lineup.Select(m => $"{m.Name}  Lv{m.Level}  HP {m.Hp}/{m.MaxHp}  TP {m.Tp}/{m.MaxTp}  EXP {m.Experience}" +
					string.Concat(m.Statuses.Select(st => $" [{st.Name}]"))).ToList());
				if (_pickAction is not null)
				{
					s.Text($"Use {_s.DisplayName(_pickAction)} on whom?", 56, s.Height - 104, Style.Accent);
				}
			}
			else
			{
				Member m = lineup.First(x => x.Id == _pickMember);
				s.Text($"{m.Name}: STR {m.Strength}  MEN {m.Mental}  AGI {m.Agility}  DEX {m.Dexterity}  DEF {m.Defense}", 56, y, Style.Text);
				s.Text("Equipment: " + string.Join(", ", m.EquipmentNames.Where(n => n.Length > 0)), 56, y + 30, Style.Dim, Style.Small);
				var techniques = _s.MemberAbilities(_pickMember).Where(a => a.Field).ToList();
				s.Text(techniques.Count == 0 ? "No techniques to use here." : "Use a technique:", 56, y + 70, Style.Accent);
				DrawList(s, 56, y + 100, techniques.Select(a => $"{a.Name} ({a.Cost} TP)").ToList());
			}
			break;
		case "Items":
			var items = _s.Inventory().Where(i => !i.IsEquipment).ToList();
			DrawList(s, 56, y, items.Select(i => $"{i.Name} x{i.Count}" + (i.Field ? "" : "  (battle only)")).ToList(), _pickAction is null, _pickAction is null ? 0 : 520);
			if (_pickAction is not null)
			{
				DrawList(s, 620, y, lineup.Select(m => $"{m.Name}  HP {m.Hp}/{m.MaxHp}").ToList());
			}
			break;
		case "Equip":
			if (_pickMember is null)
			{
				DrawList(s, 56, y, lineup.Select(m => m.Name + ": " + string.Join(", ", m.EquipmentNames.Where(n => n.Length > 0))).ToList());
			}
			else
			{
				DrawList(s, 56, y, EquipChoices(_pickMember).Select(c => c.Label).ToList());
			}
			break;
		case "Save":
			var saves = _s.Saves() ?? [];
			int slots = _s.GameInfo().ManualSlots;
			DrawList(s, 56, y, Enumerable.Range(0, slots).Select(i =>
			{
				SaveSummary? existing = saves.FirstOrDefault(x => x.Slot == $"manual_{i}");
				return $"manual_{i}  " + (existing is null ? "(empty)" : existing.Damaged ? "(damaged)" : $"{existing.Area} {string.Join(" ", existing.Party)}");
			}).ToList());
			break;
		case "Quests":
			DrawQuestLog(s, 56, y);
			break;
		case "Codex":
			int cy = y;
			foreach (CodexEntry entry in _s.Encyclopedia())
			{
				s.Text(entry.Title, 56, cy, Style.Accent);
				cy += Style.LineHeight;
				foreach (string l in Style.Wrap(s, entry.Text, s.Width - 150, Style.Small))
				{
					s.Text(l, 76, cy, Style.Text, Style.Small);
					cy += 22;
				}
				cy += 10;
			}
			break;
		case "Log":
			DrawTextLog(s, 56, y, s.Height - y - 100);
			break;
		}
		s.Text("Left/Right tab   Up/Down choose   Enter use   Esc back", 56, s.Height - 66, Style.Dim, Style.Small);
	}

	private void DrawQuestLog(IDrawSurface s, int x, int y)
	{
		QuestLog log = _s.QuestLog();
		s.Text("Objective", x, y, Style.Accent);
		s.Text(log.Objective.Length == 0 ? "-" : log.Objective, x + 20, y + 30, Style.Text);
		s.Text("Jobs", x, y + 80, Style.Accent);
		for (int i = 0; i < log.Jobs.Count; i++)
		{
			s.Text($"{log.Jobs[i].Title} ({GameSession.NameOf(NameKind.JobState, (int)log.Jobs[i].State)})", x + 20, y + 110 + i * Style.LineHeight, Style.Text);
		}
	}

	private void DrawTextLog(IDrawSurface s, int x, int y, int height)
	{
		IReadOnlyList<string> pages = _s.TextLog();
		int lines = Math.Max(1, height / 22);
		var visible = pages.Skip(Math.Max(0, pages.Count - lines)).ToList();
		for (int i = 0; i < visible.Count; i++)
		{
			s.Text(visible[i], x, y + i * 22, Style.Text, Style.Small);
		}
	}

	// Shop, inn, save prompt, name entry

	private void UpdateShop(InputState input)
	{
		if (input.Pressed(Keys.Left) || input.Pressed(Keys.Right))
		{
			_tab = 1 - _tab;
			_cursor = 0;
		}
		if (input.Back)
		{
			Report(_s.CloseMenu());
			return;
		}
		ShopInfo? shop = _s.Shop();
		if (shop is null)
		{
			return;
		}
		var list = _tab == 0 ? shop.Stock.ToList() : _s.Inventory().Where(i => i.Price >= 0).ToList();
		Move(input, list.Count);
		if (input.Confirm && list.Count > 0)
		{
			Report(_tab == 0 ? _s.Buy(list[_cursor].Id) : _s.Sell(list[_cursor].Id));
		}
	}

	private void DrawShop(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		int x = 80;
		int w = s.Width - 160;
		Style.Window(s, x, 50, w, s.Height - 100, "Shop");
		ShopInfo? shop = _s.Shop();
		if (shop is null)
		{
			return;
		}
		Style.Tabs(s, x + 24, 76, ["Buy", "Sell"], _tab);
		string money = $"Money {_s.WorldInfo().Money}";
		s.Text(money, x + w - 40 - s.Measure(money), 81, Style.Text);
		s.Image(art.Stall, x + w - 120, 120, 80, 80, Color.White);
		int priceX = x + 560;
		int countX = x + 680;
		s.Text(_tab == 0 ? "Price" : "Each", priceX, 128, Style.Dim, Style.Small);
		s.Text("Have", countX, 128, Style.Dim, Style.Small);
		IReadOnlyList<(string Name, int Price, int Count)> rows = _tab == 0
			? shop.Stock.Select(i => (i.Name, i.Price, i.Count)).ToList()
			: _s.Inventory().Where(i => i.Price >= 0).Select(i => (i.Name, i.Price * shop.SellPercent / 100, i.Count)).ToList();
		for (int i = 0; i < rows.Count; i++)
		{
			int y = 156 + i * 30;
			bool on = i == _cursor;
			Style.ListRow(s, rows[i].Name, x + 40, y, countX + 60 - x - 40, on);
			string price = rows[i].Price.ToString(System.Globalization.CultureInfo.InvariantCulture);
			s.Text(price, priceX + 40 - s.Measure(price), y, on ? Style.Accent : Style.Text);
			s.Text($"x{rows[i].Count}", countX, y, Style.Dim);
		}
		if (rows.Count == 0)
		{
			s.Text(_tab == 0 ? "Nothing for sale." : "Nothing to sell.", x + 54, 156, Style.Dim);
		}
		DrawToasts(s, x + 24, s.Height - 92);
		s.Text("Left/Right buy or sell   Enter one   Esc leave", x + 24, s.Height - 86, Style.Dim, Style.Small);
	}

	private void UpdateInn(InputState input)
	{
		Move(input, 2);
		if (input.Back || (input.Confirm && _cursor == 1))
		{
			Report(_s.CloseMenu());
		}
		else if (input.Confirm)
		{
			Report(_s.Rest());
		}
	}

	private void DrawInn(IDrawSurface s, Art art)
	{
		bool field = DrawField(s, art);
		int y = DialogueTop(s, field);
		Style.Box(s, 24, y, s.Width - 48, DialogueHeight);
		Style.Plate(s, "Inn", 42, y - 16, Style.Accent, Style.Body);
		s.Image(art.Bed, 48, y + 34, 96, 96, Color.White);
		InnInfo? inn = _s.Inn();
		s.Text($"A night's rest costs {inn?.Cost ?? 0}. You have {_s.WorldInfo().Money}.", 170, y + 30, Style.Text);
		DrawList(s, 186, y + 74, ["Rest", "Leave"], width: 300);
	}

	private void UpdateSavePrompt(InputState input)
	{
		int slots = _s.GameInfo().ManualSlots;
		Move(input, slots + 1);
		if (input.Back || (input.Confirm && _cursor == slots))
		{
			Report(_s.CloseMenu());
		}
		else if (input.Confirm)
		{
			PsivResult saved = _s.SaveManual($"manual_{_cursor}");
			Report(saved);
			if (saved.Ok)
			{
				Report(_s.CloseMenu());
			}
		}
	}

	private void DrawSavePrompt(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		int slots = _s.GameInfo().ManualSlots;
		int w = 560;
		int h = 110 + (slots + 1) * 30;
		int x = (s.Width - w) / 2;
		int y = (s.Height - h) / 2;
		Style.Window(s, x, y, w, h, "Save");
		s.Text("Save your progress?", x + 28, y + 28, Style.Accent);
		DrawList(s, x + 40, y + 72, Enumerable.Range(0, slots).Select(i => $"manual_{i}").Append("Don't save").ToList(), width: w - 80);
	}

	private void UpdateNameEntry(InputState input)
	{
		foreach (Keys k in input.PressedKeys)
		{
			if (k is >= Keys.A and <= Keys.Z && _nameBuffer.Length < 16)
			{
				char c = (char)('a' + (k - Keys.A));
				_nameBuffer += _nameBuffer.Length == 0 || _nameBuffer[^1] == ' ' ? char.ToUpperInvariant(c) : c;
			}
			else if (k == Keys.Space && _nameBuffer.Length < 16)
			{
				_nameBuffer += ' ';
			}
		}
		if (input.Pressed(Keys.Back) && _nameBuffer.Length > 0)
		{
			_nameBuffer = _nameBuffer[..^1];
		}
		if (input.Pressed(Keys.Enter))
		{
			Report(_s.EnterName(_nameBuffer.Length == 0 ? _s.DisplayName(_s.NameEntryMember) : _nameBuffer));
		}
	}

	private void DrawNameEntry(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		int w = 760;
		int x = (s.Width - w) / 2;
		int y = 180;
		Style.Window(s, x, y, w, 220, "Name");
		string member = _s.NameEntryMember;
		s.Image(art.ActorFrame(Art.LookFor(member), Direction.South, 0), x + 28, y + 40, 96, 96, Color.White);
		s.Text($"A name for {_s.DisplayName(member)}:", x + 150, y + 36, Style.Accent);
		s.Fill(x + 150, y + 80, 420, 50, Style.Track);
		Style.Frame(s, x + 150, y + 80, 420, 50, Style.PanelEdge);
		string cursor = _clockMs / 400 % 2 == 0 ? "_" : " ";
		s.Text(_nameBuffer + cursor, x + 166, y + 88, Style.Text, Style.Title);
		s.Text("Type letters   Backspace erase   Enter confirm (empty keeps the name)", x + 28, y + 176, Style.Dim, Style.Small);
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
			DrawHint(s, "Arrows walk   Enter talk   M menu   Q quests   T party talk   L log");
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
