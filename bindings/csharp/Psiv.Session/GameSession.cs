using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using Psiv.Session.Interop;

namespace Psiv.Session;

public sealed class SessionHandle : SafeHandle
{
	public SessionHandle() : base(IntPtr.Zero, ownsHandle: true) { }

	internal SessionHandle(IntPtr handle) : base(IntPtr.Zero, ownsHandle: true) => SetHandle(handle);

	public override bool IsInvalid => handle == IntPtr.Zero;

	protected override bool ReleaseHandle()
	{
		Native.SessionDestroy(handle);
		return true;
	}
}

// One game session over libpsiv_session. Not thread safe, like the C library. Every value it returns is a
// managed copy, so the C library's "valid until the next MUTATES call" rule never reaches the caller.
public sealed unsafe class GameSession : IDisposable
{
	private readonly SessionHandle _handle;
	private GCHandle _callbackTarget;
	private Action<PresentationAction>? _callback;

	private GameSession(SessionHandle handle) => _handle = handle;

	public static string LibraryVersion => Native.Str(Native.Version());

	public static uint LibraryAbiVersion => Native.AbiVersionOf();

	public static string NameOf(NameKind kind, int value) => Native.Str(Native.NameOf((int)kind, value));

	// Also the spellings older files use for reward multipliers and event types; null for an unknown name.
	public static int? ParseName(NameKind kind, string text) => Native.ParseName((int)kind, text, out int value) == 0 ? value : null;

	public static GameSession Create(SessionOptions options)
	{
		if (Native.AbiVersionOf() != Native.AbiVersion)
		{
			throw new PsivException(new PsivError(PsivStatus.InvalidArgument, "binding.abi_mismatch", [Native.AbiVersionOf().ToString()],
				$"libpsiv_session has ABI version {Native.AbiVersionOf()}, this wrapper expects {Native.AbiVersion}"));
		}
		var native = new PsivSessionOptions();
		Native.SessionOptionsInit(ref native);
		IntPtr dataDir = Marshal.StringToCoTaskMemUTF8(options.DataDirectory);
		IntPtr coreDir = options.CoreDataDirectory is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(options.CoreDataDirectory);
		IntPtr saveDir = options.SaveDirectory is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(options.SaveDirectory);
		try
		{
			native.DataDir = dataDir;
			native.CoreDataDir = coreDir;
			native.SaveDir = saveDir;
			native.Seed = options.Seed;
			native.EncounterMode = options.EncounterMode is { } mode ? (int)mode : -1;
			native.Presentation = (int)options.Presentation;
			native.WallClock = options.WallClock ? 1 : 0;
			int status = Native.SessionCreate(in native, out IntPtr session);
			if (status != 0)
			{
				Native.LastErrorRaw(IntPtr.Zero, out PsivErrorNative error);
				if ((PsivStatus)status == PsivStatus.DataError)
				{
					throw new PsivDataException(ToError(error), DataProblems());
				}
				throw new PsivException(ToError(error));
			}
			return new GameSession(new SessionHandle(session));
		}
		finally
		{
			Marshal.FreeCoTaskMem(dataDir);
			if (coreDir != IntPtr.Zero)
			{
				Marshal.FreeCoTaskMem(coreDir);
			}
			if (saveDir != IntPtr.Zero)
			{
				Marshal.FreeCoTaskMem(saveDir);
			}
		}
	}

	private static IReadOnlyList<DataProblem> DataProblems()
	{
		Native.DataProblems(out IntPtr p, out int count);
		var list = new List<DataProblem>(count);
		foreach (PsivProblem d in Native.Span<PsivProblem>(p, count))
		{
			list.Add(new DataProblem(Native.Str(d.File), d.Line, Native.Str(d.Entry), Native.Str(d.Key), Native.Strs(d.Params, d.ParamCount), Native.Str(d.Text)));
		}
		return list;
	}

	public void Dispose()
	{
		_handle.Dispose();
		if (_callbackTarget.IsAllocated)
		{
			_callbackTarget.Free();
		}
	}

	// Errors

	private static PsivError ToError(in PsivErrorNative e) =>
		new((PsivStatus)e.Status, Native.Str(e.Key), Native.Strs(e.Params, e.ParamCount), Native.Str(e.Text));

