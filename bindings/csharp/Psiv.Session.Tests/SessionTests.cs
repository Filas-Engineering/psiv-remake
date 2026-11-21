using System.Diagnostics;
using System.Runtime.CompilerServices;

using Psiv.Session.Interop;
using Psiv.Session.Scripting;

namespace Psiv.Session.Tests;

internal static class Paths
{
	// PSIV_DATA_DIR wins; otherwise the repository is found above the test binaries.
	public static string GameData
	{
		get
		{
			string? fromEnvironment = Environment.GetEnvironmentVariable("PSIV_DATA_DIR");
			if (!string.IsNullOrEmpty(fromEnvironment))
			{
				return Path.GetFullPath(fromEnvironment);
			}
			for (DirectoryInfo? dir = new(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
			{
				string candidate = Path.Combine(dir.FullName, "game", "data");
				if (File.Exists(Path.Combine(candidate, "game.json")))
				{
					return candidate;
				}
			}
			throw new InvalidOperationException("game/data not found; set PSIV_DATA_DIR");
		}
	}

	public static string Game => Path.GetDirectoryName(GameData)!;

	public static string FreshDirectory(string name)
	{
		string path = Path.Combine(Path.GetTempPath(), "psiv-tests-" + Environment.ProcessId, name);
		if (Directory.Exists(path))
		{
			Directory.Delete(path, recursive: true);
		}
		Directory.CreateDirectory(path);
		return path;
	}
}

public class PlaythroughTests
{
	private static string RunScript(string saves, string script = "playthrough.txt")
	{
		using GameSession session = GameSession.Create(new SessionOptions { DataDirectory = Paths.GameData, SaveDirectory = saves, Seed = 6 });
		var output = new StringWriter();
		output.Write($"{session.GameInfo().Title} (seed 6)\n");
		var runner = new ScriptRunner(session, output, autoBattle: true);
		runner.RunScript(File.ReadAllText(Path.Combine(Paths.GameData, "scripts", script)));
		output.Write("End of script.\n");
		Assert.Equal(0, runner.Errors);
		return output.ToString();
	}

	[Fact]
	public void ScriptedPlaythroughMatchesTheGoldenTranscript()
	{
		string transcript = RunScript(Paths.FreshDirectory("playthrough"));
		string expected = File.ReadAllText(Path.Combine(Paths.Game, "tests", "expected", "playthrough.txt"));
		Assert.Equal(expected, transcript);
	}

	[Fact]
	public void VehicleAndTravelScriptMatchesItsGoldenTranscript()
	{
		string transcript = RunScript(Paths.FreshDirectory("travel"), "travel.txt");
		string expected = File.ReadAllText(Path.Combine(Paths.Game, "tests", "expected", "travel.txt"));
		Assert.Equal(expected, transcript);
	}

	[Fact]
	public void ScriptedPlaythroughMatchesAFreshCppRun()
	{
		string cli = Path.Combine(Paths.Game, "build", OperatingSystem.IsWindows() ? "game_cli.exe" : "game_cli");
		Assert.True(File.Exists(cli), $"{cli} is missing; run make in bindings/ first");
		string cppSaves = Paths.FreshDirectory("cpp_saves");
		var start = new ProcessStartInfo(cli)
		{
			RedirectStandardOutput = true,
			UseShellExecute = false,
			WorkingDirectory = Paths.Game,
		};
		foreach (string arg in new[] { "--data", Paths.GameData, "--seed", "6", "--auto-battle", "--script", Path.Combine(Paths.GameData, "scripts", "playthrough.txt"), "--saves", cppSaves })
		{
			start.ArgumentList.Add(arg);
		}
		using Process process = Process.Start(start)!;
		string cpp = process.StandardOutput.ReadToEnd();
		process.WaitForExit();
		Assert.Equal(0, process.ExitCode);

		string saves = Paths.FreshDirectory("cs_saves");
		Assert.Equal(cpp, RunScript(saves));
		foreach (string file in Directory.GetFiles(cppSaves))
		{
			Assert.Equal(File.ReadAllBytes(file), File.ReadAllBytes(Path.Combine(saves, Path.GetFileName(file))));
		}
		Assert.Equal(Directory.GetFiles(cppSaves).Length, Directory.GetFiles(saves).Length);
	}
}

public class NativeLayoutTests
{
	private static void Same<T>(string name) where T : unmanaged =>
		Assert.True(Native.SizeOf(name) == (uint)Unsafe.SizeOf<T>(), $"{name}: native {Native.SizeOf(name)}, managed {Unsafe.SizeOf<T>()}");

	[Fact]
	public void EveryStructHasTheNativeSize()
	{
		Assert.Equal(Native.AbiVersion, GameSession.LibraryAbiVersion);
		Assert.Contains("psiv-bindings", GameSession.LibraryVersion);
		Same<PsivAbilityInfo>("psiv_ability_info");
		Same<PsivAction>("psiv_action");
		Same<PsivActor>("psiv_actor");
		Same<PsivActiveStatus>("psiv_active_status");
		Same<PsivBattleEvent>("psiv_battle_event");
		Same<PsivBattleInfo>("psiv_battle_info");
		Same<PsivBattleOption>("psiv_battle_option");
		Same<PsivChoice>("psiv_choice");
		Same<PsivCodexEntry>("psiv_codex_entry");
		Same<PsivCombatant>("psiv_combatant");
		Same<PsivEnemy>("psiv_enemy");
		Same<PsivErrorNative>("psiv_error");
		Same<PsivExit>("psiv_exit");
		Same<PsivFieldInfo>("psiv_field_info");
		Same<PsivGameInfo>("psiv_game_info");
		Same<PsivGlossaryEntry>("psiv_glossary_entry");
		Same<PsivInnInfo>("psiv_inn_info");
		Same<PsivInventoryEntry>("psiv_inventory_entry");
		Same<PsivItemCount>("psiv_item_count");
		Same<PsivJob>("psiv_job");
		Same<PsivLine>("psiv_line");
		Same<PsivMacroStepResult>("psiv_macro_step_result");
		Same<PsivMarker>("psiv_marker");
		Same<PsivMember>("psiv_member");
		Same<PsivNotice>("psiv_notice");
		Same<PsivPoint>("psiv_point");
		Same<PsivProfile>("psiv_profile");
		Same<PsivQuestLog>("psiv_quest_log");
		Same<PsivRecordedCommand>("psiv_recorded_command");
		Same<PsivSaveSummary>("psiv_save_summary");
		Same<PsivSessionOptions>("psiv_session_options");
		Same<PsivSettings>("psiv_settings");
		Same<PsivShopInfo>("psiv_shop_info");
		Same<PsivWorldInfo>("psiv_world_info");
		Same<PsivProblem>("psiv_problem");
		Same<PsivLevelUp>("psiv_level_up");
		Same<PsivBattleResult>("psiv_battle_result");
		Same<PsivAutoRound>("psiv_auto_round");
		Same<PsivAutoBattle>("psiv_auto_battle");
		Same<PsivPacingArea>("psiv_pacing_area");
		Same<PsivPacingBoss>("psiv_pacing_boss");
		Same<PsivMacroStep>("psiv_macro_step");
		Same<PsivMacro>("psiv_macro");
		Same<PsivStatBonus>("psiv_stat_bonus");
		Same<PsivAffinity>("psiv_affinity");
		Same<PsivStatusResist>("psiv_status_resist");
		Same<PsivMemberDetail>("psiv_member_detail");
		Same<PsivComboEntry>("psiv_combo_entry");
		Same<PsivCombatView>("psiv_combat_view");
		Same<PsivEquipPreview>("psiv_equip_preview");
		Same<PsivDifficulty>("psiv_difficulty");
		Same<PsivCamera>("psiv_camera");
		Same<PsivVehicle>("psiv_vehicle");
		Same<PsivTravelDestination>("psiv_travel_destination");
		Assert.Equal(0u, Native.SizeOf("no_such_struct"));
	}

	[Fact]
	public void EnumNamesComeFromTheLibrary()
	{
		Assert.Equal("field", GameSession.NameOf(NameKind.Mode, (int)Mode.Field));
		Assert.Equal("battle_ended", GameSession.NameOf(NameKind.EventType, (int)BattleEventType.BattleEnded));
		Assert.Equal("x1_5", GameSession.NameOf(NameKind.RewardMultiplier, (int)RewardMultiplier.X1_5));
		Assert.Equal((int)RewardMultiplier.X1_5, GameSession.ParseName(NameKind.RewardMultiplier, "1.5"));
		Assert.Equal((int)BattleEventType.BattleEnded, GameSession.ParseName(NameKind.EventType, "BattleEnded"));
		Assert.Null(GameSession.ParseName(NameKind.Outcome, "won"));
		Assert.Equal("one_fallen_ally", GameSession.NameOf(NameKind.Targeting, (int)Targeting.OneFallenAlly));
		Assert.Equal("warp", GameSession.NameOf(NameKind.ActionType, (int)ActionType.Warp));
		Assert.Equal("member", GameSession.NameOf(NameKind.MacroTargetKind, (int)MacroTargetKind.Member));
		Assert.Equal("", GameSession.NameOf(NameKind.Outcome, 99));
	}
}

public class SessionTests
{
	private static GameSession NewSession(string? saves = null, EncounterMode? mode = null, Presentation presentation = Presentation.Instant) =>
		GameSession.Create(new SessionOptions { DataDirectory = Paths.GameData, SaveDirectory = saves, Seed = 285, EncounterMode = mode, Presentation = presentation });

	private static void Settle(GameSession s)
	{
		for (int guard = 0; guard < 10000 && s.Mode == Mode.Event; guard++)
		{
			if (s.LinePending)
			{
				s.ConfirmLine();
			}
			else if (s.ChoicePending)
			{
				s.Choose(0);
			}
			else
			{
				s.Update(100);
			}
		}
	}

	[Fact]
	public void ABadDataDirectoryThrowsWithTheLoaderProblems()
	{
		var e = Assert.Throws<PsivDataException>(() => GameSession.Create(new SessionOptions { DataDirectory = "no/such/dir" }));
		Assert.Equal(PsivStatus.DataError, e.Error.Status);
		Assert.Equal("binding.data_error", e.Error.Key);
		DataProblem problem = Assert.Single(e.Problems);
		Assert.Equal("data.cannot_read", problem.Key);
		Assert.Equal(["no/such/dir/game.json"], problem.Params);
		var core = Assert.Throws<PsivDataException>(() => GameSession.Create(new SessionOptions { DataDirectory = Paths.GameData, CoreDataDirectory = "no/such/core" }));
		Assert.Equal("load.cannot_open", Assert.Single(core.Problems).Key);
		using GameSession explicitCore = GameSession.Create(new SessionOptions { DataDirectory = Paths.GameData, CoreDataDirectory = Path.Combine(Paths.Game, "..", "core", "data") });
		Assert.Equal(Mode.Title, explicitCore.Mode);
	}

	[Fact]
	public void RefusalsAreResultsAndBadArgumentsThrow()
	{
		using GameSession s = NewSession();
		PsivResult walk = s.Walk(Direction.East);
		Assert.False(walk.Ok);
		Assert.Equal(PsivStatus.Refused, walk.Error!.Status);
		Assert.Equal("game.refuse.walk_field_only", walk.Error.Key);
		Assert.Throws<PsivException>(() => walk.ThrowIfFailed());
		var bad = Assert.Throws<PsivException>(() => s.Update(16, (Direction)9));
		Assert.Equal(PsivStatus.InvalidArgument, bad.Error.Status);
		Assert.Equal(["direction", "9"], bad.Error.Params);
		Assert.Null(s.FieldInfo());
		Assert.Null(s.Saves());
		Assert.True(s.NewGame().Ok);
		Settle(s);
		PsivResult difficulty = s.SetSettings("no_such_difficulty", RewardMultiplier.X1);
		Assert.Equal("game.refuse.unknown_difficulty", difficulty.Error!.Key);
		Assert.Equal(["no_such_difficulty"], difficulty.Error.Params);
	}

	[Fact]
	public void ADisposedSessionCannotBeUsed()
	{
		GameSession s = NewSession();
		s.Dispose();
		Assert.Throws<ObjectDisposedException>(() => s.NewGame());
	}

	[Fact]
	public void SaveAndLoadRoundTrip()
	{
		string saves = Paths.FreshDirectory("save_load");
		using (GameSession s = NewSession(saves))
		{
			Assert.True(s.NewGame().Ok);
			Settle(s);
			Assert.True(s.OpenMenu().Ok);
			Assert.True(s.SaveManual("manual_1").Ok);
			Assert.False(s.SaveManual("manual_42").Ok);
			Assert.True(s.CloseMenu().Ok);
			Assert.True(s.Walk(Direction.South).Ok);
			Assert.Equal(5, s.FieldInfo()!.LeaderY);
			SaveSummary summary = s.Saves()!.Single(x => x.Slot == "manual_1");
			Assert.Equal("tidewater", summary.Area);
			Assert.Equal(["fighter:3"], summary.Party);
			Assert.False(summary.Autosave);
		}
		using (GameSession s = NewSession(saves))
		{
			Assert.True(s.Load("manual_1").Ok);
			Assert.Equal(Mode.Field, s.Mode);
			FieldInfo field = s.FieldInfo()!;
			Assert.Equal((3, 4), (field.LeaderX, field.LeaderY));
			Assert.True(s.Flag("tidewater.arrived"));
			Assert.False(s.Load("manual_9").Ok);
		}
	}

	[Fact]
	public void ABattlePlayedThroughTheEventStream()
	{
		using GameSession s = NewSession(mode: EncounterMode.Random);
		Assert.True(s.NewGame().Ok);
		Settle(s);
		for (int i = 0; i < 10; i++)
		{
			Assert.True(s.Walk(Direction.East).Ok);
			Settle(s);
		}
		Assert.Equal("reed_path", s.FieldInfo()!.AreaId);
		Direction direction = Direction.East;
		for (int i = 0; i < 200 && s.Mode == Mode.Field; i++)
		{
			int x = s.FieldInfo()!.LeaderX;
			direction = x >= 8 ? Direction.West : x <= 2 ? Direction.East : direction;
			s.Walk(direction);
			Settle(s);
		}
		Assert.Equal(Mode.Battle, s.Mode);
		BattleInfo info = s.BattleInfo()!;
		Assert.True(info.Live);
		Assert.Equal(BattleSource.Random, info.Source);

		IReadOnlyList<Combatant> combatants = s.Combatants();
		var hp = combatants.Select(c => c.Hp).ToArray();
		IReadOnlyList<BattleOption> options = s.BattleOptions(0);
		Assert.NotEmpty(options);
		var events = new List<BattleEvent>();
		for (int round = 0; round < 40 && s.Mode == Mode.Battle; round++)
		{
			int target = s.Combatants().First(c => c.Side == Side.Enemy && c.Alive).Index;
			BattleOption? spell = s.BattleOptions(0).FirstOrDefault(o => o.Usable && o.AbilityKind == AbilityKind.Technique && o.Targeting == Targeting.OneEnemy);
			PsivResult set = spell is null ? s.SetCommand(0, CommandKind.Attack, null, target) : s.SetCommand(0, CommandKind.Ability, spell.Id, target);
			Assert.True(set.Ok, set.Error?.Text);
			IReadOnlyList<BattleEvent> roundEvents = s.ResolveRound();
			Assert.Contains(roundEvents, e => e.Type == BattleEventType.RoundStarted);
			events.AddRange(roundEvents);
		}
		Assert.Equal(Mode.Field, s.Mode);
		Assert.Equal(BattleEventType.BattleStarted, events[0].Type);
		Assert.Equal(combatants.Count, events[0].Ids.Count);
		BattleEvent end = events[^1];
		Assert.Equal(BattleEventType.BattleEnded, end.Type);
		Assert.Equal(Outcome.Victory, end.Outcome);
		Assert.True(end.Experience > 0);
		foreach (BattleEvent e in events.Where(e => e.Type is BattleEventType.DamageDealt or BattleEventType.Healed))
		{
			hp[e.Target] = e.Hp;
		}
		Assert.All(events.Where(e => e.Type == BattleEventType.CommandsLocked), e => Assert.Contains(e.Commands, c => c.Actor == 0 && c.Kind is CommandKind.Attack or CommandKind.Ability));
		IReadOnlyList<Combatant> after = s.Combatants();
		Assert.False(s.BattleInfo()!.Live);
		Assert.Equal(hp, after.Select(c => c.Hp).ToArray());
		Assert.All(after.Where(c => c.Side == Side.Enemy), c => Assert.False(c.Alive));
		Assert.Contains(s.DrainNotices(), n => n.Key == "game.victory");
	}

	[Fact]
	public void PresentationActionsWaitForTheEngine()
	{
		using GameSession s = NewSession(presentation: Presentation.Wait);
		var pushed = new List<PresentationAction>();
		s.SetActionCallback(pushed.Add);
		Assert.True(s.NewGame().Ok);
		for (int i = 0; i < 20; i++)
		{
			s.Update(100);
		}
		Assert.Equal(Mode.Event, s.Mode);
		Assert.False(s.LinePending);
		PresentationAction fade = Assert.Single(s.PollActions());
		Assert.Equal(ActionType.FadeIn, fade.Type);
		Assert.Equal(ActionPhase.Begin, fade.Phase);
		Assert.Equal(1.0, fade.Seconds);
		Assert.Equal(fade, Assert.Single(pushed));
		s.FinishAction(fade.Handle);
		s.Update(100);
		Line line = s.Line()!;
		Assert.Equal("", line.SpeakerId);
		s.ConfirmLine();
		// The dockhand walks over and turns before it speaks; the field already shows where each step ends.
		(string Description, int X, int Y, Direction Facing)[] steps =
		[
			("move dockhand to dock_front", 10, 3, Direction.South),
			("move dockhand to greet_spot", 5, 4, Direction.South),
			("face dockhand fighter", 5, 4, Direction.West),
		];
		foreach ((string description, int x, int y, Direction facing) in steps)
		{
			s.Update(100);
			Assert.Null(s.Line());
			PresentationAction step = Assert.Single(s.PollActions());
			Assert.Equal(description, step.Description);
			FieldActor dockhand = s.Field()!.Actors.Single(a => a.Id == "dockhand");
			Assert.Equal((x, y, facing, true, true), (dockhand.X, dockhand.Y, dockhand.Facing, dockhand.Visible, dockhand.Scripted));
			s.FinishAction(step.Handle);
		}
		s.Update(100);
		line = s.Line()!;
		Assert.Equal("dockhand", line.SpeakerId);
		Assert.Equal("Dockhand", line.SpeakerName);
		Assert.Equal("Dockhand: " + line.Text, line.Display);
		s.SetActionCallback(null);
	}

	[Fact]
	public void FieldViewAndMenus()
	{
		using GameSession s = NewSession();
		Assert.True(s.NewGame().Ok);
		Settle(s);
		FieldView view = s.Field()!;
		Assert.Equal("tidewater", view.Info.AreaId);
		Assert.Equal(14 * 9, view.Grid.Count);
		Assert.True(view.Cell(3, 4).HasFlag(CellFlags.Walkable));
		Assert.Equal(CellFlags.Wall, view.Cell(0, 0) & CellFlags.TerrainMask);
		Assert.Contains(view.Points, p => p.Kind == PointKind.Shop);
		Assert.Contains(view.Exits, e => e.ToArea == "reed_path");
		// The arrival walked the dockhand back to its talk point; the guide has not been touched.
		Assert.Equal(["dockhand", "guide"], view.Actors.Select(a => a.Id));
		Assert.Equal(new FieldActor("dockhand", "Dockhand", 10, 2, Direction.South, true, true), view.Actors[0]);
		Assert.Equal(new FieldActor("guide", "Harbor Guide", 5, 2, Direction.South, true, false), view.Actors[1]);
		// Every actor is in the ids; those standing on the reed path or in Saltgrass are not in this area's view.
		Assert.Equal(["dockhand", "ferrywoman", "guide", "kilnwright", "lamplighter", "peddler"], s.ActorIds());
		Assert.DoesNotContain(view.Actors, a => a.Id == "peddler");
		Assert.True(s.OpenMenu().Ok);
		Member fighter = s.Party().Single();
		Assert.Equal("fighter", fighter.Id);
		Assert.NotEqual("", fighter.Equipment[(int)EquipSlot.Weapon]);
		Assert.NotEmpty(s.MemberAbilities("fighter"));
		Assert.Contains(s.Inventory(), i => i.Id == "tonic" && !i.IsEquipment);
		Assert.True(s.CloseMenu().Ok);
		QuestLog log = s.QuestLog();
		Assert.NotEmpty(log.Objective);
		Assert.Equal(JobState.Available, Assert.Single(log.Jobs).State);
		Assert.Equal("Test Fighter", s.DisplayName("fighter"));
		Assert.Contains("Test Fighter", s.Render("game.equipped", "fighter", "tonic"));
		Assert.True(s.SetProfileValue("text_language", "ja").Ok);
		Assert.Equal("ja", s.Profile().TextLanguage);
		Assert.NotEqual(log.Objective, s.QuestLog().Objective);
		Assert.True(s.SetProfile(s.Profile() with { TextLanguage = "en" }).Ok);
		Assert.Equal(log.Objective, s.QuestLog().Objective);
		Assert.Equal("game.refuse.unknown_language", s.SetProfileValue("text_language", "xx").Error!.Key);
	}

	private static WalkStatus WalkToMarker(GameSession s, string name)
	{
		Marker marker = s.Field()!.Markers.Single(m => m.Name == name);
		Assert.True(s.WalkTo(marker.X, marker.Y).Ok);
		WalkStatus status;
		while ((status = s.WalkStep()) == WalkStatus.Walking)
		{
			Settle(s);
		}
		return status;
	}

	[Fact]
	public void TheGuideGivesLineDataAndANameEntry()
	{
		using GameSession s = NewSession();
		Assert.True(s.NewGame().Ok);
		Settle(s);
		Assert.Equal(WalkStatus.Arrived, WalkToMarker(s, "guide_front"));
		Assert.True(s.Face(Direction.North).Ok);
		Assert.True(s.Interact().Ok);
		Line line = s.Line()!;
		Assert.Equal(("guide", "Harbor Guide", "tidewater.guide.bell", "guide_smile"), (line.SpeakerId, line.SpeakerName, line.TextId, line.Portrait));
		s.ConfirmLine();
		Choice ask = s.Choice()!;
		Assert.True(ask.YesNo);
		Assert.Equal(["Yes", "No"], ask.Options);
		Assert.True(s.SetProfileValue("text_language", "ja").Ok);
		Assert.Equal(["はい", "いいえ"], s.Choice()!.Options);
		Assert.True(s.SetProfileValue("text_language", "en").Ok);
		Assert.True(s.Choose(0).Ok);
		s.ConfirmLine();
		Assert.Equal(MenuKind.NameEntry, s.MenuKind);
		Assert.Equal("fighter", s.NameEntryMember);
		Assert.True(s.EnterName("Bryn").Ok);
		line = s.Line()!;
		Assert.Equal(("fighter", "Bryn", ""), (line.SpeakerId, line.SpeakerName, line.Portrait));
		Assert.Equal("Bryn: Tell the ferry I am staying a while.", line.Display);
		Settle(s);
		Assert.Equal(Mode.Field, s.Mode);
		Assert.True(s.WalkTo(0, 0).Ok);
		Assert.Equal(WalkStatus.NoPath, s.WalkStep());
		PacingArea area = Assert.Single(s.PacingAreas());
		Assert.Equal("tidewater", area.Area);
		Assert.True(area.Steps > 0);
		Assert.Empty(s.PacingBosses());
	}

	[Fact]
	public void ALostEventBattleStaysReadableReplaysAndRetries()
	{
		using GameSession s = NewSession(Paths.FreshDirectory("lost_battle"));
		Assert.True(s.NewGame().Ok);
		Settle(s);
		Assert.Equal(WalkStatus.Arrived, WalkToMarker(s, "hold_front"));
		Assert.True(s.Face(Direction.North).Ok);
		Assert.True(s.Interact().Ok);
		Settle(s);
		Assert.Equal(Mode.Battle, s.Mode);
		AutoBattle played = s.PlayBattle();
		Assert.True(played.Ended);
		Assert.Null(played.Refusal);
		Assert.Equal(BattleEventType.BattleStarted, played.Rounds[0].Events[0].Type);
		Assert.Equal(Mode.Title, s.Mode);
		BattleInfo info = s.BattleInfo()!;
		Assert.False(info.Live);
		Assert.True(info.GameOver);
		Assert.Equal(Outcome.Defeat, info.Outcome);
		Assert.False(s.Combatants()[0].Alive);
		BattleResult result = s.BattleResult()!;
		Assert.Equal(("training", Outcome.Defeat, true, 0), (result.Encounter, result.Outcome, result.GameOver, result.Experience));
		Assert.Empty(result.LevelUps);

		string record = s.BattleRecord()!;
		Replay replay = s.ReplayBattle(record);
		Assert.Equal(played.Rounds.Sum(r => r.Events.Count), replay.Events.Count);
		Assert.StartsWith("0 battle_started", replay.EventText);
		var damaged = Assert.Throws<PsivException>(() => s.ReplayBattle(record.Replace("seed ", "seed 9")));
		Assert.Equal(PsivStatus.DataError, damaged.Error.Status);

		Assert.True(s.WorldInfo().CanRetry);
		Assert.True(s.RetryBattle().Ok);
		Settle(s);
		Assert.Equal(Mode.Battle, s.Mode);
		Assert.Null(s.BattleResult());
		Assert.True(s.PlayBattle().Ended);
		s.DismissBattle();
		Assert.Null(s.BattleInfo());
	}

	[Fact]
	public void TheLineupMacrosMemberDetailAndComboLogFromTheMenus()
	{
		using GameSession s = NewSession(Paths.FreshDirectory("menus"));
		Assert.True(s.NewGame().Ok);
		Settle(s);
		Assert.Equal(WalkStatus.Arrived, WalkToMarker(s, "guide_front"));
		Assert.True(s.Face(Direction.North).Ok);
		Assert.True(s.Interact().Ok);
		Settle(s);
		Assert.True(s.EnterName("Bryn").Ok);
		Settle(s);

		Assert.Equal("game.refuse.party_menu_only", s.SwapLineup(0, 1).Error!.Key);
		Assert.True(s.OpenMenu().Ok);
		Assert.True(s.SwapLineup(0, 1).Ok);
		Assert.Equal(["mystic", "fighter", "medic"], s.Party().Select(m => m.Id));
		Assert.Equal(["5"], s.SwapLineup(0, 5).Error!.Params);
		Assert.True(s.SaveManual("manual_1").Ok);
		Assert.True(s.QuitToTitle().Ok);
		Assert.True(s.Load("manual_1").Ok);
		Assert.Equal(["mystic", "fighter", "medic"], s.Party().Select(m => m.Id));

		Macro tempest = s.MacroList().Single(m => m.Name == "tempest");
		Assert.Equal(new MacroStep("engineer", "Test Engineer", CommandKind.Ability, "flare_burst", "Flare Burst", MacroTargetKind.All, "", "", -1, MacroIssue.NotInParty), tempest.Steps[2]);
		Assert.Equal(MacroIssue.None, tempest.Steps[3].Issue);
		Assert.True(s.OpenMenu().Ok);
		Assert.True(s.SwapLineup(1, 0).Ok);
		Assert.True(s.CreateMacro("arc").Ok);
		Assert.True(s.SetMacroStep("arc", 0, "medic", CommandKind.Ability, "glint", MacroTargetKind.Enemy, targetEnemy: 0).Ok);
		Assert.True(s.SetMacroStep("arc", 1, "mystic", CommandKind.Ability, "spark").Ok);
		PsivResult unknown = s.SetMacroStep("arc", 2, "medic", CommandKind.Defend);
		Assert.Equal(("game.refuse.macro_member_twice", "Test Medic already has a step in this macro."), (unknown.Error!.Key, unknown.Error.Text));
		Assert.Equal("game.refuse.macro_unknown_member", s.SetMacroStep("arc", 2, "scout", CommandKind.Attack).Error!.Key);
		Assert.True(s.SetMacroStep("arc", 2, "fighter", CommandKind.Attack, target: MacroTargetKind.Enemy, targetEnemy: 1).Ok);
		Assert.True(s.MoveMacroStep("arc", 2, 0).Ok);
		Assert.True(s.RemoveMacroStep("arc", 0).Ok);
		Assert.True(s.MoveMacro("arc", 0).Ok);
		Macro arc = s.MacroList()[0];
		Assert.Equal("arc", arc.Name);
		Assert.Equal(["medic", "mystic"], arc.Steps.Select(x => x.Member));
		Assert.Equal(("Glint", 0), (arc.Steps[0].IdName, arc.Steps[0].TargetEnemy));
		Assert.True(s.SetAutoMacro("tempest").Ok);
		Assert.True(s.RenameMacro("tempest", "storm").Ok);
		Assert.Equal("storm", s.Settings().AutoMacro);
		s.DrainNotices();
		Assert.True(s.DeleteMacro("storm").Ok);
		Assert.Equal("", s.Settings().AutoMacro);
		Assert.Equal("game.auto_macro_cleared", Assert.Single(s.DrainNotices()).Key);
		Assert.Equal("game.refuse.no_macro", s.DeleteMacro("storm").Error!.Key);

		MemberDetail mystic = s.MemberDetail("mystic")!;
		Assert.Equal(Element.Heat, mystic.AttackElement);
		Assert.Equal(50, mystic.Affinities[Element.Shock]);
		Assert.Equal(new StatusResist("sleep", "Sleep", 100), Assert.Single(mystic.StatusResists));
		Assert.Equal(3, mystic.EquipmentBonus[(int)EquipSlot.Weapon].Mental);
		Assert.True(mystic.ExperienceToNext > 0);
		Assert.Null(s.MemberDetail("scout"));
		AbilityInfo focus = s.MemberAbilities("fighter").Single(a => a.Id == "focus_shot");
		Assert.Equal((3, 3), (focus.Uses, focus.MaxUses));
		Assert.True(s.CloseMenu().Ok);

		Assert.Equal(15, s.ComboLog().Total);
		Assert.Empty(s.ComboLog().Entries);
		Assert.Equal(WalkStatus.Arrived, WalkToMarker(s, "hold_front"));
		Assert.True(s.Face(Direction.North).Ok);
		Assert.True(s.Interact().Ok);
		Settle(s);
		for (int round = 0; round < 10 && s.Mode == Mode.Battle && s.ComboLog().Entries.Count == 0; round++)
		{
			Assert.True(s.ApplyMacro("arc", out _).Ok);
			s.ResolveRound();
		}
		ComboEntry twin = Assert.Single(s.ComboLog().Entries);
		Assert.Equal(("twin_arc", "Twin Arc"), (twin.Recipe, twin.ResultName));
		Assert.Equal(["medic", "mystic"], twin.Participants);
		Assert.Equal(["Glint", "Spark"], twin.AbilityNames);

		if (s.Mode == Mode.Battle)
		{
			s.PlayBattle();
		}
		Settle(s);
		Assert.Equal(Mode.Field, s.Mode);
		Assert.True(s.OpenMenu().Ok);
		Assert.Equal("game.refuse.combo_not_found", s.MacroFromCombo("storm_spark", "Storm").Error!.Key);
		Assert.True(s.MacroFromCombo(twin.Recipe, "Twin").Ok);
		Macro made = s.MacroList().Single(m => m.Name == "Twin");
		Assert.Equal([("medic", "glint"), ("mystic", "spark")], made.Steps.Select(x => (x.Member, x.Id)));
	}

	private static GameSession AfterGuide(string saves)
	{
		GameSession s = NewSession(Paths.FreshDirectory(saves));
		Assert.True(s.NewGame().Ok);
		Settle(s);
		Assert.Equal(WalkStatus.Arrived, WalkToMarker(s, "guide_front"));
		Assert.True(s.Face(Direction.North).Ok);
		Assert.True(s.Interact().Ok);
		Settle(s);
		Assert.True(s.EnterName("Bryn").Ok);
		Settle(s);
		Assert.Equal(Mode.Field, s.Mode);
		return s;
	}

	[Fact]
	public void EquipmentIsComparedBeforeItGoesOn()
	{
		using GameSession s = AfterGuide("preview");
		EquipPreview plate = s.EquipPreview("fighter", "rivet_plate")!;
		Assert.Equal(("Rivet Plate", EquipSlot.Body, "padded_coat", true), (plate.PieceName, plate.Slot, plate.Replaces, plate.CanWear));
		Assert.Equal((6, 20, -2), (plate.Bonus.Defense, plate.Bonus.MaxHp, plate.Bonus.Agility));
		Assert.Equal(3, plate.After.Stats.Defense - plate.Before.Stats.Defense);
		Assert.Equal(["Bryn"], plate.WearerNames);
		Assert.Null(plate.Refusal);
		Assert.Equal(s.MemberDetail("fighter")!.Critical, plate.Before.Stats.Critical);

		EquipPreview ring = s.EquipPreview("mystic", "spark_ring")!;
		Assert.Equal(["Glint"], ring.GrantedNames);
		Assert.Contains("glint", ring.After.Abilities);
		Assert.DoesNotContain("glint", ring.Before.Abilities);
		Assert.Equal(new StatusResist("sleep", "Sleep", 100), Assert.Single(ring.Before.StatusResists));
		Assert.Empty(ring.After.StatusResists);

		EquipPreview sling = s.EquipPreview("fighter", "volley_sling")!;
		Assert.False(sling.CanWear);
		Assert.Equal("equip.wrong_category", sling.Refusal!.Key);
		Assert.Equal((sling.Before.Stats, sling.Before.BasicAttack), (sling.After.Stats, sling.After.BasicAttack));
		Assert.Equal("Scatter Shot", sling.AttackName);
		Assert.Null(s.EquipPreview("scout", "tin_cap"));
		Assert.Null(s.EquipPreview("fighter", "tonic"));
	}

	[Fact]
	public void DescriptionsFollowTheProfileLanguage()
	{
		using GameSession s = NewSession();
		Assert.Equal("A bitter herbal draught. Restores 30 HP to one ally.", s.Description(DescriptionKind.Item, "tonic"));
		Assert.Equal("A snap of lightning at one foe.", s.Description(DescriptionKind.Ability, "spark"));
		Assert.Contains("Gear Lock", s.Description(DescriptionKind.Item, "loosen_oil"));
		Assert.NotEmpty(s.Description(DescriptionKind.Enemy, "brass_warden")!);
		Assert.Null(s.Description(DescriptionKind.Enemy, "tonic"));
		Assert.Equal("difficulty", GameSession.NameOf(NameKind.DescriptionKind, (int)DescriptionKind.Difficulty));
		Assert.True(s.SetProfileValue("text_language", "ja").Ok);
		Assert.Equal("なめらかな石のお守り。眠らされなくなる。", s.Description(DescriptionKind.Equipment, "calm_charm"));
	}

	[Fact]
	public void TheBagIsListedInFixedOrders()
	{
		using GameSession s = AfterGuide("sort");
		Assert.True(s.OpenMenu().Ok);
		Assert.True(s.Unequip("fighter", EquipSlot.Weapon).Ok);
		Assert.True(s.Unequip("fighter", EquipSlot.Head).Ok);
		Assert.Equal(["tonic", "copper_blade", "tin_cap"], s.Inventory().Select(i => i.Id));
		Assert.Equal(["Copper Blade", "Tin Cap", "Tonic"], s.Inventory(ItemSort.Name).Select(i => i.Name));
		Assert.Equal(["tonic", "copper_blade", "tin_cap"], s.Inventory(ItemSort.Count).Select(i => i.Id));
		Assert.Equal([false, true, true], s.Inventory(ItemSort.Kind).Select(i => i.IsEquipment));
		Assert.Equal("count", GameSession.NameOf(NameKind.ItemSort, (int)ItemSort.Count));
	}

	[Fact]
	public void DifficultyPresetsAreListedAndChangeOnlyInAGame()
	{
		using GameSession s = NewSession();
		Difficulties at = s.Difficulties();
		Assert.False(at.CanChange);
		Assert.Equal(["hard", "original"], at.Presets.Select(d => d.Id));
		Assert.Equal(("Hard", 120), (at.Presets[0].Name, at.Presets[0].RewardPercent));
		Assert.Contains("smarter", at.Presets[0].Description);
		Assert.Equal("game.refuse.settings_no_game", s.SetSettings("hard", RewardMultiplier.X1).Error!.Key);
		Assert.True(s.NewGame().Ok);
		Settle(s);
		Assert.True(s.Difficulties().CanChange);
		Assert.True(s.SetSettings("hard", RewardMultiplier.X1).Ok);
		Assert.Equal("hard", s.Settings().Difficulty);
	}

	[Fact]
	public void CamerasMusicAndDurationsComeFromTheData()
	{
		using GameSession s = NewSession();
		Camera east = s.Cameras().Single(c => c.Id == "lookout_east");
		Assert.Equal((CameraTarget.Marker, "reed_path", "lookout_east", 1.25), (east.Target, east.Area, east.Marker, east.Zoom));
		Camera peddler = s.Cameras().Single(c => c.Id == "peddler");
		Assert.Equal((CameraTarget.Follow, "peddler", 1.5), (peddler.Target, peddler.Follow, peddler.Zoom));
		GameInfo info = s.GameInfo();
		Assert.Equal(("title_harbor_dawn", "victory_bright_sail", "defeat_low_tide"), (info.TitleMusic, info.VictoryMusic, info.DefeatMusic));
		Assert.True(s.NewGame().Ok);
		IReadOnlyList<PresentationAction> arrival = s.PollActions();
		Assert.Equal(1.0, arrival.First(a => a.Type == ActionType.FadeIn).Duration);
		Settle(s);
		Assert.Equal("town_tide_bells", s.FieldInfo()!.Music);
		Assert.Equal("leader", GameSession.NameOf(NameKind.CameraTarget, (int)CameraTarget.Leader));

		// Onto the reed path: its own track, and the lookout on the wall in its corner, talked to from below.
		FieldExit gate = s.Field()!.Exits.Single(x => x.ToArea == "reed_path");
		Assert.True(s.WalkTo(gate.X, gate.Y).Ok);
		while (s.WalkStep() == WalkStatus.Walking)
		{
			Settle(s);
		}
		Settle(s);
		FieldView reeds = s.Field()!;
		Assert.Equal(("reed_path", "field_marsh_reeds"), (reeds.Info.AreaId, reeds.Info.Music));
		Assert.Equal(CellFlags.Wall, reeds.Cell(1, 1) & CellFlags.TerrainMask);
		Assert.Contains(reeds.Points, p => p.Kind == PointKind.Talk && p.Id == "lookout" && (p.X, p.Y) == (1, 1));
		Assert.Equal(WalkStatus.Arrived, WalkToMarker(s, "lookout_front"));
		Assert.True(s.Face(Direction.North).Ok);
		s.PollActions();
		Assert.True(s.Interact().Ok);
		Settle(s);
		Assert.Equal(Mode.Battle, s.Mode);
		Assert.Equal("battle_marsh_ambush", s.BattleInfo()!.Music);
		Assert.Equal(0.5, s.PollActions().Single(a => a.Type == ActionType.PanelShow && a.Subject == "ladder").Duration);
	}

	[Fact]
	public void PartyTalkComesUpByItselfOnArrival()
	{
		using GameSession s = AfterGuide("ambient");
		s.DrainNotices();
		FieldExit gate = s.Field()!.Exits.Single(x => x.ToArea == "reed_path");
		Assert.True(s.WalkTo(gate.X, gate.Y).Ok);
		while (s.WalkStep() == WalkStatus.Walking)
		{
			Settle(s);
		}
		Settle(s);
		Notice[] ambient = s.DrainNotices().Where(n => n.Kind == NoticeKind.Ambient).ToArray();
		Assert.Equal(["talk.reeds.1", "talk.reeds.2"], ambient.Select(n => n.Key));
		Assert.StartsWith("Test Mystic: ", ambient[0].Text);
		Assert.Equal(Mode.Field, s.Mode);
		Assert.Equal("ambient", GameSession.NameOf(NameKind.NoticeKind, (int)NoticeKind.Ambient));
	}

	// The skimmer from the brine flats' mooring, driven over water through a vehicle battle, left on the islet; the ferry
	// to Cinder Quay, a return to a town of the isles and the way out of the Drowned Kiln, all through the wrapper.
	[Fact]
	public void VehiclesAndTravelWorkThroughTheWrapper()
	{
		using GameSession s = GameSession.Create(new SessionOptions { DataDirectory = Paths.GameData, SaveDirectory = Paths.FreshDirectory("vehicles"), Seed = 6 });
		Assert.Equal("dungeon_exit", GameSession.NameOf(NameKind.TravelKind, (int)TravelKind.DungeonExit));
		Assert.Equal("port", GameSession.NameOf(NameKind.MenuKind, (int)MenuKind.Port));
		var output = new StringWriter();
		var runner = new ScriptRunner(s, output, autoBattle: true);
		runner.RunScript("new\nwalk guide_front\nface north\ntalk\nchoose 0\nname Bryn\nwalk 13 4\nwalk 10 4\nwalk 19 4\nwalk 6 0\nwalk 0 10\n" +
			"walk post_front\nface east\ntalk\nwalk 6 4\nface east\n");
		Assert.Equal(0, runner.Errors);
		FieldView field = s.Field()!;
		Assert.Equal("coast", field.Info.Region);
		Assert.Equal("", field.Info.Aboard);
		FieldVehicle skimmer = Assert.Single(field.Vehicles);
		Assert.Equal(("reed_skimmer", "Reed Skimmer", 7, 4, false), (skimmer.Id, skimmer.Name, skimmer.X, skimmer.Y, skimmer.Aboard));
		Assert.True(s.Board().Ok);
		field = s.Field()!;
		Assert.Equal("reed_skimmer", field.Info.Aboard);
		Assert.Equal(field.Info.StepMs / 2, field.Info.LeaderStepMs);
		Assert.True(Assert.Single(field.Vehicles).Aboard);
		Assert.Equal("game.refuse.aboard", s.Interact().Error!.Key);
		int vehicleBattles = 0;
		for (int i = 0; i < 60 && s.FieldInfo()!.LeaderX < 19; ++i)
		{
			s.Walk(Direction.East);
			if (s.Mode == Mode.Battle)
			{
				Assert.Equal("reed_skimmer", s.BattleInfo()!.Vehicle);
				++vehicleBattles;
				s.PlayBattle();
			}
		}
		Assert.True(vehicleBattles >= 1);
		Assert.Equal(19, s.FieldInfo()!.LeaderX);
		Assert.True(s.LeaveVehicle().Ok);
		Assert.Equal(("", 20), (s.FieldInfo()!.Aboard, s.FieldInfo()!.LeaderX));
		Assert.Equal("game.refuse.not_aboard", s.LeaveVehicle().Error!.Key);
		Assert.Equal((19, 4), (s.FieldVehicles()[0].X, s.FieldVehicles()[0].Y));

		runner.RunScript("walk 21 2\nface north\ntalk\nwalk 22 4\nface east\ntalk\nwalk 20 4\nface west\nboard\nw 11\nw 8\nwalk pier_front\nface north\ntalk\n");
		Assert.Equal(0, runner.Errors);
		Assert.Equal(MenuKind.Port, s.MenuKind);
		Assert.Equal(TravelKind.Town, s.Inventory().Single(i => i.Id == "homing_shell").Travel);
		Assert.Equal(TravelKind.DungeonExit, s.Inventory().Single(i => i.Id == "waymark_chalk").Travel);
		Assert.Null(s.Inventory().Single(i => i.Id == "tonic").Travel);
		TravelDestination quay = Assert.Single(s.TravelDestinations(TravelKind.Port));
		Assert.Equal(("quay_pier", "Cinder Quay", "isles"), (quay.Id, quay.AreaName, quay.Region));
		Assert.True(s.Travel(TravelKind.Port, "quay_pier").Ok);
		Assert.Equal("cinder_quay", s.FieldInfo()!.AreaId);
		Assert.Contains(s.Field()!.Points, p => p.Kind == PointKind.Port && p.Id == "quay_pier" && p.Active);
		Assert.True(s.OpenMenu().Ok);
		Assert.Empty(s.TravelDestinations(TravelKind.Town));
		Assert.Equal("game.refuse.no_town", s.Travel(TravelKind.Town, "saltgrass", null, "homing_shell").Error!.Key);
		Assert.True(s.CloseMenu().Ok);
		runner.RunScript("walk 13 4\nmenu\n");
		TravelDestination town = Assert.Single(s.TravelDestinations(TravelKind.Town));
		Assert.Equal("cinder_quay", town.Id);
		Assert.True(s.Travel(TravelKind.Town, "cinder_quay", null, "homing_shell").Ok);
		runner.RunScript("face south\ntalk\ntravel port saltgrass_pier\nwalk 47 12\nmenu\n");
		Assert.Equal(0, runner.Errors);
		Assert.Equal("kiln_upper", s.FieldInfo()!.AreaId);
		TravelDestination way = Assert.Single(s.TravelDestinations(TravelKind.DungeonExit));
		Assert.Equal(("saltgrass", 46, 12), (way.Area, way.X, way.Y));
		Assert.True(s.Travel(TravelKind.DungeonExit, null, null, "waymark_chalk").Ok);
		Assert.Equal(("saltgrass", 46, 12), (s.FieldInfo()!.AreaId, s.FieldInfo()!.LeaderX, s.FieldInfo()!.LeaderY));
	}
}
