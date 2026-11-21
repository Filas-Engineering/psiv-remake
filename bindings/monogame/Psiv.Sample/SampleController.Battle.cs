using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	// The playback interval at 1x battle speed; the profile's speed divides it.
	private const int PlaybackLineMsAt1x = 350;

	private int PlaybackLineMs() => _s.Profile().BattleSpeed switch
	{
		BattleSpeed.X1_5 => PlaybackLineMsAt1x * 2 / 3,
		BattleSpeed.X2 => PlaybackLineMsAt1x / 2,
		BattleSpeed.X3 => PlaybackLineMsAt1x / 3,
		_ => PlaybackLineMsAt1x,
	};

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
		public readonly Queue<(string Text, string? Cue)> Queue = new();
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
		_battle.Queue.Enqueue(($"Enemies: {enemies}.", "battle_start"));
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
		int partyCount = _s.BattleInfo()?.PartyCount ?? 0;
		foreach (BattleEvent e in _s.ResolveRound())
		{
			if (BattleText.Describe(_s, _battle.Labels, e) is { } line)
			{
				_battle.Queue.Enqueue((line, BattleCue(e, partyCount)));
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

	private void UpdateBattle(int dtMs, ActionInput input)
	{
		BattleUi b = _battle;
		if (b.Phase is BattlePhase.Command or BattlePhase.Option or BattlePhase.Target or BattlePhase.Macro && input.Pressed(GameAction.Pause))
		{
			OpenPause();
			return;
		}
		int lineMs = PlaybackLineMs();
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
			while (b.Queue.Count > 0 && (hurry || b.PlaybackMs >= lineMs))
			{
				b.PlaybackMs = 0;
				(string line, string? cue) = b.Queue.Dequeue();
				b.Log.Add(line);
				_log("battle: " + line);
				if (cue is not null)
				{
					_audio.PlayCue(cue);
				}
			}
			if (b.Queue.Count == 0 && b.PlaybackMs >= lineMs)
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
			Widgets.ValueBar(s, cx + 12, cy + 86, bw, "HP", c.Hp, c.MaxHp, c.Hp * 4 <= c.MaxHp ? Style.Bad : Style.Good);
			Widgets.ValueBar(s, cx + 12, cy + 130, bw, "TP", c.Tp, c.MaxTp, Style.Tp);
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
				s.Text($"{Hint(GameAction.Back)} stops after this round", mx + 24, BattlePanelTop + 64, Style.Dim, Style.Small);
			}
			s.Text($"{Hint(GameAction.Confirm)} hurries", mx + 24, BattlePanelTop + panelH - 34, Style.Dim, Style.Small);
			break;
		case BattlePhase.Ended:
			s.Text(OutcomeText(_s.BattleInfo()?.Outcome ?? Outcome.Ongoing), mx + 24, BattlePanelTop + 34, Style.Accent, Style.Title);
			s.Text($"{Hint(GameAction.Confirm)}: continue", mx + 24, BattlePanelTop + 84, Style.Text);
			break;
		}
	}
}