	private PsivError LastError()
	{
		Native.LastError(_handle, out PsivErrorNative error);
		return ToError(error);
	}

	// Refused and NotFound are results; everything else throws.
	private PsivResult Result(int status)
	{
		if (status == 0)
		{
			return PsivResult.Success;
		}
		PsivError error = LastError();
		if (error.Status is PsivStatus.Refused or PsivStatus.NotFound)
		{
			return new PsivResult(error);
		}
		throw new PsivException(error);
	}

	// For reads: NotFound means "not there now" and gives false; anything else throws.
	private bool Read(int status)
	{
		if (status == 0)
		{
			return true;
		}
		PsivError error = LastError();
		if (error.Status == PsivStatus.NotFound)
		{
			return false;
		}
		throw new PsivException(error);
	}

	private void Must(int status)
	{
		if (status != 0)
		{
			throw new PsivException(LastError());
		}
	}

	private static IReadOnlyList<ActiveStatus> Statuses(IntPtr p, int count)
	{
		var list = new List<ActiveStatus>(count);
		foreach (PsivActiveStatus s in Native.Span<PsivActiveStatus>(p, count))
		{
			list.Add(new ActiveStatus(Native.Str(s.Id), Native.Str(s.Name), s.Remaining));
		}
		return list;
	}

	// Session

	public GameInfo GameInfo()
	{
		Must(Native.GameInfoGet(_handle, out PsivGameInfo i));
		return new GameInfo(Native.Str(i.Title), Native.Str(i.GameBuild), i.RulesVersion, i.TickMs, i.RoundMs, i.ManualSlots, i.AutosaveSlots, i.ItemCap,
			i.MoneyCap, Native.Strs(i.Languages, i.LanguageCount));
	}

	public Profile Profile()
	{
		Must(Native.ProfileGet(_handle, out PsivProfile p));
		return new Profile((TextSpeed)p.TextSpeed, p.AutoAdvance != 0, p.ShowShippedNames != 0, Native.Str(p.TextLanguage), p.TextSizePercent,
			(BattleSpeed)p.BattleSpeed, p.AmbientTalk != 0, p.ReducedFlash != 0, p.MusicVolume, p.AmbientVolume, p.EffectsVolume);
	}

	public PsivResult SetProfile(Profile profile)
	{
		IntPtr language = Marshal.StringToCoTaskMemUTF8(profile.TextLanguage);
		try
		{
			var p = new PsivProfile
			{
				TextSpeed = (int)profile.TextSpeed,
				AutoAdvance = profile.AutoAdvance ? 1 : 0,
				ShowShippedNames = profile.ShowShippedNames ? 1 : 0,
				TextLanguage = language,
				TextSizePercent = profile.TextSizePercent,
				BattleSpeed = (int)profile.BattleSpeed,
				AmbientTalk = profile.AmbientTalk ? 1 : 0,
				ReducedFlash = profile.ReducedFlash ? 1 : 0,
				MusicVolume = profile.MusicVolume,
				AmbientVolume = profile.AmbientVolume,
				EffectsVolume = profile.EffectsVolume,
			};
			return Result(Native.ProfileSet(_handle, in p));
		}
		finally
		{
			Marshal.FreeCoTaskMem(language);
		}
	}

	public PsivResult SetProfileValue(string key, string value) => Result(Native.ProfileSetValue(_handle, key, value));

	public Settings Settings()
	{
		Must(Native.SettingsGet(_handle, out PsivSettings s));
		return new Settings(Native.Str(s.Difficulty), (RewardMultiplier)s.RewardMultiplier, Native.Str(s.AutoMacro));
	}

	public PsivResult SetSettings(string difficulty, RewardMultiplier reward) => Result(Native.SettingsSet(_handle, difficulty, (int)reward));

	public PsivResult SetAutoMacro(string? macro) => Result(Native.SettingsSetAutoMacro(_handle, macro));

	public IReadOnlyList<string> Macros()
	{
		Must(Native.Macros(_handle, out IntPtr names, out int count));
		return Native.Strs(names, count);
	}

	// Title and saves

	public PsivResult NewGame() => Result(Native.NewGame(_handle));
	public PsivResult ContinueGame() => Result(Native.ContinueGame(_handle));
	public PsivResult Load(string slot) => Result(Native.Load(_handle, slot));
	public PsivResult RetryBattle() => Result(Native.RetryBattle(_handle));
	public PsivResult QuitToTitle() => Result(Native.QuitToTitle(_handle));
	public PsivResult SaveManual(string slot) => Result(Native.SaveManual(_handle, slot));

