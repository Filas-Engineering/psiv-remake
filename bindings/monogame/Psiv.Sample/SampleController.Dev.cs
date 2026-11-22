using System.Globalization;

using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// Developer tools, only with --dev (docs/dev-tools.md): the console and its commands, the data reload, the overlays, the
// panel and scene previewer, and frame stepping and speed. Their keys are the window's own, outside the bindings.
public sealed partial class SampleController
{
	private const Keys ConsoleKey = Keys.OemTilde;
	private const Keys ReloadKey = Keys.F6;
	private const Keys PreviewKey = Keys.F12;
	private const Keys SlowerKey = Keys.OemOpenBrackets;
	private const Keys FasterKey = Keys.OemCloseBrackets;
	private const Keys PauseKey = Keys.OemPipe;
	private const Keys StepKey = Keys.OemPeriod;
	private const int StepMs = 16;
	private const int ConsoleLines = 10;
	private const int ErrorShownMs = 10000;
	private const int OverlayText = 13;

	private static readonly float[] Speeds = [0.25f, 0.5f, 1f, 2f, 4f, 8f];

	private static readonly (string Name, Keys Key)[] OverlayKeys =
	[
		("collision", Keys.F2), ("hooks", Keys.F3), ("zones", Keys.F4), ("actors", Keys.F7), ("camera", Keys.F8), ("fps", Keys.F9), ("event", Keys.F10),
	];

	// The layout's windows outlined with their names (F1); `overlay all` and `overlay none` leave it as it is.
	private const string WindowsOverlay = "windows";

	private static readonly string[] Help =
	[
		"warp AREA MARKER | warp AREA X Y      flag NAME on|off      give ID [COUNT]      money AMOUNT",
		"level MEMBER LEVEL      battle ENCOUNTER      win | lose      vehicle ID AREA MARKER",
		"encounters on|off      event ID      list KIND      reload      clear",
		"overlay NAME [on|off] | overlay all|none   (collision hooks zones actors camera fps event)",
		"speed 0.25|0.5|1|2|4|8      pause | resume | step [N]",
		"preview | preview event ID | preview battle ENTRY | preview next|prev|stop",
	];

	private readonly bool _dev;
	private readonly string? _dataDir;
	private bool _console;
	private string _consoleInput = "";
	private readonly List<(string Text, bool Bad)> _consoleOut = [];
	private readonly List<string> _consoleHistory = [];
	private int _historyAt;
	private readonly HashSet<string> _overlays = [];
	private readonly Dictionary<string, string> _overlayDrawn = [];
	private int _speedIndex = 2;
	private bool _paused;
	private int _stepsPending;
	private int _speedCarryMs;
	private readonly Queue<int> _frameTimes = new();
	private int _devClockMs;
	private string _reloadResult = "-";
	private List<string> _devError = [];
	private int _devErrorMs;
	private bool _hudMarkDrawn;

	// The previewer: its picker, an event playing in a scratch session, or a battle presentation entry on dummies.
	private bool _picker;
	private int _pickerTab;
	private readonly ScrollList _pickerList = new();
	private EventPreview? _preview;
	private string? _stageEntry;
	private IReadOnlyList<Combatant>? _stage;
	private BattleInfo? _stageInfo;
	private List<BattleEvent> _stageEvents = [];
	// Panel shows this controller has presented, so the previewer can stop at each.
	private int _panelShows;

	private sealed class EventPreview(string id, GameSession session, SampleController controller)
	{
		public string Id { get; } = id;
		public GameSession Session { get; } = session;
		public SampleController Controller { get; } = controller;
	}

	public bool Dev => _dev;

	public string? DataDirectory => _dataDir;

	private float Speed => Speeds[_speedIndex];

	// What the battle stage draws: the battle presentation preview's dummies, else the session's battle.
	private const int DevSyncMs = 600;

	private IReadOnlyList<Combatant> StageCombatants() => _stage ?? _s.Combatants();

	private BattleInfo? StageInfo() => _stage is null ? _s.BattleInfo() : _stageInfo;

	// `bad` shows the line as a refusal or an error.
	private void DevLog(string line, bool bad = false)
	{
		_consoleOut.Add((line, bad));
		if (_consoleOut.Count > 200)
		{
			_consoleOut.RemoveAt(0);
		}
		_log("dev: " + line);
	}

	// Runs before the frame's update: dev keys, the console and the picker take the input, the previewed event plays, and
	// the frame time is scaled, paused or stepped. False when the screens get no update this frame.
	private bool DevUpdate(ref int dtMs, InputState raw)
	{
		_frameTimes.Enqueue(dtMs);
		if (_frameTimes.Count > 30)
		{
			_frameTimes.Dequeue();
		}
		_devClockMs += dtMs;
		if (_devError.Count > 0 && (_devErrorMs -= dtMs) <= 0)
		{
			_devError = [];
		}
		if (raw.Pressed(ConsoleKey))
		{
			_console = !_console;
			return false;
		}
		if (_console)
		{
			UpdateConsole(raw);
			return false;
		}
		if (_picker)
		{
			UpdatePicker(new ActionInput(raw, _bindings));
			return false;
		}
		foreach ((string name, Keys key) in OverlayKeys.Append((WindowsOverlay, Keys.F1)))
		{
			if (raw.Pressed(key))
			{
				ToggleOverlay(name, !_overlays.Contains(name));
			}
		}
		if (raw.Pressed(ReloadKey))
		{
			ReloadData();
		}
		if (raw.Pressed(PreviewKey))
		{
			OpenPicker();
			return false;
		}
		if (raw.Pressed(SlowerKey) || raw.Pressed(FasterKey))
		{
			_speedIndex = Math.Clamp(_speedIndex + (raw.Pressed(FasterKey) ? 1 : -1), 0, Speeds.Length - 1);
			DevLog(T("ui.dev.speed", Speed));
		}
		if (raw.Pressed(PauseKey))
		{
			_paused = !_paused;
			DevLog(T(_paused ? "ui.dev.paused" : "ui.dev.resumed"));
		}
		if (raw.Pressed(StepKey))
		{
			_paused = true;
			_stepsPending++;
		}
		if (raw.Pressed(StepBackKey) && _animPreview is not null)
		{
			AnimSeek(PreviewFrame() - BattleClock.Frame);
		}
		if (_preview is not null || _stage is not null)
		{
			if (raw.Pressed(Keys.End))
			{
				StopPreview();
				return false;
			}
			if (raw.Pressed(Keys.PageDown) && _animPreview is { } replay)
			{
				StartAnimPreview(replay);
			}
			else if (raw.Pressed(Keys.PageDown))
			{
				PreviewNext();
			}
			else if (raw.Pressed(Keys.PageUp))
			{
				PreviewPrevious();
			}
		}
		if (_paused)
		{
			if (_stepsPending == 0)
			{
				return false;
			}
			_stepsPending--;
			dtMs = StepMs;
		}
		else
		{
			_speedCarryMs += (int)(dtMs * Speed * 4);
			dtMs = _speedCarryMs / 4;
			_speedCarryMs %= 4;
		}
		if (_preview is { } preview)
		{
			preview.Controller.Update(dtMs, raw);
			return false;
		}
		return true;
	}

