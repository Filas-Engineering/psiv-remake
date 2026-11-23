using System.Globalization;
using System.Text;

namespace Psiv.Session.Scripting;

// game_cli's command language (game/src/runner.cpp) over GameSession, printing the same transcript, so a
// scripted run through C# can be compared with the C++ one byte for byte.
public sealed class ScriptRunner(GameSession session, TextWriter output, bool autoBattle)
{
	private readonly List<string> _battleNames = [];

	public int Errors { get; private set; }

	public string Prompt => GameSession.NameOf(NameKind.Mode, (int)session.Mode) + "> ";

	private void Print(string text) => output.Write(text + "\n");

	private void Error(string text)
	{
		Errors++;
		Print("! " + text);
	}

	private void Report(PsivResult result)
	{
		if (result.Error is { } error)
		{
			Error(error.Text);
		}
	}

	private static bool ToInt(string text, out int value) =>
		int.TryParse(text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out value);

	private static string Clock(long ms)
	{
		long s = ms / 1000;
		return string.Create(CultureInfo.InvariantCulture, $"{s / 3600}:{s / 60 % 60:00}:{s % 60:00}");
	}

	private static bool ParseFacing(string text, out Direction direction)
	{
		for (var d = Direction.North; d <= Direction.West; d++)
		{
			string name = GameSession.NameOf(NameKind.Direction, (int)d);
			if (text == name || (text.Length == 1 && text[0] == name[0]))
			{
				direction = d;
				return true;
			}
		}
		direction = Direction.None;
		return false;
	}

	public void FlushOutput() => PrintNotices(session.DrainNotices());

	private void PrintNotices(IReadOnlyList<Notice> notices)
	{
		foreach (Notice n in notices)
		{
			Print(n.Kind switch
			{
				NoticeKind.Action => "  (" + n.Text + ")",
				NoticeKind.Warning => "! " + n.Text,
				NoticeKind.Autosave => "[" + n.Text + "]",
				NoticeKind.Ambient => "~ " + n.Text,
				_ => n.Text,
			});
		}
	}

	public void PrintEvents(IReadOnlyList<BattleEvent> events)
	{
		foreach (BattleEvent e in events)
		{
			if (BattleText.Describe(session, _battleNames, e) is { } line)
			{
				Print(line);
			}
		}
	}

	private void FinishBattleAuto()
	{
		AutoBattle played = session.PlayBattle();
		if (played.Refusal is { } refusal)
		{
			Error(refusal.Text);
		}
		foreach (AutoRound round in played.Rounds)
		{
			PrintNotices(round.Notices);
			PrintEvents(round.Events);
		}
		if (!played.Ended)
		{
			Error("the battle did not end in 60 rounds");
		}
		FlushOutput();
	}

	public void Settle()
	{
		for (int guard = 0; guard < 100000; guard++)
		{
			FlushOutput();
			Mode mode = session.Mode;
			if (mode == Mode.Battle)
			{
				if (_battleNames.Count == 0)
				{
					var enemies = new List<string>();
					foreach (Combatant c in session.Combatants())
					{
						_battleNames.Add(c.Label);
						if (c.Side == Side.Enemy)
						{
							enemies.Add(c.Label);
						}
					}
					Print("Enemies: " + string.Join(", ", enemies) + ".");
				}
				if (!autoBattle)
				{
					return;
				}
				FinishBattleAuto();
				continue;
			}
			_battleNames.Clear();
			if (mode != Mode.Event)
			{
				return;
			}
			if (session.Line() is { } line)
			{
				PrintKept();
				Print(line.Display);
				session.ConfirmLine();
				continue;
			}
			if (session.Choice() is { } choice)
			{
				PrintKept();
				var text = new StringBuilder(choice.Prompt.Length == 0 ? "Choose:" : choice.Prompt);
				if (choice.Header.Length > 0)
				{
					text.Append(" | ").Append(choice.Header);
				}
				for (int i = 0; i < choice.Options.Count; i++)
				{
					text.Append(CultureInfo.InvariantCulture, $" [{i}] {choice.Options[i]}");
					if (choice.States.Count > i && choice.States[i].Length > 0)
					{
						text.Append(" (").Append(choice.States[i]).Append(')');
					}
				}
				if (choice.Keep)
				{
					text.Append(" | keep");
				}
				Print(text.ToString());
				return;
			}
			session.Update(100);
		}
		Error("the game did not settle");
	}