	// Null when saving is disabled.
	public IReadOnlyList<SaveSummary>? Saves()
	{
		int status = Native.Saves(_handle, out IntPtr p, out int count);
		if (!Result(status).Ok)
		{
			return null;
		}
		var list = new List<SaveSummary>(count);
		foreach (PsivSaveSummary s in Native.Span<PsivSaveSummary>(p, count))
		{
			list.Add(new SaveSummary(Native.Str(s.Slot), Native.Str(s.Area), s.PlayTimeMs, Native.Strs(s.Party, s.PartyCount), Native.Str(s.Difficulty),
				s.WriteTime, s.Sequence, s.Autosave != 0, s.Damaged != 0, Native.Str(s.Error)));
		}
		return list;
	}

	public Mode Mode => (Mode)Native.ModeGet(_handle);

	public MenuKind MenuKind => (MenuKind)Native.MenuKindGet(_handle);

	public WorldInfo WorldInfo()
	{
		Must(Native.WorldInfoGet(_handle, out PsivWorldInfo w));
		return new WorldInfo((Mode)w.Mode, (MenuKind)w.MenuKind, w.Money, w.PlayTimeMs, w.AutosaveCount, w.GameOver != 0, w.CanRetry != 0, w.PlayerControl != 0);
	}

	public bool Flag(string name) => Native.Flag(_handle, name) != 0;

	// Frame loop and field

	public void Update(int dtMs, Direction held = Direction.None) => Must(Native.Update(_handle, dtMs, (int)held));
	public PsivResult Walk(Direction direction) => Result(Native.Walk(_handle, (int)direction));
	public PsivResult Face(Direction direction) => Result(Native.Face(_handle, (int)direction));

	// A walk to (x, y) along the shortest path; call WalkStep until it returns anything but Walking, running
	// events and battles between the calls.
	public PsivResult WalkTo(int x, int y) => Result(Native.WalkTo(_handle, x, y));

	public WalkStatus WalkStep()
	{
		Must(Native.WalkStep(_handle, out int status));
		return (WalkStatus)status;
	}
	public PsivResult Interact() => Result(Native.Interact(_handle));
	public PsivResult PartyTalk() => Result(Native.PartyTalk(_handle));

	public FieldInfo? FieldInfo()
	{
		if (!Read(Native.FieldInfoGet(_handle, out PsivFieldInfo f)))
		{
			return null;
		}
		return new FieldInfo(Native.Str(f.AreaId), Native.Str(f.AreaName), (AreaKind)f.AreaKind, (EncounterMode)f.EncounterMode, f.Width, f.Height, f.StepMs,
			f.LeaderX, f.LeaderY, (Direction)f.Facing, f.PlayerControl != 0, f.GraceMs);
	}

	public IReadOnlyList<string> FieldRows()
	{
		return Read(Native.FieldRows(_handle, out IntPtr rows, out int count)) ? Native.Strs(rows, count) : [];
	}