	private void UpdateConsole(InputState raw)
	{
		bool shift = raw.Down(Keys.LeftShift) || raw.Down(Keys.RightShift);
		foreach (Keys k in raw.PressedKeys)
		{
			switch (k)
			{
			case Keys.Enter:
				string line = _consoleInput.Trim();
				_consoleInput = "";
				if (line.Length > 0)
				{
					_consoleHistory.Add(line);
					_historyAt = _consoleHistory.Count;
					DevCommand(line);
				}
				break;
			case Keys.Back when _consoleInput.Length > 0:
				_consoleInput = _consoleInput[..^1];
				break;
			case Keys.Escape:
				_console = false;
				break;
			case Keys.Up when _consoleHistory.Count > 0:
				_historyAt = Math.Max(0, _historyAt - 1);
				_consoleInput = _consoleHistory[_historyAt];
				break;
			case Keys.Down when _consoleHistory.Count > 0:
				_historyAt = Math.Min(_consoleHistory.Count, _historyAt + 1);
				_consoleInput = _historyAt < _consoleHistory.Count ? _consoleHistory[_historyAt] : "";
				break;
			default:
				if (KeyChar(k, shift) is { } c && _consoleInput.Length < 120)
				{
					_consoleInput += c;
				}
				break;
			}
		}
	}

	private static char? KeyChar(Keys k, bool shift) => k switch
	{
		>= Keys.A and <= Keys.Z => (char)((shift ? 'A' : 'a') + (k - Keys.A)),
		>= Keys.D0 and <= Keys.D9 => (char)('0' + (k - Keys.D0)),
		>= Keys.NumPad0 and <= Keys.NumPad9 => (char)('0' + (k - Keys.NumPad0)),
		Keys.Space => ' ',
		Keys.OemMinus => shift ? '_' : '-',
		Keys.Subtract => '-',
		Keys.OemPeriod or Keys.Decimal => '.',
		Keys.OemComma => ',',
		Keys.OemSemicolon => shift ? ':' : ';',
		_ => null,
	};