	// A kept choice under the next line or choice, printed once each time it changes.
	private string _lastKept = "";

	private void PrintKept()
	{
		Choice? kept = session.KeptChoice();
		string text = kept is null ? "" : (kept.Prompt.Length == 0 ? "Choose:" : kept.Prompt) + " > " + kept.Options[kept.Picked];
		if (text.Length > 0 && text != _lastKept)
		{
			Print("  (kept: " + text + ")");
		}
		_lastKept = text;
	}

	private void Status()
	{
		FieldInfo? field = session.FieldInfo();
		WorldInfo world = session.WorldInfo();
		Print($"Mode {GameSession.NameOf(NameKind.Mode, (int)world.Mode)}, {field?.AreaName ?? "-"} ({field?.LeaderX ?? 0},{field?.LeaderY ?? 0}) facing " +
			$"{GameSession.NameOf(NameKind.Direction, (int)(field?.Facing ?? Direction.South))}, play time {Clock(world.PlayTimeMs)}.");
		foreach (Member m in session.Party().Where(m => m.InLineup))
		{
			var line = new StringBuilder($"  {m.Name} Lv{m.Level} HP {m.Hp}/{m.MaxHp} TP {m.Tp}/{m.MaxTp} EXP {m.Experience}");
			foreach (ActiveStatus s in m.Statuses)
			{
				line.Append(" [" + s.Name + (s.Remaining > 0 ? " " + s.Remaining : "") + "]");
			}
			Print(line.ToString());
		}
		string items = string.Join(", ", session.Inventory().Select(i => $"{i.Id} x{i.Count}"));
		Print($"  Money {world.Money}. Items: {(items.Length == 0 ? "none" : items)}.");
		if (field is not null && field.Aboard.Length > 0 && session.FieldVehicles().FirstOrDefault(v => v.Aboard) is { } vehicle)
		{
			Print($"  Aboard the {vehicle.Name}.");
		}
	}

	private void Look()
	{
		FieldView? view = session.Field();
		if (view is null)
		{
			Error("there is no area");
			return;
		}
		char[][] rows = session.FieldRows().Select(r => r.ToCharArray()).ToArray();
		void Put(int x, int y, char c)
		{
			if (y >= 0 && y < rows.Length && x >= 0 && x < rows[y].Length)
			{
				rows[y][x] = c;
			}
		}
		foreach (FieldExit e in view.Exits)
		{
			Put(e.X, e.Y, '>');
		}
		foreach (FieldPoint p in view.Points)
		{
			switch (p.Kind)
			{
			case PointKind.Talk: Put(p.X, p.Y, 'N'); break;
			case PointKind.Chest: Put(p.X, p.Y, p.Active ? 'C' : 'c'); break;
			case PointKind.Shop: Put(p.X, p.Y, '$'); break;
			case PointKind.Rest: Put(p.X, p.Y, 'R'); break;
			case PointKind.Port: Put(p.X, p.Y, 'P'); break;
			}
		}
		foreach (FieldVehicle v in view.Vehicles)
		{
			Put(v.X, v.Y, 'V');
		}
		foreach (FieldEnemy e in view.Enemies)
		{
			Put(e.X, e.Y, e.State == EnemyState.Chasing ? '!' : 'E');
		}
		Put(view.Info.LeaderX, view.Info.LeaderY, '@');
		foreach (char[] r in rows)
		{
			Print("  " + new string(r));
		}
	}