	// Null outside an area.
	public FieldView? Field()
	{
		FieldInfo? info = FieldInfo();
		if (info is null)
		{
			return null;
		}
		Must(Native.FieldGrid(_handle, out IntPtr cells, out int cellCount));
		var grid = new CellFlags[cellCount];
		Native.Span<byte>(cells, cellCount).CopyTo(MemoryMarshal.AsBytes(grid.AsSpan()));

		Must(Native.FieldEnemies(_handle, out IntPtr ep, out int ec));
		var enemies = new List<FieldEnemy>(ec);
		foreach (PsivEnemy e in Native.Span<PsivEnemy>(ep, ec))
		{
			enemies.Add(new FieldEnemy(e.Id, Native.Str(e.Encounter), Native.Str(e.Zone), e.X, e.Y, (Direction)e.Facing, (EnemyState)e.State));
		}
		Must(Native.FieldMarkers(_handle, out IntPtr mp, out int mc));
		var markers = new List<Marker>(mc);
		foreach (PsivMarker m in Native.Span<PsivMarker>(mp, mc))
		{
			markers.Add(new Marker(Native.Str(m.Name), m.X, m.Y));
		}
		Must(Native.FieldExits(_handle, out IntPtr xp, out int xc));
		var exits = new List<FieldExit>(xc);
		foreach (PsivExit x in Native.Span<PsivExit>(xp, xc))
		{
			exits.Add(new FieldExit(x.X, x.Y, Native.Str(x.ToArea), Native.Str(x.ToMarker)));
		}
		Must(Native.FieldPoints(_handle, out IntPtr pp, out int pc));
		var points = new List<FieldPoint>(pc);
		foreach (PsivPoint p in Native.Span<PsivPoint>(pp, pc))
		{
			points.Add(new FieldPoint((PointKind)p.Kind, Native.Str(p.Id), p.X, p.Y, Native.Str(p.Event), p.Active != 0, p.Cost));
		}
		Must(Native.FieldActors(_handle, out IntPtr ap, out int ac));
		var actors = new List<FieldActor>(ac);
		foreach (PsivActor a in Native.Span<PsivActor>(ap, ac))
		{
			actors.Add(new FieldActor(Native.Str(a.Id), Native.Str(a.Name), a.X, a.Y, (Direction)a.Facing, a.Visible != 0, a.Scripted != 0));
		}
		return new FieldView(info, grid, enemies, markers, exits, points, actors);
	}

	// Every actor id in game.json order, wherever the actor stands. A script subject that is one of these but not in
	// FieldView.Actors is an actor in another area; any other subject is a character.
	public IReadOnlyList<string> ActorIds()
	{
		Must(Native.ActorIds(_handle, out IntPtr p, out int count));
		return Native.Strs(p, count);
	}

	// Events

	public bool LinePending => Native.LinePending(_handle) != 0;

	public Line? Line()
	{
		if (!Read(Native.LineGet(_handle, out PsivLine l)))
		{
			return null;
		}
		return new Line(Native.Str(l.SpeakerId), Native.Str(l.SpeakerName), Native.Str(l.TextId), Native.Str(l.Text), Native.Str(l.Portrait), Native.Str(l.Display));
	}

	public PsivResult ConfirmLine() => Result(Native.LineConfirm(_handle));

	public bool ChoicePending => Native.ChoicePending(_handle) != 0;

	public Choice? Choice()
	{
		if (!Read(Native.ChoiceGet(_handle, out PsivChoice c)))
		{
			return null;
		}
		return new Choice(Native.Str(c.Prompt), c.YesNo != 0, Native.Strs(c.Options, c.OptionCount));
	}

	public PsivResult Choose(int index) => Result(Native.Choose(_handle, index));

	public bool CanSkip => Native.CanSkip(_handle) != 0;

	public PsivResult Skip() => Result(Native.Skip(_handle));

	// Presentation actions

	private static PresentationAction ToAction(in PsivAction a) =>
		new(a.Handle, (ActionPhase)a.Phase, (ActionType)a.Type, Native.Str(a.Subject), Native.Str(a.Target), Native.Str(a.Slot), a.HasPosition != 0, a.X, a.Y, a.Z,
			a.Seconds, a.Speed, a.Strength, a.Loop != 0, a.Instant != 0, Native.Str(a.Description));

	public IReadOnlyList<PresentationAction> PollActions()
	{
		Must(Native.ActionsPoll(_handle, out IntPtr p, out int count));
		var list = new List<PresentationAction>(count);
		foreach (PsivAction a in Native.Span<PsivAction>(p, count))
		{
			list.Add(ToAction(a));
		}
		return list;
	}

	public void FinishAction(ulong handle) => Must(Native.ActionFinish(_handle, handle));

