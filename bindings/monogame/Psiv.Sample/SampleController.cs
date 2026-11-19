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

// The sample's screens: reads the session after each update and draws it with flat rectangles and text.
// Holds no game state of its own beyond cursors and the battle playback queue.
public sealed class SampleController
{
	private const int PlaybackLineMs = 350;
	private const int TickerMs = 4000;
	// Script actions play for their own length, held between these so the sample stays brisk.
	private const int ShortestAnimationMs = 150;
	private const int LongestAnimationMs = 600;

	// A presentation action being played: the session waits on it until FinishAction.
	private sealed class Animation(PresentationAction action, int lengthMs)
	{
		public PresentationAction Action { get; } = action;
		public int LengthMs { get; } = lengthMs;
		public int ElapsedMs { get; set; }
		public float Progress => LengthMs == 0 ? 1f : Math.Clamp(ElapsedMs / (float)LengthMs, 0f, 1f);
	}

	private readonly List<Animation> _animations = [];
	// The screen's darkness after the last fade, 0 to 1.
	private float _darkness;

	private readonly GameSession _s;
	private readonly Action<string> _log;
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
			if (a.Instant)
			{
				EndAnimation(a);
				continue;
			}
			int length = a.Type is ActionType.Move or ActionType.FadeIn or ActionType.FadeOut or ActionType.CameraMove
				? Math.Clamp((int)(a.Seconds * 1000), ShortestAnimationMs, LongestAnimationMs)
				: ShortestAnimationMs;
			_animations.Add(new Animation(a, length));
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
			default:
				// Anything else, a move included, is a marker sliding across the top of the screen with its subject.
				int x = 20 + (int)((s.Width - 200) * a.Progress);
				s.Fill(x, 44, 160, 22, Style.Accent);
				s.Text(a.Action.Subject.Length > 0 ? a.Action.Subject : a.Action.Description, x + 6, 44, Style.Background, Style.Small);
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