	private void WalkTo(int goalX, int goalY)
	{
		if (!session.WalkTo(goalX, goalY).Ok)
		{
			return;
		}
		WalkStatus status;
		while ((status = session.WalkStep()) == WalkStatus.Walking)
		{
			Settle();
		}
		if (status == WalkStatus.NoPath)
		{
			Error($"no path to ({goalX},{goalY})");
		}
		else if (status == WalkStatus.TooLong)
		{
			Error("walking took too long");
		}
	}

	private bool ActorIndex(string text, out int index)
	{
		if (ToInt(text, out index))
		{
			return true;
		}
		index = session.Party().Where(m => m.InLineup).Select(m => m.Id).ToList().IndexOf(text);
		return index >= 0;
	}

	private bool TargetIndex(string text, out int index)
	{
		if (text.Length > 1 && (text[0] == 'e' || text[0] == 'p') && ToInt(text[1..], out index))
		{
			if (text[0] == 'e')
			{
				index += session.BattleInfo()?.PartyCount ?? 0;
			}
			return true;
		}
		return ToInt(text, out index);
	}

	// Runs one command line. Returns false after `quit`.
	public bool Execute(string rawLine)
	{
		string line = rawLine;
		int hash = line.IndexOf('#');
		if (hash >= 0)
		{
			line = line[..hash];
		}
		string[] w = line.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
		if (w.Length == 0)
		{
			return true;
		}
		Print(Prompt + line.TrimStart(' ', '\t'));
		string cmd = w[0];
		Mode mode = session.Mode;
		int count = 1;

		if (cmd == "quit")
		{
			return false;
		}
		else if (cmd == "new")
		{
			Report(session.NewGame());
		}
		else if (cmd == "continue")
		{
			Report(session.ContinueGame());
		}
		else if (cmd == "load" && w.Length == 2)
		{
			Report(session.Load(w[1]));
		}
		else if (cmd == "retry")
		{
			Report(session.RetryBattle());
		}
		else if (cmd == "title")
		{
			session.QuitToTitle();
		}
		else if (ParseFacing(cmd, out Direction facing) && (w.Length == 1 || (w.Length == 2 && ToInt(w[1], out count))))
		{
			for (int i = 0; i < count; i++)
			{
				PsivResult problem = session.Walk(facing);
				if (!problem.Ok)
				{
					Report(problem);
					break;
				}
				Settle();
				if (session.Mode != Mode.Field)
				{
					break;
				}
			}
		}
		else if (cmd == "face" && w.Length == 2 && ParseFacing(w[1], out Direction turn))
		{
			Report(session.Face(turn));
		}
		else if (cmd == "walk" && w.Length is 2 or 3)
		{
			if (w.Length == 3 && ToInt(w[1], out int x) && ToInt(w[2], out int y))
			{
				WalkTo(x, y);
			}
			else if (w.Length == 2 && session.Field()?.Markers.FirstOrDefault(m => m.Name == w[1]) is { } marker)
			{
				WalkTo(marker.X, marker.Y);
			}
			else
			{
				Error("walk takes a marker of this area or x y");
				return true;
			}
		}
		else if (cmd is "talk" or "x" or "interact" && w.Length == 1)
		{
			Report(session.Interact());
		}
		else if (cmd == "board" && w.Length == 1)
		{
			Report(session.Board());
		}
		else if (cmd == "disembark" && w.Length == 1)
		{
			Report(session.LeaveVehicle());
		}
		else if (cmd == "destinations" && w.Length == 2)
		{
			if (GameSession.ParseName(NameKind.TravelKind, w[1]) is not { } kind)
			{
				Error("destinations port|town|dungeon_exit");
			}
			else
			{
				foreach (TravelDestination d in session.TravelDestinations((TravelKind)kind))
				{
					Print($"  {d.Id}: {d.AreaName} ({d.X},{d.Y})");
				}
			}
		}
		else if (cmd == "travel" && w.Length is >= 3 and <= 5)
		{
			const string usage = "travel port DEST | travel town DEST THING [USER] | travel dungeon_exit THING [USER]";
			if (GameSession.ParseName(NameKind.TravelKind, w[1]) is not { } kind)
			{
				Error(usage);
			}
			else if ((TravelKind)kind == TravelKind.Port)
			{
				Report(session.Travel(TravelKind.Port, w[2]));
			}
			else if ((TravelKind)kind == TravelKind.Town && w.Length >= 4)
			{
				Report(session.Travel(TravelKind.Town, w[2], w.Length == 5 ? w[4] : null, w[3]));
			}
			else if ((TravelKind)kind == TravelKind.DungeonExit && w.Length <= 4)
			{
				Report(session.Travel(TravelKind.DungeonExit, null, w.Length == 4 ? w[3] : null, w[2]));
			}
			else
			{
				Error(usage);
			}
		}
		else if (cmd == "choose" && w.Length == 2 && ToInt(w[1], out count))
		{
			Report(session.Choose(count));
		}
		else if (cmd == "skip")
		{
			if (!session.Skip().Ok)
			{
				Error("this scene cannot be skipped");
			}
		}
		else if (cmd == "wait" && w.Length == 2 && ToInt(w[1], out count))
		{
			session.Update(count);
		}
		else if (mode == Mode.Battle && cmd == "attack" && w.Length >= 2)
		{
			int target = -1;
			if (!ActorIndex(w[1], out int actor) || (w.Length == 3 && !TargetIndex(w[2], out target)))
			{
				Error("attack <member> [eN|pN]");
			}
			else
			{
				Report(session.SetCommand(actor, CommandKind.Attack, null, target));
			}
		}
		else if (mode == Mode.Battle && cmd is "use" or "item" && w.Length >= 3)
		{
			int target = -1;
			if (!ActorIndex(w[1], out int actor) || (w.Length == 4 && !TargetIndex(w[3], out target)))
			{
				Error(cmd + " <member> <id> [eN|pN]");
			}
			else
			{
				Report(session.SetCommand(actor, cmd == "use" ? CommandKind.Ability : CommandKind.Item, w[2], target));
			}
		}
		else if (mode == Mode.Battle && cmd is "defend" or "escape" && w.Length == 2)
		{
			if (!ActorIndex(w[1], out int actor))
			{
				Error(cmd + " <member>");
			}
			else
			{
				Report(session.SetCommand(actor, cmd == "defend" ? CommandKind.Defend : CommandKind.Escape));
			}
		}
		else if (mode == Mode.Battle && cmd == "macro" && w.Length == 2)
		{
			Report(session.ApplyMacro(w[1], out _));
		}
		else if (mode == Mode.Battle && cmd == "round")
		{
			PrintEvents(session.ResolveRound());
		}
		else if (mode == Mode.Battle && cmd == "auto")
		{
			FinishBattleAuto();
		}
		else if (mode == Mode.Battle && cmd == "cancel")
		{
			session.CancelAuto();
		}
		else if (cmd == "menu")
		{
			Report(session.OpenMenu());
		}
		else if (cmd is "close" or "leave")
		{
			Report(session.CloseMenu());
		}
		else if (cmd == "item" && w.Length is 2 or 3)
		{
			Report(session.UseItem(w[1], w.Length == 3 ? w[2] : ""));
		}
		else if (cmd == "tech" && w.Length is 3 or 4)
		{
			Report(session.UseAbility(w[1], w[2], w.Length == 4 ? w[3] : ""));
		}
		else if (cmd == "name" && w.Length >= 2)
		{
			Report(session.EnterName(string.Join(" ", w[1..])));
		}
		else if (cmd == "chat")
		{
			Report(session.PartyTalk());
		}
		else if (cmd == "quests")
		{
			QuestLog log = session.QuestLog();
			Print("  Objective: " + (log.Objective.Length == 0 ? "-" : log.Objective));
			foreach (Job job in log.Jobs)
			{
				Print($"  Job: {job.Title} ({GameSession.NameOf(NameKind.JobState, (int)job.State)})");
			}
		}
		else if (cmd == "codex")
		{
			foreach (CodexEntry entry in session.Encyclopedia())
			{
				Print($"  {entry.Title}: {entry.Text}");
			}
			foreach (GlossaryEntry g in session.Glossary())
			{
				Print($"  Glossary: {g.Restored} (shipped as {g.Shipped})");
			}
		}
		else if (cmd == "log")
		{
			foreach (string page in session.TextLog())
			{
				Print("  " + page);
			}
		}
		else if (cmd == "equip" && w.Length == 3)
		{
			Report(session.Equip(w[1], w[2]));
		}
		else if (cmd == "unequip" && w.Length == 3)
		{
			int slot = Enumerable.Range(0, 5).FirstOrDefault(i => GameSession.NameOf(NameKind.EquipSlot, i) == w[2], -1);
			if (slot < 0)
			{
				Error($"unknown slot '{w[2]}'");
			}
			else
			{
				Report(session.Unequip(w[1], (EquipSlot)slot));
			}
		}
		else if (cmd is "buy" or "sell" && (w.Length == 2 || (w.Length == 3 && ToInt(w[2], out count))))
		{
			Report(cmd == "buy" ? session.Buy(w[1], count) : session.Sell(w[1], count));
		}
		else if (cmd == "rest")
		{
			Report(session.Rest());
		}
		else if (cmd == "save" && w.Length == 2)
		{
			Report(session.SaveManual(w[1]));
		}
		else if (cmd == "saves")
		{
			if (session.Saves() is not { } saves)
			{
				Error("saving is disabled");
			}
			else
			{
				foreach (SaveSummary s in saves)
				{
					Print("  " + s.Slot + (s.Damaged ? " DAMAGED: " + s.Error
						: $" {s.Area} {Clock(s.PlayTimeMs)} [{string.Join(" ", s.Party)}] {s.Difficulty}{(s.Autosave ? " (auto)" : "")}"));
				}
			}
		}
		else if (cmd == "set" && w.Length == 3)
		{
			Settings settings = session.Settings();
			if (w[1] is "difficulty" or "reward")
			{
				string difficulty = settings.Difficulty;
				RewardMultiplier reward = settings.RewardMultiplier;
				if (w[1] == "difficulty")
				{
					difficulty = w[2];
				}
				else
				{
					if (GameSession.ParseName(NameKind.RewardMultiplier, w[2]) is not { } found)
					{
						Error($"bad reward multiplier '{w[2]}'");
						return true;
					}
					reward = (RewardMultiplier)found;
				}
				Report(session.SetSettings(difficulty, reward));
			}
			else if (w[1] == "auto")
			{
				Report(session.SetAutoMacro(w[2] == "-" ? "" : w[2]));
			}
			else
			{
				string key = w[1] switch { "text" => "text_speed", "battle" => "battle_speed", "shipped" => "show_shipped_names", _ => w[1] };
				Report(session.SetProfileValue(key, w[2]));
			}
		}
		else if (cmd == "status")
		{
			Status();
		}
		else if (cmd == "look")
		{
			Look();
		}
		else if (cmd == "pacing")
		{
			output.Write(session.PacingTable());
		}
		else
		{
			Error($"unknown command '{line}' in {GameSession.NameOf(NameKind.Mode, (int)mode)} mode");
		}
		Settle();
		return true;
	}

	// Runs every line of a script. Returns false if it stopped on `quit`.
	public bool RunScript(string text)
	{
		Settle();
		foreach (string line in text.Split('\n'))
		{
			if (!Execute(line))
			{
				return false;
			}
		}
		return true;
	}
}
