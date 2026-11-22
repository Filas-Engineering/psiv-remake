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

	// What this build supports, as JSON (bindings/README.md, "Capabilities").
	public static string Capabilities => Native.Str(Native.Capabilities());

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
			i.MoneyCap, Native.Strs(i.Languages, i.LanguageCount), Native.Str(i.TitleMusic), Native.Str(i.VictoryMusic), Native.Str(i.DefeatMusic),
			i.MagicDefense != 0, i.AffinityDivisor, (FlashLevel)i.FlashDefault, i.FlashOriginal != 0, i.PartyTalkAmbient != 0, i.MusicFadeMs, i.CellPixels,
			i.Autosave != 0, i.BagSlots, i.BagSingle != 0);
	}

	public Profile Profile()
	{
		Must(Native.ProfileGet(_handle, out PsivProfile p));
		return new Profile((TextSpeed)p.TextSpeed, p.AutoAdvance != 0, p.ShowShippedNames != 0, Native.Str(p.TextLanguage), p.TextSizePercent,
			(BattleSpeed)p.BattleSpeed, p.AmbientTalk != 0, p.ReducedFlash != 0, p.MusicVolume, p.AmbientVolume, p.EffectsVolume, p.AlwaysRun != 0,
			(FlashLevel)p.Flashing, p.FlashNoticeSeen != 0);
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
				AlwaysRun = profile.AlwaysRun ? 1 : 0,
				Flashing = (int)profile.Flashing,
				FlashNoticeSeen = profile.FlashNoticeSeen ? 1 : 0,
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

	public Difficulties Difficulties()
	{
		Must(Native.Difficulties(_handle, out IntPtr p, out int count, out int canChange));
		var list = new List<Difficulty>(count);
		foreach (PsivDifficulty d in Native.Span<PsivDifficulty>(p, count))
		{
			list.Add(new Difficulty(Native.Str(d.Id), Native.Str(d.Name), Native.Str(d.Description), d.RewardPercent));
		}
		return new Difficulties(list, canChange != 0);
	}

	public PsivResult SetAutoMacro(string? macro) => Result(Native.SettingsSetAutoMacro(_handle, macro));

	public IReadOnlyList<string> Macros()
	{
		Must(Native.Macros(_handle, out IntPtr names, out int count));
		return Native.Strs(names, count);
	}

	// Macro editor: reads any time, changes from the field menu.

	public IReadOnlyList<Macro> MacroList()
	{
		Must(Native.MacroList(_handle, out IntPtr p, out int count));
		var list = new List<Macro>(count);
		foreach (PsivMacro m in Native.Span<PsivMacro>(p, count))
		{
			var steps = new List<MacroStep>(m.StepCount);
			foreach (PsivMacroStep s in Native.Span<PsivMacroStep>(m.Steps, m.StepCount))
			{
				steps.Add(new MacroStep(Native.Str(s.Member), Native.Str(s.MemberName), (CommandKind)s.CommandKind, Native.Str(s.Id), Native.Str(s.IdName),
					(MacroTargetKind)s.TargetKind, Native.Str(s.TargetMember), Native.Str(s.TargetMemberName), s.TargetEnemy, (MacroIssue)s.Issue, s.Piece != 0));
			}
			list.Add(new Macro(Native.Str(m.Name), steps, (MacroTrouble)m.OnTrouble, (MacroRecoverWho)m.RecoverWho, (MacroUnnamed)m.Unnamed, (MacroPolicy)m.Policy));
		}
		return list;
	}

	public PsivResult CreateMacro(string name) => Result(Native.MacroCreate(_handle, name));
	// The macro's on_trouble, recover_who and unnamed; Default leaves each to the caller and rules.json. Saved with the macro.
	public PsivResult SetMacroOptions(string macro, MacroTrouble onTrouble, MacroRecoverWho recoverWho, MacroUnnamed unnamed) =>
		Result(Native.MacroSetOptions(_handle, macro, (int)onTrouble, (int)recoverWho, (int)unnamed));
	// The macro's policy for its automatic fallbacks and recover rounds; Default leaves it to rules.json. Saved with the macro.
	public PsivResult SetMacroPolicy(string macro, MacroPolicy policy) => Result(Native.MacroSetPolicy(_handle, macro, (int)policy));
	public PsivResult RenameMacro(string name, string newName) => Result(Native.MacroRename(_handle, name, newName));
	public PsivResult DeleteMacro(string name) => Result(Native.MacroDelete(_handle, name));
	public PsivResult MoveMacro(string name, int position) => Result(Native.MacroMove(_handle, name, position));

	// Replaces step `index`, or appends when `index` is the step count.
	public PsivResult SetMacroStep(string macro, int index, string member, CommandKind kind, string? id = null, MacroTargetKind target = MacroTargetKind.Default,
		string? targetMember = null, int targetEnemy = -1) =>
		Result(Native.MacroSetStep(_handle, macro, index, member, (int)kind, id, (int)target, targetMember, targetEnemy));

	// An Item step naming an equipment piece with a battle use, which the member casts worn or from the bag.
	public PsivResult SetMacroPieceStep(string macro, int index, string member, string piece, MacroTargetKind target = MacroTargetKind.Default,
		string? targetMember = null, int targetEnemy = -1) =>
		Result(Native.MacroSetPieceStep(_handle, macro, index, member, piece, (int)target, targetMember, targetEnemy));

	public PsivResult RemoveMacroStep(string macro, int index) => Result(Native.MacroRemoveStep(_handle, macro, index));
	public PsivResult MoveMacroStep(string macro, int from, int to) => Result(Native.MacroMoveStep(_handle, macro, from, to));

	// A new macro from a discovered combination (a ComboEntry's Recipe): its abilities in the recipe's order, each given
	// to a lineup member who knows it now.
	public PsivResult MacroFromCombo(string recipe, string name) => Result(Native.MacroFromCombo(_handle, recipe, name));

	// Title and saves

	public PsivResult NewGame() => Result(Native.NewGame(_handle));
	public PsivResult ContinueGame() => Result(Native.ContinueGame(_handle));
	public PsivResult Load(string slot) => Result(Native.Load(_handle, slot));
	public PsivResult RetryBattle() => Result(Native.RetryBattle(_handle));
	public PsivResult QuitToTitle() => Result(Native.QuitToTitle(_handle));
	public PsivResult SaveManual(string slot) => Result(Native.SaveManual(_handle, slot));
	// Deletes a slot's file (manual or autosave, damaged included) in any mode; refused like Load for a slot it cannot read.
	public PsivResult EraseSave(string slot) => Result(Native.EraseSave(_handle, slot));

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
				s.WriteTime, s.Sequence, s.Autosave != 0, s.Damaged != 0, Native.Str(s.Error), s.Dev != 0));
		}
		return list;
	}

	public Mode Mode => (Mode)Native.ModeGet(_handle);

	public MenuKind MenuKind => (MenuKind)Native.MenuKindGet(_handle);

	public WorldInfo WorldInfo()
	{
		Must(Native.WorldInfoGet(_handle, out PsivWorldInfo w));
		return new WorldInfo((Mode)w.Mode, (MenuKind)w.MenuKind, w.Money, w.PlayTimeMs, w.AutosaveCount, w.GameOver != 0, w.CanRetry != 0, w.PlayerControl != 0,
			w.Dev != 0, w.Encounters != 0, w.BagUsed);
	}

	public bool Flag(string name) => Native.Flag(_handle, name) != 0;

	// Frame loop and field

	public void Update(int dtMs, Direction held = Direction.None) => Must(Native.Update(_handle, dtMs, (int)held));
	// With the run input: the leader runs on foot while it is held (walks with the profile's AlwaysRun) when game.json
	// gives run_speed_percent.
	public void Update(int dtMs, Direction held, bool running) => Must(Native.UpdateInput(_handle, dtMs, new PsivFieldInput { Direction = (int)held, Running = running ? 1 : 0 }));
	// Whether a step that way would move the leader now (player control, no belt, a cell it can enter, no enemy on it).
	public bool CanStep(Direction direction) => Native.CanStep(_handle, (int)direction) != 0;
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

	// What Interact would act on now, for a prompt; null when nothing answers, outside the field, without control or aboard.
	public InteractTarget? InteractTarget()
	{
		if (!Read(Native.InteractTargetGet(_handle, out PsivInteractTarget t)))
		{
			return null;
		}
		return Target(t);
	}

	// What stands on (x, y) and whether the leader can use it from where it stands, with the facing to turn to; null when
	// nothing there answers from here.
	public InteractTarget? InteractAt(int x, int y) => Read(Native.InteractAt(_handle, x, y, out PsivInteractTarget t)) ? Target(t) : null;

	static InteractTarget Target(PsivInteractTarget t) =>
		new((InteractKind)t.Kind, Native.Str(t.Id), t.X, t.Y, (Direction)t.Facing, t.Ready != 0, t.Across != 0, Native.Str(t.Keeper));
	public PsivResult PartyTalk() => Result(Native.PartyTalk(_handle));
	// Boards the party's vehicle in the faced cell (Interact does the same); LeaveVehicle steps off onto dry ground.
	public PsivResult Board() => Result(Native.Board(_handle));
	public PsivResult LeaveVehicle() => Result(Native.LeaveVehicle(_handle));

	public IReadOnlyList<FieldVehicle> FieldVehicles()
	{
		if (!Read(Native.FieldVehicles(_handle, out IntPtr p, out int count)))
		{
			return [];
		}
		var list = new List<FieldVehicle>(count);
		foreach (PsivVehicle v in Native.Span<PsivVehicle>(p, count))
		{
			list.Add(new FieldVehicle(Native.Str(v.Id), Native.Str(v.Name), v.X, v.Y, (Direction)v.Facing, v.Aboard != 0));
		}
		return list;
	}

	public IReadOnlyList<TravelDestination> TravelDestinations(TravelKind kind)
	{
		Must(Native.TravelDestinations(_handle, (int)kind, out IntPtr p, out int count));
		var list = new List<TravelDestination>(count);
		foreach (PsivTravelDestination d in Native.Span<PsivTravelDestination>(p, count))
		{
			list.Add(new TravelDestination(Native.Str(d.Id), Native.Str(d.Area), Native.Str(d.AreaName), Native.Str(d.Region), d.X, d.Y));
		}
		return list;
	}

	// Port: from a port's menu. Town and DungeonExit: from the field menu through `thing`, an item (user null) or a technique
	// `user` knows; DungeonExit takes a null destination.
	public PsivResult Travel(TravelKind kind, string? destination, string? user = null, string? thing = null) =>
		Result(Native.Travel(_handle, (int)kind, destination, user, thing));

	public FieldInfo? FieldInfo()
	{
		if (!Read(Native.FieldInfoGet(_handle, out PsivFieldInfo f)))
		{
			return null;
		}
		return new FieldInfo(Native.Str(f.AreaId), Native.Str(f.AreaName), (AreaKind)f.AreaKind, (EncounterMode)f.EncounterMode, f.Width, f.Height, f.StepMs,
			f.LeaderX, f.LeaderY, (Direction)f.Facing, f.PlayerControl != 0, f.GraceMs, Native.Str(f.Music), Native.Str(f.Region), Native.Str(f.Aboard), f.LeaderStepMs,
			Native.Str(f.ShipPort), f.RunStepMs, f.StepProgressMs, f.ConveyorStepMs, f.ConveyorProgressMs, f.StepMask,
			f.AnnounceArrival != 0, f.LeaderStepUs, f.RunStepUs, f.StepProgressUs, f.ConveyorStepUs, f.ConveyorProgressUs, Native.Str(f.ExitSound), f.NextTickUs,
			f.StepLandsInUs, f.RunStepLandsInUs, Native.Str(f.RidingPlatform), Native.Str(f.AreaNameKey));
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
			enemies.Add(new FieldEnemy(e.Id, Native.Str(e.Encounter), Native.Str(e.Zone), e.X, e.Y, (Direction)e.Facing, (EnemyState)e.State, Native.Str(e.Actor)));
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
			exits.Add(new FieldExit(x.X, x.Y, Native.Str(x.ToArea), Native.Str(x.ToMarker), x.Open != 0, (char)x.ClosedTile, Native.Str(x.Sound), (ExitFires)x.Fires));
		}
		Must(Native.FieldPoints(_handle, out IntPtr pp, out int pc));
		var points = new List<FieldPoint>(pc);
		foreach (PsivPoint p in Native.Span<PsivPoint>(pp, pc))
		{
			points.Add(new FieldPoint((PointKind)p.Kind, Native.Str(p.Id), p.X, p.Y, Native.Str(p.Event), p.Active != 0, p.Cost, (ShopKind)p.ShopKind, (Direction)p.Direction,
				Native.Str(p.Keeper)));
		}
		Must(Native.FieldActors(_handle, out IntPtr ap, out int ac));
		var actors = new List<FieldActor>(ac);
		foreach (PsivActor a in Native.Span<PsivActor>(ap, ac))
		{
			actors.Add(new FieldActor(Native.Str(a.Id), Native.Str(a.Name), a.X, a.Y, (Direction)a.Facing, a.Visible != 0, a.Scripted != 0, a.Solid != 0,
				(ActorVanish)a.Vanish, Native.Str(a.Animation), a.AnimationMs, a.AnimationTotalMs, (ActorRemoval)a.Removal, Native.Str(a.SpriteEnemy), Native.Str(a.Look),
				a.Wanders != 0, a.Patrols != 0, a.OnPath != 0, a.HomeX, a.HomeY, a.PathX, a.PathY, a.FootprintW, a.FootprintH));
		}
		return new FieldView(info, grid, enemies, markers, exits, points, actors, FieldVehicles());
	}

	// Every actor id in game.json order, wherever the actor stands. A script subject that is one of these but not in
	// FieldView.Actors is an actor in another area; any other subject is a character.
	public IReadOnlyList<string> ActorIds()
	{
		Must(Native.ActorIds(_handle, out IntPtr p, out int count));
		return Native.Strs(p, count);
	}

	// Every animation scripts may name with its length and what it leaves behind: game.json's, then the standard ones.
	public IReadOnlyList<AnimationInfo> Animations()
	{
		Must(Native.Animations(_handle, out IntPtr p, out int count));
		var list = new List<AnimationInfo>(count);
		foreach (PsivAnimation a in Native.Span<PsivAnimation>(p, count))
		{
			list.Add(new AnimationInfo(Native.Str(a.Id), a.Seconds, a.Standard != 0, (AnimationEnd)a.End));
		}
		return list;
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
	// What the running event shows next without advancing it: a line, a choice, its end, or something else.
	public NextOutput NextEventOutput => (NextOutput)Native.EventNextOutput(_handle);

	public PsivResult Skip() => Result(Native.Skip(_handle));

	// Presentation actions

	private static PresentationAction ToAction(in PsivAction a)
	{
		var route = new List<RouteCell>(a.RouteCount);
		foreach (PsivRouteCell c in Native.Span<PsivRouteCell>(a.Route, a.RouteCount))
		{
			route.Add(new RouteCell(c.X, c.Y));
		}
		return new(a.Handle, (ActionPhase)a.Phase, (ActionType)a.Type, Native.Str(a.Subject), Native.Str(a.Target), Native.Str(a.Slot), a.HasPosition != 0, a.X, a.Y, a.Z,
			a.Seconds, a.Speed, a.Strength, a.Loop != 0, a.Instant != 0, Native.Str(a.Description), a.Duration, new ValueList<string>(Native.Strs(a.Waypoints, a.WaypointCount)),
			a.Pathfind != 0, Native.Str(a.Facing), new ValueList<RouteCell>(route), a.FadeMs);
	}

	public IReadOnlyList<Camera> Cameras()
	{
		Must(Native.Cameras(_handle, out IntPtr p, out int count));
		var list = new List<Camera>(count);
		foreach (PsivCamera c in Native.Span<PsivCamera>(p, count))
		{
			list.Add(new Camera(Native.Str(c.Id), (CameraTarget)c.Target, Native.Str(c.Area), Native.Str(c.Marker), Native.Str(c.Follow), c.Zoom));
		}
		return list;
	}

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
			b.EscapeAllowed != 0, b.AutoActive != 0, b.PartyActsNextRound != 0, b.GameOver != 0, Native.Str(b.Music), Native.Str(b.Vehicle), b.FormationSize);
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
				c.MaxTp, c.Alive != 0, c.Defending != 0, Statuses(c.Statuses, c.StatusCount), (CommandKind)c.CommandKind, Native.Str(c.CommandId), c.CommandTarget,
				c.Flying != 0, c.Position, c.Level));
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
				(Targeting)o.Targeting, o.Cost, o.Uses, (CancelReason)o.Unusable, o.MaxUses));
		}
		return list;
	}

	public PsivResult SetCommand(int actor, CommandKind kind, string? id = null, int target = -1) =>
		Result(Native.BattleSetCommand(_handle, actor, (int)kind, id ?? "", target));

	public PsivResult ApplyMacro(string macro, out IReadOnlyList<MacroStepResult> steps, MacroFallback fallback = MacroFallback.BasicAttack)
	{
		PsivResult result = Result(Native.BattleApplyMacroFallback(_handle, macro, (int)fallback, out IntPtr p, out int count));
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

	// The macro FillAutoRound and PlayBattle fill every round with, each member whose step cannot run taking core's
	// automatic choice; null or "" plays as before. Not saved.
	public PsivResult SetAutoPlayMacro(string? macro) => Result(Native.BattleSetAutoPlayMacro(_handle, macro ?? ""));

	// As SetAutoPlayMacro with a trouble mode that wins over the macro's and rules.json; with null or "" it applies to the
	// save's auto macro.
	public PsivResult SetAutoPlayMacro(string? macro, MacroTrouble trouble) => Result(Native.BattleSetAutoPlayMacroTrouble(_handle, macro ?? "", (int)trouble));
	// And with a policy for those battles' automatic choices, over the macro's and rules.json's.
	public PsivResult SetAutoPlayMacro(string? macro, MacroTrouble trouble, MacroPolicy policy) =>
		Result(Native.BattleSetAutoPlayMacroPolicy(_handle, macro ?? "", (int)trouble, (int)policy));

	// Each step's state this round, for marking a macro before the player confirms it.
	public PsivResult CheckMacro(string macro, out IReadOnlyList<MacroStepCheck> steps)
	{
		PsivResult result = Result(Native.BattleMacroCheck(_handle, macro, out IntPtr p, out int count));
		var list = new List<MacroStepCheck>(count);
		foreach (PsivMacroStepCheck c in Native.Span<PsivMacroStepCheck>(p, count))
		{
			list.Add(new MacroStepCheck(Native.Str(c.Member), c.Actor, (MacroIssue)c.Issue, Native.Str(c.Status), Native.Str(c.RefusalKey), Native.Str(c.RefusalText), c.Ok != 0));
		}
		steps = list;
		return result;
	}

	// With a macro name, the trouble a round of it would meet now; with null, the round FillAutoRound filled last.
	public PsivResult TroubleReport(string? macro, out MacroTroubleReport report)
	{
		PsivResult result = Result(Native.BattleMacroTrouble(_handle, macro, out PsivMacroTroubleReport r));
		report = Trouble(r);
		return result;
	}

	private static MacroTroubleReport Trouble(PsivMacroTroubleReport r) => new(r.Holds != 0, (MacroTrouble)r.Mode,
		Native.Span<int>(r.Fallen, r.FallenCount).ToArray(), Native.Span<int>(r.TroubledSteps, r.TroubledStepCount).ToArray(),
		Native.Span<int>(r.TroubledMembers, r.TroubledMemberCount).ToArray(), Native.Span<int>(r.Recovered, r.RecoveredCount).ToArray());

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
			rounds.Add(new AutoRound(Notices(r.Notices, r.NoticeCount), Events(r.Events, r.EventCount), Trouble(r.Trouble)));
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
				e.VehicleBattle != 0, commands, e.Rolled));
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
			var equipment = new string[Native.SlotCount];
			var names = new string[Native.SlotCount];
			for (int i = 0; i < Native.SlotCount; i++)
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
			list.Add(new AbilityInfo(Native.Str(a.Id), Native.Str(a.Name), (AbilityKind)a.Kind, (Targeting)a.Targeting, a.Cost, a.Field != 0, a.Uses, a.MaxUses,
				a.Travel < 0 ? null : (TravelKind)a.Travel));
		}
		return list;
	}

	// Swaps two lineup positions, from the field menu.
	public PsivResult SwapLineup(int a, int b) => Result(Native.PartySwap(_handle, a, b));

	public MemberDetail? MemberDetail(string member)
	{
		if (!Read(Native.MemberDetailGet(_handle, member, out PsivMemberDetail d)))
		{
			return null;
		}
		var bonuses = new StatBonus[Native.SlotCount];
		for (int i = 0; i < Native.SlotCount; i++)
		{
			PsivStatBonus b = d.EquipmentBonus[i];
			bonuses[i] = ToBonus(b);
		}
		var affinities = new Dictionary<Element, int>();
		var affinityList = new List<Affinity>(d.AffinityCount);
		foreach (PsivAffinity a in Native.Span<PsivAffinity>(d.Affinities, d.AffinityCount))
		{
			affinities[(Element)a.Element] = a.Percent;
			affinityList.Add(new Affinity((Element)a.Element, Native.Str(a.Id), Native.Str(a.Name), a.Percent));
		}
		var resists = new List<StatusResist>(d.StatusResistCount);
		foreach (PsivStatusResist r in Native.Span<PsivStatusResist>(d.StatusResists, d.StatusResistCount))
		{
			resists.Add(new StatusResist(Native.Str(r.Id), Native.Str(r.Name), r.Percent));
		}
		var profile = new List<ProfileRow>(d.ProfileCount);
		foreach (PsivProfileRow r in Native.Span<PsivProfileRow>(d.Profile, d.ProfileCount))
		{
			profile.Add(new ProfileRow(Native.Str(r.Key), Native.Str(r.KeyText), r.KeyNumber != 0, Native.Str(r.Value), Native.Str(r.ValueText), r.ValueNumber != 0));
		}
		return new MemberDetail(Native.Str(d.Id), d.ExperienceToNext, d.Critical, d.Evasion, (Element)d.AttackElement, bonuses, affinities, resists, d.MagicDefense, affinityList,
			profile, d.Attack, ToBonus(d.BaseStats), d.ExperienceForLevel, d.TotalExperience, (CreatureKind)d.CreatureKind, d.Vulnerable != 0);
	}

	private static StatBonus ToBonus(in PsivStatBonus b) => new(b.MaxHp, b.MaxTp, b.Strength, b.Mental, b.Agility, b.Dexterity, b.Defense, b.Critical, b.Evasion, b.MagicDefense);

	private static IReadOnlyList<StatusResist> Resists(IntPtr p, int count)
	{
		var list = new List<StatusResist>(count);
		foreach (PsivStatusResist r in Native.Span<PsivStatusResist>(p, count))
		{
			list.Add(new StatusResist(Native.Str(r.Id), Native.Str(r.Name), r.Percent));
		}
		return list;
	}

	private static CombatView ToCombat(in PsivCombatView v) =>
		new(ToBonus(v.Stats), Native.Str(v.BasicAttack), Native.Str(v.BasicAttackName), (Element)v.AttackElement, Native.Strs(v.Abilities, v.AbilityCount),
			Native.Strs(v.AbilityNames, v.AbilityCount), Resists(v.StatusResists, v.StatusResistCount));

	// Any equipment piece, held or not, where Equip puts it, or at a position (a hand for weapons and shields); null for a
	// character not in the roster or an id that is not equipment.
	public EquipPreview? EquipPreview(string member, string piece, EquipSlot? position = null)
	{
		if (!Read(position is { } at ? Native.EquipPreviewAt(_handle, member, piece, (int)at, out PsivEquipPreview p) : Native.EquipPreviewGet(_handle, member, piece, out p)))
		{
			return null;
		}
		string key = Native.Str(p.RefusalKey);
		PsivError? refusal = key.Length == 0 ? null : new PsivError(PsivStatus.Refused, key, [], Native.Str(p.RefusalText));
		return new EquipPreview(Native.Str(p.Member), Native.Str(p.Piece), Native.Str(p.PieceName), (EquipSlot)p.Slot, Native.Str(p.Category), ToBonus(p.Bonus),
			(Element)p.Element, Native.Str(p.Attack), Native.Str(p.AttackName), Native.Strs(p.Granted, p.GrantedCount), Native.Strs(p.GrantedNames, p.GrantedCount),
			Resists(p.PieceResists, p.PieceResistCount), Native.Str(p.Replaces), Native.Str(p.ReplacesName), p.CanWear != 0, refusal,
			Native.Strs(p.Wearers, p.WearerCount), Native.Strs(p.WearerNames, p.WearerCount), ToCombat(p.Before), ToCombat(p.After), (EquipSlot)p.Position,
			Native.Str(p.AlsoReplaces), Native.Str(p.AlsoReplacesName), p.Hands);
	}

	public ComboLog ComboLog()
	{
		Must(Native.ComboLog(_handle, out IntPtr p, out int count, out int total));
		var entries = new List<ComboEntry>(count);
		foreach (PsivComboEntry e in Native.Span<PsivComboEntry>(p, count))
		{
			entries.Add(new ComboEntry(Native.Str(e.Recipe), Native.Str(e.Result), Native.Str(e.ResultName), e.PlayTimeMs, Native.Strs(e.Participants, e.ParticipantCount),
				Native.Strs(e.ParticipantNames, e.ParticipantCount), Native.Strs(e.Abilities, e.AbilityCount), Native.Strs(e.AbilityNames, e.AbilityCount)));
		}
		return new ComboLog(entries, total);
	}

	private static InventoryEntry ToEntry(in PsivInventoryEntry e) =>
		new(Native.Str(e.Id), Native.Str(e.Name), e.Count, e.Equipment != 0, e.Field != 0, e.TargetingOrSlot, e.Price, e.Travel < 0 ? null : (TravelKind)e.Travel,
			Native.Str(e.CallsVehicle), e.Hands);

	// Items in id order, then unworn pieces in id order; another ItemSort lists them as one sorted list.
	public IReadOnlyList<InventoryEntry> Inventory(ItemSort order = ItemSort.Id)
	{
		Must(Native.InventorySorted(_handle, (int)order, out IntPtr p, out int count));
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
		return new ShopInfo(Native.Str(s.Id), s.SellPercent, stock, (ShopKind)s.Kind, Native.Str(s.Voice), Native.Str(s.NameKey), Native.Str(s.Name));
	}

	public InnInfo? Inn() => Read(Native.InnGet(_handle, out PsivInnInfo i)) ? new InnInfo(Native.Str(i.Id), i.Cost, Native.Str(i.Voice), i.PerMember != 0, Native.Str(i.NameKey), Native.Str(i.Name)) : null;

	public PsivResult OpenMenu() => Result(Native.OpenMenu(_handle));
	public PsivResult CloseMenu() => Result(Native.CloseMenu(_handle));
	public PsivResult Buy(string id, int count = 1) => Result(Native.Buy(_handle, id, count));
	public PsivResult Sell(string id, int count = 1) => Result(Native.Sell(_handle, id, count));
	public PsivResult Rest() => Result(Native.Rest(_handle));
	public PsivResult UseItem(string item, string? member = null) => Result(Native.UseItem(_handle, item, member));
	public PsivResult UseAbility(string user, string ability, string? member = null) => Result(Native.UseAbility(_handle, user, ability, member));
	public PsivResult Equip(string member, string equipment) => Result(Native.Equip(_handle, member, equipment));
	public PsivResult EquipAt(string member, string equipment, EquipSlot position) => Result(Native.EquipAt(_handle, member, equipment, (int)position));
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

	// "" when the profile's language has no description for it; null when the battle data has no such id of that kind.
	public string? Description(DescriptionKind kind, string id) => Read(Native.Description(_handle, (int)kind, id, out IntPtr text)) ? Native.Str(text) : null;

	public string StringText(string id)
	{
		Must(Native.StringText(_handle, id, out IntPtr text));
		return Native.Str(text);
	}

	// Developer tools (docs/dev-tools.md). A command that changes the game marks it (WorldInfo.Dev, SaveSummary.Dev); refusals
	// are game.refuse.no_game and game.refuse.dev_* results.

	public IReadOnlyList<string> DevIds(DevList list)
	{
		Must(Native.DevIds(_handle, (int)list, out IntPtr p, out int count));
		return Native.Strs(p, count);
	}

	public PsivResult DevWarp(string area, string marker) => Result(Native.DevWarp(_handle, area, marker));
	public PsivResult DevWarpCell(string area, int x, int y) => Result(Native.DevWarpCell(_handle, area, x, y));
	public PsivResult DevSetFlag(string flag, bool value) => Result(Native.DevSetFlag(_handle, flag, value ? 1 : 0));
	public PsivResult DevGive(string id, int count) => Result(Native.DevGive(_handle, id, count));
	public PsivResult DevMoney(int amount) => Result(Native.DevMoney(_handle, amount));
	// Restores the roster and the vehicles' skill uses as a rest does, free.
	public PsivResult DevRest() => Result(Native.DevRest(_handle));
	public PsivResult DevSetLevel(string member, int level) => Result(Native.DevSetLevel(_handle, member, level));
	public PsivResult DevBattle(string encounter) => Result(Native.DevBattle(_handle, encounter));
	public PsivResult DevVehicle(string vehicle, string area, string marker) => Result(Native.DevVehicle(_handle, vehicle, area, marker));
	public PsivResult DevEncounters(bool on) => Result(Native.DevEncounters(_handle, on ? 1 : 0));
	public PsivResult DevRunEvent(string ev) => Result(Native.DevRunEvent(_handle, ev));
	public void DevMark() => Must(Native.DevMark(_handle));

	// The round that ended the battle, or the refusal (no battle) with no events.
	public (PsivResult Result, IReadOnlyList<BattleEvent> Events) DevEndBattle(bool victory)
	{
		PsivResult result = Result(Native.DevEndBattle(_handle, victory ? 1 : 0, out IntPtr p, out int count));
		return (result, result.Ok ? Events(p, count) : []);
	}

	// Null while no event runs.
	public EventPosition? EventPosition() =>
		Read(Native.EventPositionGet(_handle, out PsivEventPosition at)) ? new EventPosition(Native.Str(at.Event), Native.Str(at.File), at.Line) : null;

	// The background events running now, in start order, each with the line it is at (events/FORMAT.md, "Background events").
	public IReadOnlyList<EventPosition> BackgroundEvents()
	{
		Must(Native.BackgroundEvents(_handle, out IntPtr p, out int count));
		var list = new List<EventPosition>(count);
		foreach (PsivEventPosition at in Native.Span<PsivEventPosition>(p, count))
		{
			list.Add(new EventPosition(Native.Str(at.Event), Native.Str(at.File), at.Line));
		}
		return list;
	}

	// The story pictures up now, bottom first; draw them in that order.
	public IReadOnlyList<string> Pictures()
	{
		Must(Native.Pictures(_handle, out IntPtr p, out int count));
		return Native.Strs(p, count);
	}

	// The followers on the leader's trail, in order; empty outside an area.
	public IReadOnlyList<Follower> Followers()
	{
		if (!Read(Native.Followers(_handle, out IntPtr p, out int count)))
		{
			return [];
		}
		var list = new List<Follower>(count);
		foreach (PsivFollower x in Native.Span<PsivFollower>(p, count))
		{
			list.Add(new Follower(Native.Str(x.Id), x.X, x.Y));
		}
		return list;
	}

	// The party's area's moving platforms; empty outside an area or where there are none.
	public IReadOnlyList<Platform> Platforms()
	{
		if (!Read(Native.Platforms(_handle, out IntPtr p, out int count)))
		{
			return [];
		}
		var list = new List<Platform>(count);
		foreach (PsivPlatform x in Native.Span<PsivPlatform>(p, count))
		{
			list.Add(new Platform(Native.Str(x.Id), (PlatformMode)x.Mode, x.FromX, x.FromY, x.ToX, x.ToY, x.X, x.Y, x.PixelX, x.PixelY, x.End, x.PauseMs, x.Carrying != 0,
				x.PositionUs, x.RunUs));
		}
		return list;
	}

	// Whether Load may run now (from the title only), for a menu that shows or hides it.
	public bool CanLoad => Native.CanLoad(_handle) != 0;

	public BattleKeys BattleKeys()
	{
		Must(Native.BattleKeysGet(_handle, out PsivBattleKeys k));
		return new BattleKeys(Native.Strs(k.Messages, k.MessageCount), Native.Strs(k.Notices, k.NoticeCount), Native.Strs(k.CancelReasons, k.CancelReasonCount));
	}

	public SceneCamera SceneCamera()
	{
		Must(Native.SceneCameraGet(_handle, out PsivSceneCamera c));
		return new SceneCamera(c.Held != 0, c.X, c.Y, c.Ms, c.TotalMs);
	}

	// Null while no credits play.
	public CreditsInfo? Credits()
	{
		if (!Read(Native.CreditsInfoGet(_handle, out PsivCreditsInfo info)) || !Read(Native.CreditsEntries(_handle, out IntPtr p, out int count)))
		{
			return null;
		}
		var entries = new List<CreditsEntry>(count);
		foreach (PsivCreditsEntry e in Native.Span<PsivCreditsEntry>(p, count))
		{
			entries.Add(new CreditsEntry((CreditsEntryKind)e.Kind, Native.Str(e.Key), Native.Str(e.Text), e.Gap, e.PauseMs));
		}
		return new CreditsInfo(Native.Str(info.Id), Native.Str(info.Music), info.Shown, info.ElapsedMs, info.TotalMs, new ValueList<CreditsEntry>(entries));
	}

	public IReadOnlyList<Zone> FieldZones()
	{
		if (!Read(Native.FieldZones(_handle, out IntPtr p, out int count)))
		{
			return [];
		}
		var list = new List<Zone>(count);
		foreach (PsivZone z in Native.Span<PsivZone>(p, count))
		{
			string[] encounters = Native.Strs(z.Encounters, z.EntryCount);
			ReadOnlySpan<int> weights = Native.Span<int>(z.Weights, z.EntryCount);
			var table = new List<(string, int)>(z.EntryCount);
			for (int i = 0; i < z.EntryCount; i++)
			{
				table.Add((encounters[i], weights[i]));
			}
			ReadOnlySpan<int> cells = Native.Span<int>(z.Spawns, z.SpawnCount * 2);
			var spawns = new List<(int, int)>(z.SpawnCount);
			for (int i = 0; i < z.SpawnCount; i++)
			{
				spawns.Add((cells[2 * i], cells[2 * i + 1]));
			}
			list.Add(new Zone(Native.Str(z.Id), z.FromX, z.FromY, z.ToX, z.ToY, table, spawns));
		}
		return list;
	}

	// The game as a save file's text, or the refusal at the title.
	public (PsivResult Result, string Text) SaveText()
	{
		PsivResult result = Result(Native.SaveText(_handle, out IntPtr text));
		return (result, result.Ok ? Native.Str(text) : "");
	}

	public PsivResult LoadText(string text) => Result(Native.LoadText(_handle, text));

	// Loads the data again and rebuilds the game session behind this one, restoring the game (see psiv_session_reload).
	// Throws PsivDataException when the data has problems, with the old session kept; a game that does not load into the
	// new data is a refusal.
	public PsivResult Reload()
	{
		int status = Native.SessionReload(_handle);
		if ((PsivStatus)status == PsivStatus.DataError)
		{
			throw new PsivDataException(LastError(), DataProblems());
		}
		return Result(status);
	}

	// The actor walking with the party without joining it, "" for none.
	public string Follower()
	{
		Must(Native.Follower(_handle, out IntPtr actor));
		return Native.Str(actor);
	}

	public CreatureKind CharacterKind(string character)
	{
		Must(Native.CharacterKind(_handle, character, out int kind));
		return (CreatureKind)kind;
	}

	// A vehicle's uses left of one of its skills, or null when the party does not have it or it has no such skill.
	public int? VehicleSkillUses(string vehicle, string skill) =>
		Read(Native.VehicleSkillUses(_handle, vehicle, skill, out int uses)) ? uses : null;

	// Whether the save directory holds a readable clear-data record: the story was finished.
	public bool ClearRecordExists()
	{
		Must(Native.ClearRecordExists(_handle, out int exists));
		return exists != 0;
	}

	// Every element the data knows: the built-in ones, then those rules.json declares.
	public IReadOnlyList<ElementInfo> Elements()
	{
		Must(Native.Elements(_handle, out IntPtr p, out int count));
		var list = new List<ElementInfo>(count);
		foreach (PsivElementInfo e in Native.Span<PsivElementInfo>(p, count))
		{
			list.Add(new ElementInfo((Element)e.Index, Native.Str(e.Id), Native.Str(e.Name)));
		}
		return list;
	}

	// Every audio id the data lists, for a sound test.
	public IReadOnlyList<string> AudioIds()
	{
		Must(Native.AudioIds(_handle, out IntPtr p, out int count));
		return Native.Strs(p, count);
	}
}