	private void DrawTitle(IDrawSurface s)
	{
		s.Text(_s.GameInfo().Title, 80, 80, Style.Accent, Style.Title);
		s.Text("A placeholder adventure played through libpsiv_session", 80, 124, Style.Dim, Style.Small);
		if (_s.WorldInfo().GameOver)
		{
			s.Text("The party has fallen.", 80, 160, Style.Bad);
		}
		var options = TitleOptions();
		for (int i = 0; i < options.Count; i++)
		{
			s.Text((i == _cursor ? "> " : "  ") + options[i].Label, 100, 220 + i * Style.LineHeight, i == _cursor ? Style.Accent : Style.Text);
		}
		s.Text("Up/Down choose   Enter confirm", 80, s.Height - 60, Style.Dim, Style.Small);
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

	private void DrawField(IDrawSurface s, int bottomReserved)
	{
		FieldView? view = _s.Field();
		if (view is null)
		{
			return;
		}
		FieldInfo f = view.Info;
		int cell = Math.Clamp(Math.Min((s.Width - 40) / Math.Max(1, f.Width), (s.Height - 80 - bottomReserved) / Math.Max(1, f.Height)), 8, 48);
		int ox = (s.Width - cell * f.Width) / 2;
		int oy = 50;
		for (int y = 0; y < f.Height; y++)
		{
			for (int x = 0; x < f.Width; x++)
			{
				CellFlags c = view.Cell(x, y) & CellFlags.TerrainMask;
				s.Fill(ox + x * cell, oy + y * cell, cell - 1, cell - 1, c == CellFlags.Wall ? Style.Wall : c == CellFlags.Water ? Style.Water : Style.Floor);
			}
		}
		foreach (FieldExit e in view.Exits)
		{
			s.Fill(ox + e.X * cell, oy + e.Y * cell, cell - 1, cell - 1, Style.Exit);
		}
		foreach (FieldPoint p in view.Points)
		{
			Color color = p.Kind switch
			{
				PointKind.Talk => Style.Talk,
				PointKind.Step => Style.Step,
				PointKind.Chest => p.Active ? Style.Chest : Style.ChestOpen,
				PointKind.Shop => Style.Shop,
				_ => Style.Rest,
			};
			int inset = p.Kind == PointKind.Step ? cell / 3 : cell / 8;
			if (p.Kind != PointKind.Step || p.Active)
			{
				s.Fill(ox + p.X * cell + inset, oy + p.Y * cell + inset, cell - 1 - 2 * inset, cell - 1 - 2 * inset, color);
			}
		}
		foreach (FieldEnemy e in view.Enemies)
		{
			s.Fill(ox + e.X * cell + cell / 6, oy + e.Y * cell + cell / 6, cell * 2 / 3, cell * 2 / 3, e.State == EnemyState.Chasing ? Style.EnemyChasing : Style.Enemy);
		}
		int lx = ox + f.LeaderX * cell;
		int ly = oy + f.LeaderY * cell;
		s.Fill(lx + cell / 6, ly + cell / 6, cell * 2 / 3, cell * 2 / 3, Style.Leader);
		(int dx, int dy) = f.Facing switch { Direction.North => (0, -1), Direction.East => (1, 0), Direction.South => (0, 1), _ => (-1, 0) };
		s.Fill(lx + cell / 2 - 3 + dx * cell / 3, ly + cell / 2 - 3 + dy * cell / 3, 6, 6, Style.Bad);

		WorldInfo w = _s.WorldInfo();
		long seconds = w.PlayTimeMs / 1000;
		s.Text($"{f.AreaName}    Money {w.Money}    {seconds / 3600}:{seconds / 60 % 60:00}:{seconds % 60:00}", 20, 14, Style.Text);
	}

	private void DrawTicker(IDrawSurface s, int y)
	{
		for (int i = 0; i < _ticker.Count; i++)
		{
			s.Text(_ticker[i].Text, 24, y + i * 22, Style.Text, Style.Small);
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

	private void DrawDialogue(IDrawSurface s)
	{
		int boxH = 200;
		DrawField(s, boxH);
		int y = s.Height - boxH - 10;
		Style.Box(s, 20, y, s.Width - 40, boxH);
		if (_s.Line() is { } line)
		{
			if (line.SpeakerName.Length > 0)
			{
				s.Text(line.SpeakerName, 40, y + 14, Style.Accent);
			}
			int ty = y + 46;
			foreach (string l in Style.Wrap(s, line.Text, s.Width - 100))
			{
				s.Text(l, 40, ty, Style.Text);
				ty += Style.LineHeight;
			}
			s.Text("Enter: next" + (_s.CanSkip ? "   Tab: skip" : ""), s.Width - 260, y + boxH - 30, Style.Dim, Style.Small);
		}
		else if (_s.Choice() is { } choice)
		{
			s.Text(choice.Prompt.Length == 0 ? "Choose:" : choice.Prompt, 40, y + 14, Style.Accent);
			for (int i = 0; i < choice.Options.Count; i++)
			{
				s.Text((i == _cursor ? "> " : "  ") + choice.Options[i], 60, y + 50 + i * Style.LineHeight, i == _cursor ? Style.Accent : Style.Text);
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

	private void DrawBattle(IDrawSurface s)
	{
		BattleUi b = _battle;
		IReadOnlyList<Combatant> all = _s.Combatants();
		int half = s.Width / 2;
		Style.Box(s, 20, 20, half - 30, 300);
		Style.Box(s, half + 10, 20, half - 30, 300);
		s.Text("Party", 36, 30, Style.Accent);
		s.Text("Enemies", half + 26, 30, Style.Accent);
		int py = 64;
		int ey = 64;
		foreach (Combatant c in all)
		{
			bool party = c.Side == Side.Party;
			int x = party ? 36 : half + 26;
			int y = party ? py : ey;
			bool acting = b.Phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro && c.Index == b.Actor;
			bool targeted = b.Phase == BattlePhase.Target && b.Targets.Count > 0 && b.Targets[Math.Clamp(_cursor, 0, b.Targets.Count - 1)] == c.Index;
			Color color = !c.Alive ? Style.Dim : targeted ? Style.Accent : acting ? Style.Good : Style.Text;
			string label = (targeted ? "> " : "") + (party ? c.Name : c.Label);
			s.Text(label, x, y, color);
			string statuses = string.Join(" ", c.Statuses.Where(st => st.Id != "defeated").Select(st => st.Name));
			s.Text($"HP {c.Hp}/{c.MaxHp}" + (party ? $"  TP {c.Tp}/{c.MaxTp}" : "") + (statuses.Length > 0 ? "  " + statuses : ""), x + 200, y + 2, Style.Dim, Style.Small);
			Style.Bar(s, x, y + 24, 180, 5, c.Hp, c.MaxHp, Style.Good);
			if (party)
			{
				py += 46;
			}
			else
			{
				ey += 46;
			}
		}

		int logTop = 336;
		int logHeight = s.Height - logTop - 20;
		int menuWidth = 300;
		Style.Box(s, 20, logTop, s.Width - menuWidth - 50, logHeight);
		int lines = Math.Max(1, (logHeight - 20) / 22);
		var visible = b.Log.Skip(Math.Max(0, b.Log.Count - lines)).ToList();
		for (int i = 0; i < visible.Count; i++)
		{
			s.Text(visible[i], 36, logTop + 10 + i * 22, Style.Text, Style.Small);
		}

		int mx = s.Width - menuWidth - 20;
		Style.Box(s, mx, logTop, menuWidth, logHeight);
		void Menu(string title, IReadOnlyList<string> items, IReadOnlyList<bool>? usable = null)
		{
			s.Text(title, mx + 16, logTop + 10, Style.Accent);
			for (int i = 0; i < items.Count; i++)
			{
				bool ok = usable is null || usable[i];
				s.Text((i == _cursor ? "> " : "  ") + items[i], mx + 16, logTop + 44 + i * 24, !ok ? Style.Dim : i == _cursor ? Style.Accent : Style.Text, Style.Small);
			}
		}
		string actor = b.Actor < all.Count ? all[b.Actor].Name : "";
		switch (b.Phase)
		{
		case BattlePhase.Command:
			Menu(actor, CommandMenu);
			break;
		case BattlePhase.Option:
			Menu(b.MenuChoice, b.Options.Select(o => o.Name + (o.Cost > 0 ? $" ({o.Cost} TP)" : "") + (o.Uses >= 0 ? $" x{o.Uses}" : "")).ToList(), b.Options.Select(o => o.Usable).ToList());
			break;
		case BattlePhase.Target:
			Menu("Target", b.Targets.Select(t => t < all.Count ? (all[t].Side == Side.Party ? all[t].Name : all[t].Label) : "?").ToList());
			break;
		case BattlePhase.Macro:
			Menu("Macro", _s.Macros());
			break;
		case BattlePhase.Playback:
			s.Text(b.Auto ? "Auto-battle (Esc stops)" : "...", mx + 16, logTop + 10, Style.Dim, Style.Small);
			break;
		case BattlePhase.Ended:
			s.Text("Enter: continue", mx + 16, logTop + 10, Style.Accent);
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

	private void DrawList(IDrawSurface s, int x, int y, IReadOnlyList<string> items, bool cursor = true)
	{
		for (int i = 0; i < items.Count; i++)
		{
			bool on = cursor && i == _cursor;
			s.Text((on ? "> " : "  ") + items[i], x, y + i * Style.LineHeight, on ? Style.Accent : Style.Text);
		}
	}

	private void DrawMenu(IDrawSurface s)
	{
		Style.Box(s, 20, 20, s.Width - 40, s.Height - 40);
		int tx = 40;
		for (int i = 0; i < MenuTabs.Length; i++)
		{
			s.Text(MenuTabs[i], tx, 36, i == _tab ? Style.Accent : Style.Dim);
			tx += s.Measure(MenuTabs[i]) + 30;
		}
		s.Text($"Money {_s.WorldInfo().Money}", s.Width - 220, 36, Style.Text);
		List<Member> lineup = _s.Party().Where(m => m.InLineup).ToList();
		int y = 90;
		switch (MenuTabs[_tab])
		{
		case "Party":
			if (_pickMember is null || _pickAction is not null)
			{
				DrawList(s, 40, y, lineup.Select(m => $"{m.Name}  Lv{m.Level}  HP {m.Hp}/{m.MaxHp}  TP {m.Tp}/{m.MaxTp}  EXP {m.Experience}" +
					string.Concat(m.Statuses.Select(st => $" [{st.Name}]"))).ToList());
				if (_pickAction is not null)
				{
					s.Text($"Use {_s.DisplayName(_pickAction)} on whom?", 40, s.Height - 80, Style.Accent);
				}
			}
			else
			{
				Member m = lineup.First(x => x.Id == _pickMember);
				s.Text($"{m.Name}: STR {m.Strength}  MEN {m.Mental}  AGI {m.Agility}  DEX {m.Dexterity}  DEF {m.Defense}", 40, y, Style.Text);
				s.Text("Equipment: " + string.Join(", ", m.EquipmentNames.Where(n => n.Length > 0)), 40, y + 30, Style.Dim, Style.Small);
				var techniques = _s.MemberAbilities(_pickMember).Where(a => a.Field).ToList();
				s.Text(techniques.Count == 0 ? "No techniques to use here." : "Use a technique:", 40, y + 70, Style.Accent);
				DrawList(s, 40, y + 100, techniques.Select(a => $"{a.Name} ({a.Cost} TP)").ToList());
			}
			break;
		case "Items":
			var items = _s.Inventory().Where(i => !i.IsEquipment).ToList();
			DrawList(s, 40, y, items.Select(i => $"{i.Name} x{i.Count}" + (i.Field ? "" : "  (battle only)")).ToList(), _pickAction is null);
			if (_pickAction is not null)
			{
				DrawList(s, 600, y, lineup.Select(m => $"{m.Name}  HP {m.Hp}/{m.MaxHp}").ToList());
			}
			break;
		case "Equip":
			if (_pickMember is null)
			{
				DrawList(s, 40, y, lineup.Select(m => m.Name + ": " + string.Join(", ", m.EquipmentNames.Where(n => n.Length > 0))).ToList());
			}
			else
			{
				DrawList(s, 40, y, EquipChoices(_pickMember).Select(c => c.Label).ToList());
			}
			break;
		case "Save":
			var saves = _s.Saves() ?? [];
			int slots = _s.GameInfo().ManualSlots;
			DrawList(s, 40, y, Enumerable.Range(0, slots).Select(i =>
			{
				SaveSummary? existing = saves.FirstOrDefault(x => x.Slot == $"manual_{i}");
				return $"manual_{i}  " + (existing is null ? "(empty)" : existing.Damaged ? "(damaged)" : $"{existing.Area} {string.Join(" ", existing.Party)}");
			}).ToList());
			break;
		case "Quests":
			DrawQuestLog(s, 40, y);
			break;
		case "Codex":
			int cy = y;
			foreach (CodexEntry entry in _s.Encyclopedia())
			{
				s.Text(entry.Title, 40, cy, Style.Accent);
				cy += Style.LineHeight;
				foreach (string l in Style.Wrap(s, entry.Text, s.Width - 120, Style.Small))
				{
					s.Text(l, 60, cy, Style.Text, Style.Small);
					cy += 22;
				}
				cy += 10;
			}
			break;
		case "Log":
			DrawTextLog(s, 40, y, s.Height - y - 60);
			break;
		}
		s.Text("Left/Right tab   Up/Down choose   Enter use   Esc back", 40, s.Height - 50, Style.Dim, Style.Small);
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

	private void DrawShop(IDrawSurface s)
	{
		Style.Box(s, 20, 20, s.Width - 40, s.Height - 40);
		ShopInfo? shop = _s.Shop();
		if (shop is null)
		{
			return;
		}
		s.Text("Buy", 40, 36, _tab == 0 ? Style.Accent : Style.Dim);
		s.Text("Sell", 120, 36, _tab == 1 ? Style.Accent : Style.Dim);
		s.Text($"Money {_s.WorldInfo().Money}", s.Width - 220, 36, Style.Text);
		if (_tab == 0)
		{
			DrawList(s, 40, 90, shop.Stock.Select(i => $"{i.Name}  {i.Price}  (have {i.Count})").ToList());
		}
		else
		{
			DrawList(s, 40, 90, _s.Inventory().Where(i => i.Price >= 0).Select(i => $"{i.Name} x{i.Count}  {i.Price * shop.SellPercent / 100} each").ToList());
		}
		DrawTicker(s, s.Height - 200);
		s.Text("Left/Right buy or sell   Enter one   Esc leave", 40, s.Height - 50, Style.Dim, Style.Small);
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

	private void DrawInn(IDrawSurface s)
	{
		DrawField(s, 200);
		Style.Box(s, 20, s.Height - 210, s.Width - 40, 200);
		InnInfo? inn = _s.Inn();
		s.Text($"A night's rest costs {inn?.Cost ?? 0}. You have {_s.WorldInfo().Money}.", 40, s.Height - 196, Style.Text);
		DrawList(s, 60, s.Height - 150, ["Rest", "Leave"]);
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

	private void DrawSavePrompt(IDrawSurface s)
	{
		Style.Box(s, 20, 20, s.Width - 40, s.Height - 40);
		s.Text("Save your progress?", 40, 36, Style.Accent);
		int slots = _s.GameInfo().ManualSlots;
		DrawList(s, 40, 90, Enumerable.Range(0, slots).Select(i => $"manual_{i}").Append("Don't save").ToList());
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

	private void DrawNameEntry(IDrawSurface s)
	{
		Style.Box(s, 20, 20, s.Width - 40, 200);
		s.Text($"A name for {_s.DisplayName(_s.NameEntryMember)}:", 40, 40, Style.Accent);
		s.Text(_nameBuffer + "_", 60, 90, Style.Text, Style.Title);
		s.Text("Type letters   Backspace erase   Enter confirm (empty keeps the name)", 40, 180, Style.Dim, Style.Small);
	}

	public void Draw(IDrawSurface s)
	{
		s.Fill(0, 0, s.Width, s.Height, Style.Background);
		switch (Screen)
		{
		case Screen.Title: DrawTitle(s); break;
		case Screen.Field:
			DrawField(s, 140);
			DrawTicker(s, s.Height - 150);
			s.Text("Arrows walk   Enter talk   M menu   Q quests   T party talk   L log", 20, s.Height - 30, Style.Dim, Style.Small);
			break;
		case Screen.Dialogue: DrawDialogue(s); break;
		case Screen.Battle: DrawBattle(s); break;
		case Screen.Menu:
			DrawMenu(s);
			DrawTicker(s, s.Height - 200);
			break;
		case Screen.Shop: DrawShop(s); break;
		case Screen.Inn: DrawInn(s); break;
		case Screen.SavePrompt: DrawSavePrompt(s); break;
		case Screen.NameEntry: DrawNameEntry(s); break;
		case Screen.QuestLog:
			Style.Box(s, 20, 20, s.Width - 40, s.Height - 40);
			s.Text("Quest log", 40, 36, Style.Accent, Style.Title);
			DrawQuestLog(s, 40, 90);
			break;
		case Screen.TextLog:
			Style.Box(s, 20, 20, s.Width - 40, s.Height - 40);
			s.Text("Text log", 40, 36, Style.Accent, Style.Title);
			DrawTextLog(s, 40, 90, s.Height - 150);
			break;
		}
		DrawAnimations(s);
	}
}