	// Optional push delivery as well as the queue. The callback may call FinishAction and nothing else.
	public void SetActionCallback(Action<PresentationAction>? callback)
	{
		_callback = callback;
		if (callback is null)
		{
			Must(Native.ActionsSetCallback(_handle, null, IntPtr.Zero));
			return;
		}
		if (!_callbackTarget.IsAllocated)
		{
			_callbackTarget = GCHandle.Alloc(this);
		}
		Must(Native.ActionsSetCallback(_handle, &OnAction, GCHandle.ToIntPtr(_callbackTarget)));
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static void OnAction(IntPtr user, PsivAction* action)
	{
		if (GCHandle.FromIntPtr(user).Target is GameSession session && session._callback is { } callback)
		{
			try
			{
				callback(ToAction(*action));
			}
			catch
			{
				// An exception must not unwind into native code.
			}
		}
	}

	// Notices

	public IReadOnlyList<Notice> DrainNotices()
	{
		Must(Native.NoticesDrain(_handle, out IntPtr p, out int count));
		return Notices(p, count);
	}

	// Battle

	public BattleInfo? BattleInfo()
	{
		if (!Read(Native.BattleInfoGet(_handle, out PsivBattleInfo b)))
		{
			return null;
		}
		return new BattleInfo(b.Live != 0, Native.Str(b.Encounter), (BattleSource)b.Source, b.Round, b.PartyCount, b.CombatantCount, (Outcome)b.Outcome,
			b.EscapeAllowed != 0, b.AutoActive != 0, b.PartyActsNextRound != 0, b.GameOver != 0);
	}

	public IReadOnlyList<Combatant> Combatants()
	{
		if (!Read(Native.BattleCombatants(_handle, out IntPtr p, out int count)))
		{
			return [];
		}
		var list = new List<Combatant>(count);
		foreach (PsivCombatant c in Native.Span<PsivCombatant>(p, count))
		{
			list.Add(new Combatant(c.Index, Native.Str(c.Id), Native.Str(c.Name), Native.Str(c.Label), (Side)c.Side, Native.Str(c.Group), c.Hp, c.MaxHp, c.Tp,
				c.MaxTp, c.Alive != 0, c.Defending != 0, Statuses(c.Statuses, c.StatusCount), (CommandKind)c.CommandKind, Native.Str(c.CommandId), c.CommandTarget));
		}
		return list;
	}

	public IReadOnlyList<BattleOption> BattleOptions(int actor)
	{
		Must(Native.BattleOptions(_handle, actor, out IntPtr p, out int count));
		var list = new List<BattleOption>(count);
		foreach (PsivBattleOption o in Native.Span<PsivBattleOption>(p, count))
		{
			list.Add(new BattleOption(Native.Str(o.Id), Native.Str(o.Name), (CommandKind)o.Command, o.AbilityKind < 0 ? null : (AbilityKind)o.AbilityKind,
				(Targeting)o.Targeting, o.Cost, o.Uses, (CancelReason)o.Unusable));
		}
		return list;
	}

	public PsivResult SetCommand(int actor, CommandKind kind, string? id = null, int target = -1) =>
		Result(Native.BattleSetCommand(_handle, actor, (int)kind, id ?? "", target));

	public PsivResult ApplyMacro(string macro, out IReadOnlyList<MacroStepResult> steps)
	{
		PsivResult result = Result(Native.BattleApplyMacro(_handle, macro, out IntPtr p, out int count));
		var list = new List<MacroStepResult>(count);
		foreach (PsivMacroStepResult s in Native.Span<PsivMacroStepResult>(p, count))
		{
			list.Add(new MacroStepResult(Native.Str(s.Member), s.Actor, (MacroStepStatus)s.Status, (MacroIssue)s.Issue, (CommandKind)s.CommandKind,
				Native.Str(s.CommandId), s.CommandTarget));
		}
		steps = list;
		return result;
	}

	public PsivResult StartAuto() => Result(Native.BattleStartAuto(_handle));
	public PsivResult CancelAuto() => Result(Native.BattleCancelAuto(_handle));

	public bool FillFromAuto()
	{
		Must(Native.BattleFillFromAuto(_handle, out int filled));
		return filled != 0;
	}

	public PsivResult FillFromPolicy() => Result(Native.BattleFillFromPolicy(_handle));

	// The round the way auto play fills it; a refusal says auto-battle could not start, and the policy filled the round.
	public PsivResult FillAutoRound() => Result(Native.BattleFillAutoRound(_handle));

	private static IReadOnlyList<Notice> Notices(IntPtr p, int count)
	{
		var list = new List<Notice>(count);
		foreach (PsivNotice n in Native.Span<PsivNotice>(p, count))
		{
			list.Add(new Notice((NoticeKind)n.Kind, Native.Str(n.Key), Native.Strs(n.Params, n.ParamCount), Native.Str(n.Text)));
		}
		return list;
	}

	// Plays the battle to its end with FillAutoRound and ResolveRound.
	public AutoBattle PlayBattle(int maxRounds = 60)
	{
		Result(Native.BattlePlay(_handle, maxRounds, out PsivAutoBattle a)).ThrowIfFailed();
		var rounds = new List<AutoRound>(a.RoundCount);
		foreach (PsivAutoRound r in Native.Span<PsivAutoRound>(a.Rounds, a.RoundCount))
		{
			rounds.Add(new AutoRound(Notices(r.Notices, r.NoticeCount), Events(r.Events, r.EventCount)));
		}
		string key = Native.Str(a.RefusalKey);
		PsivError? refusal = key.Length == 0 ? null : new PsivError(PsivStatus.Refused, key, Native.Strs(a.RefusalParams, a.RefusalParamCount), Native.Str(a.RefusalText));
		return new AutoBattle(refusal, rounds, a.Ended != 0);
	}

	// Null unless an ended battle is kept.
	public BattleResult? BattleResult()
	{
		if (!Read(Native.BattleResultGet(_handle, out PsivBattleResult r)))
		{
			return null;
		}
		var drops = new Dictionary<string, int>();
		foreach (PsivItemCount i in Native.Span<PsivItemCount>(r.Drops, r.DropCount))
		{
			drops[Native.Str(i.Id)] = i.Count;
		}
		var ups = new List<LevelUp>(r.LevelUpCount);
		foreach (PsivLevelUp u in Native.Span<PsivLevelUp>(r.LevelUps, r.LevelUpCount))
		{
			ups.Add(new LevelUp(Native.Str(u.Member), Native.Str(u.Name), u.Level, Native.Strs(u.Learned, u.LearnedCount)));
		}
		return new BattleResult(Native.Str(r.Encounter), (BattleSource)r.Source, (Outcome)r.Outcome, r.GameOver != 0, r.Experience, r.Money, drops, ups);
	}

	public void DismissBattle() => Must(Native.BattleDismiss(_handle));

	// The live or ended battle's record; null with neither.
	public string? BattleRecord() => Read(Native.BattleRecord(_handle, out IntPtr text)) ? Native.Str(text) : null;

	// Throws PsivException (DataError) for a record that does not parse or replay against this data.
	public Replay ReplayBattle(string record)
	{
		Must(Native.BattleReplay(_handle, record, out IntPtr p, out int count, out IntPtr text));
		return new Replay(Events(p, count), Native.Str(text));
	}

	private static IReadOnlyList<BattleEvent> Events(IntPtr p, int count)
	{
		var list = new List<BattleEvent>(count);
		foreach (PsivBattleEvent e in Native.Span<PsivBattleEvent>(p, count))
		{
			var items = new Dictionary<string, int>();
			foreach (PsivItemCount i in Native.Span<PsivItemCount>(e.Items, e.ItemCount))
			{
				items[Native.Str(i.Id)] = i.Count;
			}
			var commands = new List<RecordedCommand>(e.CommandCount);
			foreach (PsivRecordedCommand c in Native.Span<PsivRecordedCommand>(e.Commands, e.CommandCount))
			{
				commands.Add(new RecordedCommand(c.Actor, (CommandKind)c.Kind, Native.Str(c.Id), c.Target));
			}
			list.Add(new BattleEvent((BattleEventType)e.Type, e.Round, e.Actor, e.Target, e.PreviousTarget, Native.Str(e.Ability), Native.Str(e.Combo),
				Native.Str(e.Status), Native.Str(e.Message), Native.Strs(e.Params, e.ParamCount), Native.Str(e.Text), e.Amount, e.Hp, e.Duration,
				(CommandKind)e.Command, (Element)e.Element, (Effectiveness)e.Effectiveness, (Stat)e.Stat, (CancelReason)e.Cancel, (StatusEnd)e.StatusEnd,
				(Outcome)e.Outcome, (ResourceKind)e.Resource, (Opening)e.Opening, e.Succeeded != 0, e.GameOver != 0, e.Experience, e.Currency, items,
				Native.Str(e.Macro), Native.Span<int>(e.Participants, e.ParticipantCount).ToArray(), Native.Span<int>(e.Order, e.OrderCount).ToArray(),
				Native.Strs(e.Ids, e.IdCount), Native.Strs(e.Groups, e.IdCount), e.PartyCount, e.EscapeAllowed != 0, e.DefeatHasBranch != 0,
				e.VehicleBattle != 0, commands));
		}
		return list;
	}

	// The round's events. The battle ends inside this call when an outcome is reached; Mode then says where
	// control went and the rewards arrive as notices.
	public IReadOnlyList<BattleEvent> ResolveRound()
	{
		int status = Native.BattleResolveRound(_handle, out IntPtr p, out int count);
		Result(status).ThrowIfFailed();
		return Events(p, count);
	}

	public IReadOnlyList<BattleEvent> LastEvents()
	{
		Must(Native.BattleLastEvents(_handle, out IntPtr p, out int count));
		return Events(p, count);
	}

	// Party, inventory, menus and shops

	public IReadOnlyList<Member> Party()
	{
		Must(Native.Party(_handle, out IntPtr p, out int count));
		var list = new List<Member>(count);
		foreach (PsivMember m in Native.Span<PsivMember>(p, count))
		{
			var equipment = new string[5];
			var names = new string[5];
			for (int i = 0; i < 5; i++)
			{
				equipment[i] = Native.Str(m.Equipment[i]);
				names[i] = Native.Str(m.EquipmentNames[i]);
			}
			list.Add(new Member(Native.Str(m.Id), Native.Str(m.Name), m.InLineup != 0, m.Level, m.Experience, m.Hp, m.MaxHp, m.Tp, m.MaxTp, m.Strength, m.Mental,
				m.Agility, m.Dexterity, m.Defense, Statuses(m.Statuses, m.StatusCount), equipment, names));
		}
		return list;
	}

	public IReadOnlyList<AbilityInfo> MemberAbilities(string member)
	{
		if (!Read(Native.MemberAbilities(_handle, member, out IntPtr p, out int count)))
		{
			return [];
		}
		var list = new List<AbilityInfo>(count);
		foreach (PsivAbilityInfo a in Native.Span<PsivAbilityInfo>(p, count))
		{
			list.Add(new AbilityInfo(Native.Str(a.Id), Native.Str(a.Name), (AbilityKind)a.Kind, (Targeting)a.Targeting, a.Cost, a.Field != 0));
		}
		return list;
	}

	private static InventoryEntry ToEntry(in PsivInventoryEntry e) =>
		new(Native.Str(e.Id), Native.Str(e.Name), e.Count, e.Equipment != 0, e.Field != 0, e.TargetingOrSlot, e.Price);

	public IReadOnlyList<InventoryEntry> Inventory()
	{
		Must(Native.Inventory(_handle, out IntPtr p, out int count));
		var list = new List<InventoryEntry>(count);
		foreach (PsivInventoryEntry e in Native.Span<PsivInventoryEntry>(p, count))
		{
			list.Add(ToEntry(e));
		}
		return list;
	}

	public ShopInfo? Shop()
	{
		if (!Read(Native.ShopGet(_handle, out PsivShopInfo s)))
		{
			return null;
		}
		var stock = new List<InventoryEntry>(s.StockCount);
		foreach (PsivInventoryEntry e in Native.Span<PsivInventoryEntry>(s.Stock, s.StockCount))
		{
			stock.Add(ToEntry(e));
		}
		return new ShopInfo(Native.Str(s.Id), s.SellPercent, stock);
	}

	public InnInfo? Inn() => Read(Native.InnGet(_handle, out PsivInnInfo i)) ? new InnInfo(Native.Str(i.Id), i.Cost) : null;

	public PsivResult OpenMenu() => Result(Native.OpenMenu(_handle));
	public PsivResult CloseMenu() => Result(Native.CloseMenu(_handle));
	public PsivResult Buy(string id, int count = 1) => Result(Native.Buy(_handle, id, count));
	public PsivResult Sell(string id, int count = 1) => Result(Native.Sell(_handle, id, count));
	public PsivResult Rest() => Result(Native.Rest(_handle));
	public PsivResult UseItem(string item, string? member = null) => Result(Native.UseItem(_handle, item, member));
	public PsivResult UseAbility(string user, string ability, string? member = null) => Result(Native.UseAbility(_handle, user, ability, member));
	public PsivResult Equip(string member, string equipment) => Result(Native.Equip(_handle, member, equipment));
	public PsivResult Unequip(string member, EquipSlot slot) => Result(Native.Unequip(_handle, member, (int)slot));
	public string NameEntryMember => Native.Str(Native.NameEntryMember(_handle));
	public PsivResult EnterName(string name) => Result(Native.EnterName(_handle, name));

	// Journal, text log and text

	public QuestLog QuestLog()
	{
		Must(Native.QuestLogGet(_handle, out PsivQuestLog q));
		var jobs = new List<Job>(q.JobCount);
		foreach (PsivJob j in Native.Span<PsivJob>(q.Jobs, q.JobCount))
		{
			jobs.Add(new Job(Native.Str(j.Id), Native.Str(j.Title), (JobState)j.State));
		}
		return new QuestLog(Native.Str(q.Objective), jobs);
	}

	public IReadOnlyList<CodexEntry> Encyclopedia()
	{
		Must(Native.Encyclopedia(_handle, out IntPtr p, out int count));
		var list = new List<CodexEntry>(count);
		foreach (PsivCodexEntry e in Native.Span<PsivCodexEntry>(p, count))
		{
			list.Add(new CodexEntry(Native.Str(e.Id), Native.Str(e.Title), Native.Str(e.Text)));
		}
		return list;
	}

	public IReadOnlyList<GlossaryEntry> Glossary()
	{
		Must(Native.Glossary(_handle, out IntPtr p, out int count));
		var list = new List<GlossaryEntry>(count);
		foreach (PsivGlossaryEntry e in Native.Span<PsivGlossaryEntry>(p, count))
		{
			list.Add(new GlossaryEntry(Native.Str(e.Id), Native.Str(e.Restored), Native.Str(e.Shipped)));
		}
		return list;
	}

	public IReadOnlyList<string> TextLog()
	{
		Must(Native.TextLog(_handle, out IntPtr p, out int count));
		return Native.Strs(p, count);
	}

	public string PacingTable()
	{
		Must(Native.PacingTable(_handle, out IntPtr text));
		return Native.Str(text);
	}

	public IReadOnlyList<PacingArea> PacingAreas()
	{
		Must(Native.PacingAreas(_handle, out IntPtr p, out int count));
		var list = new List<PacingArea>(count);
		foreach (PsivPacingArea a in Native.Span<PsivPacingArea>(p, count))
		{
			list.Add(new PacingArea(Native.Str(a.Area), a.Visits, a.Battles, a.Steps, a.FieldMs, a.BattleMs, a.EventMs, a.Experience, a.Money,
				Native.Span<long>(a.GapMs, a.GapCount).ToArray(), Native.Span<int>(a.GapSteps, a.GapCount).ToArray()));
		}
		return list;
	}

	public IReadOnlyList<PacingBoss> PacingBosses()
	{
		Must(Native.PacingBosses(_handle, out IntPtr p, out int count));
		var list = new List<PacingBoss>(count);
		foreach (PsivPacingBoss b in Native.Span<PsivPacingBoss>(p, count))
		{
			list.Add(new PacingBoss(Native.Str(b.Flag), Native.Str(b.Encounter), Native.Str(b.Area), b.AverageLevelX100, b.DungeonMs));
		}
		return list;
	}

	private string WithParams(string key, IReadOnlyList<string> parameters, bool raw)
	{
		var pointers = new IntPtr[parameters.Count];
		try
		{
			for (int i = 0; i < pointers.Length; i++)
			{
				pointers[i] = Marshal.StringToCoTaskMemUTF8(parameters[i]);
			}
			fixed (IntPtr* p = pointers)
			{
				IntPtr text;
				Must(raw ? Native.FormatText(_handle, key, p, pointers.Length, out text) : Native.Render(_handle, key, p, pointers.Length, out text));
				return Native.Str(text);
			}
		}
		finally
		{
			foreach (IntPtr p in pointers)
			{
				Marshal.FreeCoTaskMem(p);
			}
		}
	}

	public string Render(string key, params string[] parameters) => WithParams(key, parameters, raw: false);

	public string FormatText(string key, params string[] parameters) => WithParams(key, parameters, raw: true);

	public string DisplayName(string id)
	{
		Must(Native.DisplayName(_handle, id, out IntPtr text));
		return Native.Str(text);
	}

	public string StringText(string id)
	{
		Must(Native.StringText(_handle, id, out IntPtr text));
		return Native.Str(text);
	}
}