	// One console command line, from the console or a script's `dev` line. False when it was refused or not understood;
	// every outcome is written to the console and the log.
	public bool DevCommand(string line)
	{
		if (!_dev)
		{
			_log("dev: ignored, developer tools are off (start with --dev): " + line);
			return false;
		}
		DevLog("> " + line);
		string[] w = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
		if (w.Length == 0)
		{
			return false;
		}
		bool Result(PsivResult result, string done)
		{
			if (result.Error is { } error)
			{
				DevLog(T("ui.dev.refused_key", error.Text, error.Key), bad: true);
				return false;
			}
			DevLog(done);
			TakeNotices();
			return true;
		}
		bool Number(string text, out int value) => int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value);
		bool OnOff(string text, out bool on)
		{
			on = text == "on";
			return text is "on" or "off";
		}
		switch (w[0])
		{
		// `help` keeps its lines, which transcripts hold; the overlays, the layout windows' among them, have their own.
		case "help" when w is [_, "overlays"]:
			DevLog("overlay NAME [on|off]   " + string.Join("  ", OverlayKeys.Prepend((Name: WindowsOverlay, Key: Keys.F1)).Select(o => $"{o.Name} ({o.Key})")));
			DevLog("overlay all|none   every overlay but windows");
			return true;
		case "help":
			foreach (string h in Help)
			{
				DevLog(h);
			}
			return true;
		case "clear":
			_consoleOut.Clear();
			return true;
		case "list" when w.Length == 2 && Enum.TryParse(w[1], true, out DevList list) && Enum.IsDefined(list):
			IReadOnlyList<string> ids = _s.DevIds(list);
			DevLog($"{w[1]} ({ids.Count}):");
			for (int i = 0; i < ids.Count; i += 6)
			{
				DevLog("  " + string.Join("  ", ids.Skip(i).Take(6)));
			}
			return true;
		case "warp" when w.Length == 3:
			return Result(_s.DevWarp(w[1], w[2]), T("ui.dev.warped", $"{w[1]} {w[2]}"));
		case "warp" when w.Length == 4 && Number(w[2], out int x) && Number(w[3], out int y):
			return Result(_s.DevWarpCell(w[1], x, y), T("ui.dev.warped", $"{w[1]} {x} {y}"));
		case "flag" when w.Length == 3 && OnOff(w[2], out bool flagOn):
			return Result(_s.DevSetFlag(w[1], flagOn), T("ui.dev.flag", w[1], T(flagOn ? "ui.dev.on" : "ui.dev.off")));
		case "give" when w.Length is 2 or 3:
			int count = 1;
			if (w.Length == 3 && !Number(w[2], out count))
			{
				break;
			}
			return Result(_s.DevGive(w[1], count), T("ui.dev.gave", count, w[1]));
		case "money" when w.Length == 2 && Number(w[1], out int amount):
			return Result(_s.DevMoney(amount), T("ui.dev.money", _s.WorldInfo().Money));
		case "level" when w.Length == 3 && Number(w[2], out int level):
			return Result(_s.DevSetLevel(w[1], level), T("ui.dev.level", w[1], level));
		case "battle" when w.Length == 2:
			return Result(_s.DevBattle(w[1]), T("ui.dev.battle", w[1]));
		case "win" or "lose" when w.Length == 1:
			(PsivResult ended, IReadOnlyList<BattleEvent> events) = _s.DevEndBattle(w[0] == "win");
			if (ended.Ok && _battle.Active && _stage is null)
			{
				// The command set hit points outside the events, so a step brings the stage to the session's state before
				// the outcome shows.
				List<BattleStep> ending = BattleTimeline.Build(events, Context());
				int outcome = ending.FindIndex(x => x.Kind == StepKind.Outcome);
				ending.Insert(outcome < 0 ? ending.Count : outcome, Context().Step(StepKind.Sync, "", "sync", [], lengthMs: DevSyncMs));
				AttachScripts(ending);
				_timeline.Add(ending);
				_battle.Phase = BattlePhase.Playback;
			}
			return Result(ended, T(w[0] == "win" ? "ui.dev.won" : "ui.dev.lost"));
		case "vehicle" when w.Length == 4:
			return Result(_s.DevVehicle(w[1], w[2], w[3]), T("ui.dev.vehicle", w[1], $"{w[2]} {w[3]}"));
		case "encounters" when w.Length == 2 && OnOff(w[1], out bool encountersOn):
			return Result(_s.DevEncounters(encountersOn), T("ui.dev.encounters", T(encountersOn ? "ui.dev.on" : "ui.dev.off")));
		case "event" when w.Length == 2:
			return Result(_s.DevRunEvent(w[1]), T("ui.dev.event", w[1]));
		case "overlay" when w.Length == 2 && w[1] is "all" or "none":
			foreach ((string name, _) in OverlayKeys)
			{
				ToggleOverlay(name, w[1] == "all");
			}
			return true;
		case "overlay" when w.Length is 2 or 3 && (OverlayKeys.Any(o => o.Name == w[1]) || w[1] == WindowsOverlay):
			bool overlayOn = !_overlays.Contains(w[1]);
			if (w.Length == 3 && !OnOff(w[2], out overlayOn))
			{
				break;
			}
			ToggleOverlay(w[1], overlayOn);
			return true;
		case "speed" when w.Length == 2 && Array.FindIndex(Speeds, v => v.ToString(CultureInfo.InvariantCulture) == w[1]) is >= 0 and int index:
			_speedIndex = index;
			DevLog(T("ui.dev.speed", w[1]));
			return true;
		case "pause" or "resume" when w.Length == 1:
			_paused = w[0] == "pause";
			DevLog(T(_paused ? "ui.dev.paused" : "ui.dev.resumed"));
			return true;
		case "step" when w.Length is 1 or 2:
			int steps = 1;
			if (w.Length == 2 && (!Number(w[1], out steps) || steps < 1))
			{
				break;
			}
			_paused = true;
			_stepsPending += steps;
			DevLog(T(steps == 1 ? "ui.dev.stepping_one" : "ui.dev.stepping", steps));
			return true;
		case "reload" when w.Length == 1:
			return ReloadData();
		case "anim":
		case "preview" when w.Length >= 3 && w[1] == "anim":
			return AnimCommand(w);
		case "preview" when w.Length == 1:
			OpenPicker();
			return true;
		case "preview" when w.Length == 3 && w[1] == "event":
			return StartEventPreview(w[2]);
		case "preview" when w.Length == 3 && w[1] == "battle":
			return StartStagePreview(w[2]);
		case "preview" when w.Length == 2 && w[1] is "next" or "prev" or "stop":
			if (_preview is null && _stage is null)
			{
				DevLog(T("ui.dev.refused", T("ui.dev.nothing_previewed")), bad: true);
				return false;
			}
			if (w[1] == "stop")
			{
				StopPreview();
			}
			else if (w[1] == "next")
			{
				PreviewNext();
			}
			else
			{
				PreviewPrevious();
			}
			return true;
		}
		DevLog(T("ui.dev.unreadable"), bad: true);
		return false;
	}

	private void ToggleOverlay(string name, bool on)
	{
		if (on ? _overlays.Add(name) : _overlays.Remove(name))
		{
			DevLog(T("ui.dev.overlay", name, T(on ? "ui.dev.on" : "ui.dev.off")));
		}
	}

	// The data and event scripts again, through the session's reload: the game is saved to memory, a session is built on
	// the new data and the game restored into it. Problems show on screen and in the log, and the old session stays.
	private bool ReloadData()
	{
		if (_preview is not null || _stage is not null)
		{
			StopPreview();
		}
		PsivResult result;
		try
		{
			result = _s.Reload();
		}
		catch (PsivDataException e)
		{
			_reloadResult = "error";
			ShowDevError([T("ui.dev.data_not_loaded"), .. e.Problems.Select(p => p.Text)]);
			return false;
		}
		if (result.Error is { } error)
		{
			_reloadResult = "refused";
			ShowDevError([T("ui.dev.data_not_fitting"), error.Text]);
			return false;
		}
		if (_dataDir is not null)
		{
			_data = GameData.Load(_s, _dataDir);
			LogDataProblems();
		}
		_actorIds = [.. _s.ActorIds()];
		_gameInfo = null;
		// The new session numbers its actions afresh and rebuilds the scene with instant ones.
		_animations.Clear();
		_field.ClearScene();
		_panels.Clear();
		_camera = null;
		_cameraMove = null;
		_shake = null;
		_scriptMusic = null;
		_scriptAmbient = null;
		_battle.Active = false;
		_timeline.Clear();
		_devError = [];
		_reloadResult = "ok";
		DevLog(_s.FieldInfo() is { } f ? T("ui.dev.reloaded_at", $"{f.AreaId} {f.LeaderX} {f.LeaderY}") : T("ui.dev.reloaded"));
		TakeNotices();
		return true;
	}

	// The first problems only: one broken file makes others fail to match it.
	private void ShowDevError(List<string> lines)
	{
		_devError = lines;
		_devErrorMs = ErrorShownMs;
		foreach (string line in lines.Take(12))
		{
			DevLog(T("ui.dev.error", line), bad: true);
		}
		if (lines.Count > 12)
		{
			DevLog(T("ui.dev.error", T("ui.dev.more", lines.Count - 12)), bad: true);
		}
	}

	// The previewer's lists: every event the data has, and every battle presentation entry, actions then fallbacks.
	private List<string> PickerEntries() => _pickerTab == 0
		? [.. _s.DevIds(DevList.Events)]
		: [.. PresentationTable.Actions.Keys.Order(StringComparer.Ordinal).Concat(PresentationTable.Fallbacks.Keys.Order(StringComparer.Ordinal))];

	private void OpenPicker()
	{
		_picker = true;
		_pickerList.Reset();
		DevLog(T("ui.dev.picker_opened"));
	}

	private void UpdatePicker(ActionInput input)
	{
		if (input.Back || input.Raw.Pressed(PreviewKey))
		{
			_picker = false;
			return;
		}
		if (input.Pressed(GameAction.Left) || input.Pressed(GameAction.Right))
		{
			_pickerTab = 1 - _pickerTab;
			_pickerList.Reset();
		}
		List<string> entries = PickerEntries();
		_pickerList.Move(input, entries.Count);
		if (input.Confirm && entries.Count > 0)
		{
			_picker = false;
			string entry = entries[_pickerList.Cursor];
			if (_pickerTab == 0)
			{
				StartEventPreview(entry);
			}
			else
			{
				StartStagePreview(entry);
			}
		}
	}

	// Clean state: a new game in a scratch session that writes nothing (no save directory, seed 1), its opening event
	// skipped or played through with the first answers, then the party put beside the hook that starts the event (or left
	// at the start for an event no hook starts), and the event started at once.
	private bool StartEventPreview(string id)
	{
		if (_dataDir is null)
		{
			DevLog(T("ui.dev.refused", T("ui.dev.no_data_directory")), bad: true);
			return false;
		}
		if (!_s.DevIds(DevList.Events).Contains(id))
		{
			DevLog(T("ui.dev.refused", T("ui.dev.no_event", id)), bad: true);
			return false;
		}
		StopPreview();
		GameSession scratch = GameSession.Create(new SessionOptions { DataDirectory = _dataDir, Seed = 1, Presentation = Presentation.Wait });
		scratch.NewGame().ThrowIfFailed();
		SettleScratch(scratch);
		if (_data.EventStarts.TryGetValue(id, out (string Area, int X, int Y) start))
		{
			if (scratch.FieldInfo()?.AreaId != start.Area && _s.DevIds(DevList.Markers).FirstOrDefault(m => m.StartsWith(start.Area + ":", StringComparison.Ordinal)) is { } marker)
			{
				scratch.DevWarp(start.Area, marker[(start.Area.Length + 1)..]);
				SettleScratch(scratch);
			}
			if (scratch.Field() is { } view)
			{
				foreach ((int dx, int dy) in new[] { (0, 1), (1, 0), (-1, 0), (0, -1), (0, 0) })
				{
					if (view.Cell(start.X + dx, start.Y + dy).HasFlag(CellFlags.Walkable) && scratch.DevWarpCell(start.Area, start.X + dx, start.Y + dy).Ok)
					{
						SettleScratch(scratch);
						break;
					}
				}
			}
		}
		scratch.DrainNotices();
		PsivResult started = scratch.DevRunEvent(id);
		if (started.Error is { } error)
		{
			scratch.Dispose();
			DevLog(T("ui.dev.refused", error.Text), bad: true);
			return false;
		}
		scratch.SetProfile(scratch.Profile() with { TextLanguage = _s.Profile().TextLanguage });
		var controller = new SampleController(scratch, _data, _log, null, new RecordingAudio(), _assetDir, battleLayout: _layoutOverride) { MissingText = MissingText };
		_preview = new EventPreview(id, scratch, controller);
		DevLog(T("ui.dev.previewing_event", id));
		return true;
	}

	private static void SettleScratch(GameSession s)
	{
		for (int i = 0; i < 20000 && s.Mode != Mode.Field && s.Mode != Mode.Title; i++)
		{
			foreach (PresentationAction a in s.PollActions())
			{
				if (a.Phase == ActionPhase.Begin && !a.Instant)
				{
					s.FinishAction(a.Handle);
				}
			}
			switch (s.Mode)
			{
			case Mode.Event when s.CanSkip:
				s.Skip();
				break;
			case Mode.Event when s.LinePending:
				s.ConfirmLine();
				break;
			case Mode.Event when s.ChoicePending:
				s.Choose(0);
				break;
			case Mode.Event:
				s.Update(100);
				break;
			case Mode.Battle:
				s.PlayBattle();
				break;
			case Mode.Menu when s.MenuKind == MenuKind.NameEntry:
				s.EnterName(s.DisplayName(s.NameEntryMember));
				break;
			default:
				s.CloseMenu();
				break;
			}
		}
		foreach (PresentationAction a in s.PollActions())
		{
			if (a.Phase == ActionPhase.Begin && !a.Instant)
			{
				s.FinishAction(a.Handle);
			}
		}
	}

	// Plays the previewed event, answering lines and choices, until it has shown `target` panels, then pauses. A battle on
	// the way is played out by the session.
	private void SeekPanel(int target)
	{
		if (_preview is not { } p)
		{
			return;
		}
		// On until the panel has slid in, so the pause shows it whole.
		bool Reached() => p.Controller._panelShows >= target && p.Controller._panels.All(x => x.Transition is null);
		for (int frame = 0; frame < 6000 && !Reached() && p.Session.Mode != Mode.Field && p.Session.Mode != Mode.Title; frame++)
		{
			if (p.Session.LinePending)
			{
				p.Session.ConfirmLine();
			}
			else if (p.Session.ChoicePending)
			{
				p.Session.Choose(0);
			}
			else if (p.Session.Mode == Mode.Battle)
			{
				p.Session.PlayBattle();
				p.Controller._battle.Active = false;
				p.Controller._timeline.Clear();
			}
			p.Controller.Update(StepMs, new InputState());
		}
		_paused = true;
		_stepsPending = 0;
		DevLog(T(p.Controller._panelShows >= target ? "ui.dev.panel_reached" : "ui.dev.panel_missing", target, p.Id));
	}

	private void PreviewNext()
	{
		if (_preview is { } p)
		{
			SeekPanel(p.Controller._panelShows + 1);
		}
		else if (_stageEntry is { } entry)
		{
			StartStagePreview(entry);
		}
	}

	private void PreviewPrevious()
	{
		if (_preview is { } p)
		{
			int target = p.Controller._panelShows - 1;
			StartEventPreview(p.Id);
			if (target > 0)
			{
				SeekPanel(target);
			}
			else
			{
				_paused = true;
			}
		}
		else if (_stageEntry is { } entry)
		{
			StartStagePreview(entry);
		}
	}

	private void StopPreview()
	{
		if (_preview is { } p)
		{
			p.Session.Dispose();
			_preview = null;
			DevLog(T("ui.dev.preview_stopped"));
		}
		if (_stage is not null)
		{
			_stage = null;
			_stageInfo = null;
			_stageEntry = null;
			_animPreview = null;
			_forcedReaction = null;
			_overlays.Remove("anim");
			_battle.Active = false;
			_timeline.Clear();
			DevLog(T("ui.dev.preview_stopped"));
		}
	}

	// A battle presentation entry played on dummy targets: synthetic typed events of one turn by the first lineup member,
	// built into steps by BattleTimeline as a round's are; no session battle runs.
	// `change` (`transform.ENEMY`) adds a change of form after the results; `dummy` names the dummies' enemy.
	private bool StartStagePreview(string entry, string? change = null, string? dummy = null, bool combo = false)
	{
		if (_s.Mode == Mode.Battle)
		{
			DevLog(T("ui.dev.refused", T("ui.dev.finish_battle")), bad: true);
			return false;
		}
		bool action = PresentationTable.Actions.ContainsKey(entry);
		if (!action && !PresentationTable.Fallbacks.ContainsKey(entry))
		{
			DevLog(T("ui.dev.refused", T("ui.dev.no_entry", entry)), bad: true);
			return false;
		}
		StopPreview();
		ActionShape shape;
		string ability;
		if (action)
		{
			ability = entry;
			shape = _data.Actions.GetValueOrDefault(entry) ?? new ActionShape("attack", "damage", "none");
		}
		else
		{
			string[] parts = entry.Split('.');
			shape = new ActionShape(parts[0], parts.Length > 1 ? parts[1] : "damage", parts.Length > 2 ? parts[2] : "none");
			// Fallback keys hold dots, which no ability id has, so the key itself names the synthetic action.
			ability = entry;
			_data.Actions[ability] = shape;
		}
		CommandKind command = shape.Kind switch { "item" => CommandKind.Item, "defend" => CommandKind.Defend, "attack" => CommandKind.Attack, _ => CommandKind.Ability };
		List<string> lineup = _s.Mode == Mode.Title ? [] : Lineup();
		List<string> characters = [.. lineup.Concat(_data.Characters.Order(StringComparer.Ordinal)).Distinct()];
		string enemy = dummy is not null && _data.Enemies.Contains(dummy) ? dummy : _data.Enemies.Order(StringComparer.Ordinal).FirstOrDefault() ?? "dummy";
		string status = _data.Statuses.Order(StringComparer.Ordinal).FirstOrDefault() ?? "";
		Combatant Make(int index, string id, string label, Side side, int hp, IReadOnlyList<ActiveStatus> statuses) =>
			new(index, id, label, label, side, side == Side.Enemy ? "dummy" : "", hp, 100, 50, 50, hp > 0, false, statuses, CommandKind.None, "", -1);
		var stage = new List<Combatant>
		{
			Make(0, characters[0], _s.DisplayName(characters[0]), Side.Party, shape.Effect == "heal" ? 70 : 100,
				shape.Effect == "cure" && status.Length > 0 ? [new ActiveStatus(status, _s.DisplayName(status), 0)] : []),
			Make(1, characters[Math.Min(1, characters.Count - 1)], _s.DisplayName(characters[Math.Min(1, characters.Count - 1)]), Side.Party, shape.Effect == "revive" ? 0 : 100, []),
		};
		for (int i = 0; i < 3; i++)
		{
			stage.Add(Make(2 + i, enemy, T("ui.dev.dummy", (char)('A' + i)), Side.Enemy, 100, []));
		}
		List<int> targets = shape.Effect switch
		{
			"heal" or "cure" or "stat" => [0],
			"revive" => [1],
			_ when command is CommandKind.Attack or CommandKind.Defend => [2],
			_ => [2, 3, 4],
		};
		_stageEvents = StageEvents(command, ability, shape, targets, status);
		// A combination's turn: the first two members cast it together.
		int head = _stageEvents.FindIndex(e => e.Type == BattleEventType.ActionStarted);
		if (combo && head >= 0)
		{
			_stageEvents[head] = _stageEvents[head] with { Type = BattleEventType.ComboTriggered, Participants = [0, 1] };
		}
		if (change is not null && _stageEvents.Count > 0)
		{
			string[] ce = change.Split('.', 2);
			string into = ce.Length > 1 && _data.Enemies.Contains(ce[1]) ? ce[1] : enemy;
			BattleEvent last = _stageEvents[^1];
			_stageEvents.Insert(_stageEvents.Count - 1, last with { Type = ce[0] == "join" ? BattleEventType.EnemyJoined : BattleEventType.EnemyTransformed, Actor = 0, Target = ce[0] == "join" ? 5 : 2,
				Ids = [into], Hp = 100, Amount = 3 });
		}
		_stage = stage;
		_stageInfo = new BattleInfo(true, "", BattleSource.Event, 1, 2, stage.Count, Outcome.Ongoing, false, false, false, false, "", "");
		_stageEntry = entry;
		_battle.Active = true;
		_battle.Labels = [.. stage.Select(c => c.Label)];
		_battle.Log.Clear();
		_battle.Phase = BattlePhase.Playback;
		_show.Start(stage, 2);
		_timeline.Clear();
		List<BattleStep> previewSteps = BattleTimeline.Build(_stageEvents, Context());
		AttachScripts(previewSteps);
		_timeline.Add(previewSteps);
		DevLog(T("ui.dev.previewing_battle", entry));
		return true;
	}

	private static List<BattleEvent> StageEvents(CommandKind command, string ability, ActionShape shape, List<int> targets, string status)
	{
		var none = new BattleEvent(BattleEventType.RoundStarted, 1, -1, -1, -1, "", "", "", "", [], "", 0, 0, 0, CommandKind.None, Element.None,
			Effectiveness.Normal, Stat.Strength, CancelReason.None, StatusEnd.Cured, Outcome.Ongoing, ResourceKind.None, Opening.Normal, false, false, 0, 0,
			new Dictionary<string, int>(), "", [], [], [], [], 0, false, false, false, []);
		string id = command is CommandKind.Attack or CommandKind.Defend ? "" : ability;
		Element element = Enum.TryParse(shape.Element, true, out Element e) ? e : Element.None;
		var events = new List<BattleEvent>
		{
			none,
			none with { Type = BattleEventType.CommandsLocked, Commands = [new RecordedCommand(0, command, id, targets[0])] },
			none with { Type = BattleEventType.OrderResolved, Order = [0] },
			none with { Type = BattleEventType.ActionStarted, Actor = 0, Target = targets[0], Ability = id, Command = command },
		};
		if (command == CommandKind.Ability)
		{
			events.Add(none with { Type = BattleEventType.ResourceSpent, Actor = 0, Resource = ResourceKind.Tp, Amount = 5, Ability = id });
		}
		foreach (int t in targets)
		{
			switch (shape.Effect)
			{
			case "heal":
				events.Add(none with { Type = BattleEventType.Healed, Actor = 0, Target = t, Amount = 30, Hp = 100, Ability = id });
				break;
			case "revive":
				events.Add(none with { Type = BattleEventType.Revived, Actor = 0, Target = t, Amount = 30, Hp = 30, Ability = id });
				break;
			case "status" when status.Length > 0:
				events.Add(none with { Type = BattleEventType.StatusApplied, Actor = 0, Target = t, Status = status, Duration = 3, Ability = id });
				break;
			case "cure" when status.Length > 0:
				events.Add(none with { Type = BattleEventType.StatusRemoved, Actor = 0, Target = t, Status = status, StatusEnd = StatusEnd.Cured, Ability = id });
				break;
			case "stat":
				events.Add(none with { Type = BattleEventType.StatModified, Actor = 0, Target = t, Stat = Stat.Agility, Amount = 4, Ability = id });
				break;
			case "damage" when command != CommandKind.Defend:
				if (command == CommandKind.Attack)
				{
					events.Add(none with { Type = BattleEventType.Hit, Actor = 0, Target = t });
				}
				events.Add(none with { Type = BattleEventType.DamageDealt, Actor = 0, Target = t, Amount = 24, Hp = 76, Element = element, Ability = id, Command = command });
				break;
			case "damage":
				break;
			default:
				events.Add(none with { Type = BattleEventType.ItemHadNoEffect, Actor = 0, Target = t, Ability = id });
				break;
			}
		}
		events.Add(none with { Type = BattleEventType.RoundEnded });
		return events;
	}

	// The preview of a battle presentation entry plays like a round's playback, on the dev speed, and holds its last step.
	private void UpdateStagePreview(int dtMs, ActionInput input)
	{
		PlaybackTimeline(dtMs, input.Down(GameAction.FastForward), input.Confirm);
		if (input.Back)
		{
			StopPreview();
		}
	}

	// Overlays, the dev mark, the speed, the previewer and the console, over whatever the frame drew.
	private void DrawDev(IDrawSurface s, Art art)
	{
		if (_overlays.Contains(WindowsOverlay))
		{
			DrawWindowOverlay(s is ZoomSurface z ? z.Inner : s, s is ZoomSurface zoom ? zoom.Zoom : 1f);
		}
		DrawAnimRuler(s);
		int y = 52;
		if (_overlays.Contains("fps"))
		{
			double average = _frameTimes.Count == 0 ? 0 : _frameTimes.Average();
			string fps = T(_paused ? "ui.dev.fps_paused" : "ui.dev.fps", (average > 0 ? 1000 / average : 0).ToString("0.0", CultureInfo.InvariantCulture),
				average.ToString("0.0", CultureInfo.InvariantCulture), Speed);
			_overlayDrawn["fps"] = fps;
			Style.Plate(s, fps, s.Width - s.Measure(fps, Style.Small) - 40, y, Style.Good);
		}
		if (_overlays.Contains("event"))
		{
			EventPosition? at = (_preview?.Session ?? _s).EventPosition();
			string text = at is null ? T("ui.dev.no_event_running") : T("ui.dev.event_at", at.Event, $"{at.File}:{at.Line}");
			_overlayDrawn["event"] = at is null ? "-" : $"{at.Event} {at.File}:{at.Line}";
			Style.Plate(s, text, 16, y, Style.Accent);
		}
		DrawDevMark(s);
		if (_devError.Count > 0)
		{
			DrawDevError(s);
		}
		if (_picker)
		{
			DrawPicker(s);
		}
		if (_console)
		{
			DrawConsole(s);
		}
	}

	// The dev badge: always with --dev, red and "marked" once a developer command changed the game; the speed under it.
	private void DrawDevMark(IDrawSurface s)
	{
		bool marked = _s.Mode != Mode.Title && _s.WorldInfo().Dev;
		_hudMarkDrawn = marked;
		// While previewing, the badge also says what plays and how to move through it; the game itself is not marked by it.
		string badge = T(marked ? "ui.dev.badge_marked" : "ui.dev.badge") + (_preview is { } p
			? "   " + T("ui.dev.badge_event", p.Id, p.Controller._panelShows)
			: _stageEntry is { } entry ? "   " + T("ui.dev.badge_battle", entry) : "");
		// The battle's banners take the top middle, so there the badge sits at the right, at the battle layout's `dev_badge_y`
		// (built in: just above the side layout's party panels, in the front one's keys' line under the strip), or in the
		// front layout at the top while a preview's long badge would run into the keys.
		bool battle = Screen == Screen.Battle;
		// Off the battle, the styles' `dev_badge_x` (-1: centered) and `dev_badge_y` place it, in the tools' units (the
		// frame's pixels over `dev_scale`).
		int badgeX = Layout.Styles.Param("dev_badge_x");
		int x = battle ? s.Width - s.Measure(badge, Style.Small) - 40 : badgeX < 0 ? (s.Width - s.Measure(badge, Style.Small)) / 2 - 12 : badgeX;
		bool previewing = _preview is not null || _stageEntry is not null;
		bool side = _stageLayout == BattleLayout.Side;
		int y = !battle ? Layout.Styles.Param("dev_badge_y") : !side && previewing ? 12 : Ui.Param(side ? "battle_side" : "battle_front", "dev_badge_y", UiData(), 0);
		if (s is ZoomSurface zoomed && battle && !(!side && previewing))
		{
			y = (int)MathF.Round(y / zoomed.Zoom);
		}
		Style.Plate(s, badge, x, y, marked ? Style.Bad : previewing ? Style.Accent : Style.Dim);
		if (_paused || Speed != 1f)
		{
			string speed = _paused ? T("ui.dev.paused_banner", Key(StepKey), Key(PauseKey)) : T("ui.dev.speed", Speed);
			Style.Plate(s, speed, (s.Width - s.Measure(speed, Style.Body)) / 2 - 12, 52, Style.Accent, Style.Body);
		}
	}

	private static string Key(Keys k) => k switch { Keys.OemPeriod => ".", Keys.OemPipe => "\\", Keys.OemTilde => "`", _ => k.ToString() };

	// Drawn over the field and under the comic panels, so a scene's panels stay readable.
	private void DrawFieldOverlays(IDrawSurface s, FieldView view)
	{
		if (Screen is not (Screen.Field or Screen.Dialogue))
		{
			return;
		}
		int tile = _field.Cell;
		int x0 = Math.Max(0, _field.CameraX / tile), y0 = Math.Max(0, _field.CameraY / tile);
		int x1 = Math.Min(view.Info.Width - 1, (_field.CameraX + s.Width) / tile), y1 = Math.Min(view.Info.Height - 1, (_field.CameraY + s.Height) / tile);
		(int X, int Y) Corner(int x, int y) => (x * tile - _field.CameraX, y * tile - _field.CameraY);
		// Labels stay on screen and step down past ones already placed, so neighbors do not cover each other.
		var placed = new List<Rectangle>();
		void Label(string text, int x, int y, Color color)
		{
			int w = s.Measure(text, OverlayText) + 6, h = OverlayText + 4;
			var r = new Rectangle(Math.Clamp(x, 4, Math.Max(4, s.Width - w - 4)), Math.Clamp(y, 4, s.Height - h - 4), w, h);
			for (int tries = 0; tries < 6 && placed.Any(p => p.Intersects(r)); tries++)
			{
				r.Y += h + 2;
			}
			placed.Add(r);
			s.Fill(r.X, r.Y, r.Width, r.Height, Color.Black * 0.8f);
			s.Text(text, r.X + 3, r.Y + 1, color, OverlayText);
		}
		if (_overlays.Contains("collision"))
		{
			int marked = 0;
			for (int y = y0; y <= y1; y++)
			{
				for (int x = x0; x <= x1; x++)
				{
					CellFlags c = view.Cell(x, y);
					(int sx, int sy) = Corner(x, y);
					Color? fill = (c & CellFlags.TerrainMask) switch
					{
						CellFlags.Wall => new Color(230, 60, 60) * 0.35f,
						CellFlags.Water => new Color(60, 120, 240) * 0.4f,
						_ when !c.HasFlag(CellFlags.Walkable) => new Color(240, 160, 40) * 0.4f,
						_ => null,
					};
					if (fill is { } f)
					{
						s.Fill(sx, sy, tile, tile, f);
						marked++;
					}
					s.Fill(sx, sy, tile, 1, Color.White * 0.15f);
					s.Fill(sx, sy, 1, tile, Color.White * 0.15f);
				}
			}
			_overlayDrawn["collision"] = $"{marked} cells";
		}
		if (_overlays.Contains("zones"))
		{
			Color[] colors = [new(250, 120, 220), new(120, 240, 200), new(250, 210, 90), new(150, 160, 255)];
			IReadOnlyList<Zone> zones = _s.FieldZones();
			for (int i = 0; i < zones.Count; i++)
			{
				Zone z = zones[i];
				Color color = colors[i % colors.Length];
				(int zx, int zy) = Corner(z.FromX, z.FromY);
				int zw = (z.ToX - z.FromX + 1) * tile, zh = (z.ToY - z.FromY + 1) * tile;
				s.Fill(zx, zy, zw, zh, color * 0.12f);
				Style.Frame(s, zx, zy, zw, zh, color, 3);
				foreach ((int spawnX, int spawnY) in z.Spawns)
				{
					(int px, int py) = Corner(spawnX, spawnY);
					s.Fill(px + tile / 2 - 5, py + tile / 2 - 5, 10, 10, color);
				}
				Label(T("ui.dev.zone", z.Id, ListText(z.Table.Select(t => T("ui.common.item_count", t.Encounter, t.Weight)))), zx + 4, zy + 4, color);
			}
			_overlayDrawn["zones"] = zones.Count == 0 ? "none" : string.Join(", ", zones.Select(z => z.Id));
		}
		if (_overlays.Contains("hooks"))
		{
			foreach (Marker m in view.Markers)
			{
				(int mx, int my) = Corner(m.X, m.Y);
				s.Fill(mx + tile / 2 - 3, my + tile / 2 - 3, 6, 6, Style.Accent);
			}
			foreach (FieldExit e in view.Exits)
			{
				(int ex, int ey) = Corner(e.X, e.Y);
				Style.Frame(s, ex + 2, ey + 2, tile - 4, tile - 4, new Color(80, 220, 250), 3);
				Label($"> {e.ToArea}:{e.ToMarker}", ex, ey + tile + 2, new Color(80, 220, 250));
			}
			foreach (FieldPoint p in view.Points)
			{
				(int px, int py) = Corner(p.X, p.Y);
				Color color = p.Active ? Style.Good : Style.Dim;
				Style.Frame(s, px + 2, py + 2, tile - 4, tile - 4, color, 3);
				Label($"{T(NameKind.PointKind, (int)p.Kind)} {p.Id}" + (p.Event.Length > 0 ? $" > {p.Event}" : ""), px, py - OverlayText - 4, color);
			}
			_overlayDrawn["hooks"] = $"{view.Points.Count} points, {view.Exits.Count} exits, {view.Markers.Count} markers";
		}
		if (_overlays.Contains("actors"))
		{
			foreach ((string id, DrawnPerson person) in _field.DrawnActors.OrderBy(a => a.Key, StringComparer.Ordinal))
			{
				int px = (int)(person.X * tile) - _field.CameraX, py = (int)(person.Y * tile) - _field.CameraY;
				Label(id, px + tile / 2 - s.Measure(id, OverlayText) / 2 - 3, py - 22, Style.Text);
			}
			foreach (FieldEnemy e in view.Enemies)
			{
				(int ex, int ey) = Corner(e.X, e.Y);
				Label($"{e.Encounter}#{e.Id}", ex, ey - 22, Style.Bad);
			}
			_overlayDrawn["actors"] = string.Join(", ", _field.DrawnActors.Keys.Order(StringComparer.Ordinal));
		}
		if (_overlays.Contains("camera"))
		{
			(float fx, float fy) = _camera is null ? _field.Leader(Lineup()) : CameraFocus(view);
			int cx = (int)(fx * tile) - _field.CameraX + tile / 2, cy = (int)(fy * tile) - _field.CameraY + tile / 2;
			s.Fill(cx - 16, cy - 1, 32, 3, Style.Accent);
			s.Fill(cx - 1, cy - 16, 3, 32, Style.Accent);
			Style.Frame(s, cx - 10, cy - 10, 20, 20, Style.Accent);
			_overlayDrawn["camera"] = _camera ?? "leader";
			Label(T("ui.dev.camera", _camera ?? T("ui.dev.camera_leader")), cx + 14, cy + 10, Style.Accent);
		}
	}

	private void DrawDevError(IDrawSurface s)
	{
		int w = s.Width - 160;
		List<string> lines = [.. _devError.SelectMany(l => Style.Wrap(s, l, w - 40, Style.Small)).Take(8)];
		if (_devError.Count > 8)
		{
			lines.Add(T("ui.dev.more", _devError.Count - 8));
		}
		int h = 24 + lines.Count * 22 + 30;
		int x = 80, y = s.Height - 40 - h;
		Style.Box(s, x, y, w, h, 0.95f);
		Style.Frame(s, x, y, w, h, Style.Bad, 3);
		s.Text(T("ui.dev.reload_failed", KeyText(ReloadKey)), x + 20, y + 10, Style.Bad, Style.Body);
		for (int i = 0; i < lines.Count; i++)
		{
			s.Text(lines[i], x + 20, y + 40 + i * 22, i == 0 ? Style.Text : Style.Dim, Style.Small);
		}
	}

	private void DrawPicker(IDrawSurface s)
	{
		int x = 200, y = 70, w = s.Width - 400, h = s.Height - 140;
		s.Fill(0, 0, s.Width, s.Height, Color.Black * 0.5f);
		Style.Window(s, x, y, w, h, T("ui.dev.previewer"));
		Style.Tabs(s, x + 24, y + 44, [T("ui.dev.events"), T("ui.dev.battle_presentation")], _pickerTab);
		List<string> entries = PickerEntries();
		int fallbacks = _pickerTab == 1 ? PresentationTable.Actions.Count : int.MaxValue;
		DrawItems(s, _pickerList, x + 24, y + 90, w - 48, h - 150, [.. entries.Select((e, i) => i >= fallbacks ? T("ui.dev.fallback", e) : e)]);
		Widgets.Hints(s, x + 24, y + h - 44, w - 48, T("ui.dev.picker_hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Left), Hint(GameAction.Right),
			Hint(GameAction.Confirm), Hint(GameAction.Back)));
	}

	private void DrawConsole(IDrawSurface s)
	{
		int h = 34 + ConsoleLines * 22 + 40;
		s.Fill(0, 0, s.Width, h, new Color(8, 10, 18));
		s.Fill(0, h, s.Width, 2, Style.Accent);
		s.Text(T("ui.dev.console_title", Key(ConsoleKey), KeyText(Keys.Enter)), 16, 8, Style.Dim, Style.Small);
		List<(string Text, bool Bad)> shown = [.. _consoleOut.TakeLast(ConsoleLines)];
		for (int i = 0; i < shown.Count; i++)
		{
			string line = Style.Fit(s, shown[i].Text, s.Width - 32, Style.Small);
			s.Text(line, 16, 34 + i * 22, shown[i].Bad ? Style.Bad : shown[i].Text.StartsWith('>') ? Style.Accent : Style.Text, Style.Small);
		}
		bool caret = _devClockMs / 400 % 2 == 0;
		s.Text("> " + _consoleInput + (caret ? "_" : ""), 16, h - 32, Style.Accent, Style.Body);
	}

	// `devdraw`: the size the last frame drew the developer tools at, `off` when it drew none.
	private string _probeDevDraw = "off";

	private string DevState(string key)
	{
		if (key == "devdraw")
		{
			return _probeDevDraw;
		}
		if (key.StartsWith("preview.", StringComparison.Ordinal))
		{
			return _preview?.Controller.State(key["preview.".Length..]) ?? "-";
		}
		if (key.StartsWith("overlay.", StringComparison.Ordinal))
		{
			return _overlayDrawn.GetValueOrDefault(key["overlay.".Length..], "-");
		}
		return key switch
		{
			"dev" => _dev ? "on" : "off",
			"devmark" => _s.Mode != Mode.Title && _s.WorldInfo().Dev ? "yes" : "no",
			"hudmark" => _hudMarkDrawn ? "yes" : "no",
			"console" => _console ? "open" : "closed",
			"consolelast" => _consoleOut.Count == 0 ? "-" : _consoleOut[^1].Text,
			"overlays" => _overlays.Count == 0 ? "none" : string.Join(", ", OverlayKeys.Select(o => o.Name).Where(_overlays.Contains)),
			"speed" => "x" + Speed.ToString(CultureInfo.InvariantCulture),
			"paused" => _paused ? "yes" : "no",
			"clock" => _clockMs.ToString(CultureInfo.InvariantCulture),
			"playtime" => _s.Mode == Mode.Title ? "-" : _s.WorldInfo().PlayTimeMs.ToString(CultureInfo.InvariantCulture),
			"reload" => _reloadResult,
			"deverror" => _devError.Count == 0 ? "-" : string.Join(" | ", _devError),
			"encounters" => _s.Mode == Mode.Title ? "-" : _s.WorldInfo().Encounters ? "on" : "off",
			"leader" => _s.FieldInfo() is { } f ? $"{f.AreaId} {f.LeaderX} {f.LeaderY}" : "-",
			"preview" => _picker ? "picker"
				: _preview is { } p ? $"event {p.Id} panels {p.Controller._panelShows}"
				: _animPreview is { } animKey ? $"anim {animKey} frame {Frames(PreviewFrame())}"
				: _stageEntry is { } entry ? $"battle {entry} step {(_timeline.Done ? "done" : _timeline.Current?.Description ?? "starting")}"
				: "-",
			_ => "?",
		};
	}
}
