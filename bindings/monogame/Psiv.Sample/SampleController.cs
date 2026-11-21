using System.Globalization;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public enum Screen { Title, Field, Dialogue, Battle, Menu, Shop, Inn, SavePrompt, NameEntry, QuestLog, TextLog, Options, Pause, LoadGame, Travel, SoundTest }

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
	private HashSet<string> _actorIds;
	private readonly FieldRenderer _field = new();
	private Art? _art;
	private GameData _data;
	// The asset directory the sample was started with, and what loaded from it; null without one.
	private readonly string? _assetDir;
	private Assets? _assets;
	// The art is rebuilt at the next draw, after a reload.
	private bool _rebuildArt;
	// The UI skin font the surface was last given, as file and scale; null after switching back to the system font,
	// "" while it was never switched, so a run without a skin never calls UseFont.
	private string? _font = "";
	// Milliseconds of frame time seen, for animations; advances only with the frame times Update gets.
	private int _clockMs;
	private readonly List<(string Text, int MsLeft)> _ticker = [];
	private Screen _lastScreen = (Screen)(-1);
	// The screen an overlay (options, pause, the load list) opened over, which keeps its place for the return.
	private Screen _beforeOverlay;
	private bool _questOverlay;
	private bool _logOverlay;
	private bool _pause;
	private bool _optionsOverlay;
	private bool _loadOverlay;
	private bool _soundTestOverlay;
	private int _cursor;
	private int _tab;
	private string? _pickMember;
	private string? _pickAction;
	private readonly ScrollList _list = new();
	private readonly ScrollList _sub = new();
	private readonly ConfirmPrompt _confirm = new();
	// The profile's text size as a factor, read at each draw.
	private float _scale = 1f;
	// The row under the cursor of the list that had it in the last frame, and that list's first row shown.
	private string _probeSelected = "";
	private int _probeTop;
	// The Equip page's comparison and the description shown for the highlighted row, as the last frame drew them.
	private string _probeCompare = "-";
	private string _probeDescription = "-";

	// `controlsPath` is where the bindings load from and save to; null keeps the defaults and saves nothing. Without
	// `audio` nothing plays and the calls are only recorded. `assetDir` is the asset directory, whose files replace the
	// generated pictures and sounds they name; problems in it are logged and the generated ones stand in. `dev` turns the
	// developer tools on; they reload and preview from `dataDir`. `battleLayout` overrides the presentation table's.
	public SampleController(GameSession session, GameData data, Action<string>? log = null, string? controlsPath = null, IAudioOut? audio = null, string? assetDir = null,
		bool dev = false, string? dataDir = null, BattleLayout? battleLayout = null)
	{
		_layoutOverride = battleLayout;
		_s = session;
		_dev = dev;
		_dataDir = dataDir;
		_data = data;
		_log = log ?? (_ => { });
		_audio = audio ?? new RecordingAudio();
		_actorIds = [.. session.ActorIds()];
		ControlsPath = controlsPath;
		_bindings = Bindings.Load(controlsPath, _log);
		_assetDir = assetDir;
		if (assetDir is not null)
		{
			_assets = Assets.Load(assetDir, data);
			LogAssetProblems();
			_audio.UseAssets(_assets.Manifest);
		}
	}

	public string? AssetDirectory => _assetDir;

	private BattlePresentation PresentationTable => _assets?.Battle ?? (_defaultPresentation ??= BattlePresentation.Defaults());
	private BattlePresentation? _defaultPresentation;

	private void LogAssetProblems()
	{
		foreach (AssetProblem problem in _assets!.Problems)
		{
			_log("assets: " + problem);
		}
	}

	// The manifest and every file again, keeping the session and the screens where they are: pictures from the next
	// draw, sounds from the next time each plays, the battle presentation table from the next round.
	public void ReloadAssets()
	{
		if (_assetDir is null)
		{
			_log("assets: no asset directory to reload");
			return;
		}
		Assets before = _assets!;
		_assets = Assets.Load(_assetDir, _data);
		List<string> changed = [.. _assets.ChangedSince(before)];
		_log($"assets: reloaded, {(changed.Count == 0 ? "nothing changed" : "changed: " + string.Join(", ", changed))}");
		LogAssetProblems();
		_audio.UseAssets(_assets.Manifest);
		_rebuildArt = true;
	}

	// Whether the picture or sound for a key (see Art.FromFile; sounds are `audio:KIND:ID`) came from a file.
	public bool AssetFromFile(string key)
	{
		if (key.StartsWith("audio:", StringComparison.Ordinal) && key.Split(':') is [_, string kind, string id])
		{
			return _assets?.Manifest.Audio.Any(a => a.Kind.ToString().Equals(kind, StringComparison.OrdinalIgnoreCase) && a.Id == id) == true;
		}
		return _art?.FromFile(key) == true;
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
			if (_optionsOverlay)
			{
				return Screen.Options;
			}
			if (_pause && (_battle.Active || _s.Mode == Mode.Field))
			{
				return Screen.Pause;
			}
			if (_battle.Active)
			{
				return Screen.Battle;
			}
			return _s.Mode switch
			{
				Mode.Title when _loadOverlay => Screen.LoadGame,
				Mode.Title when _soundTestOverlay => Screen.SoundTest,
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
					MenuKind.Port => Screen.Travel,
					MenuKind.Field when _travelPick is not null => Screen.Travel,
					_ => Screen.Menu,
				},
				_ => Screen.Title,
			};
		}
	}

	private static bool IsOverlay(Screen screen) => screen is Screen.Options or Screen.Pause or Screen.LoadGame or Screen.SoundTest;

	private int Px(int value) => (int)MathF.Round(value * _scale);

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
			_refused = true;
		}
	}

	private void TakeNotices()
	{
		foreach (Notice n in _s.DrainNotices())
		{
			if (_battle.Active)
			{
				AddBattleNotice(n);
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
				if (NoticeCue(n) is { } cue)
				{
					_audio.PlayCue(cue);
				}
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
		// Outside an event nothing is scripted: party members walk back into line, poses stop, panels and the scene's
		// camera go, and the area's music comes back.
		if (_s.Mode == Mode.Field)
		{
			_field.EndScene(Lineup());
		}
		else if (_s.Mode == Mode.Title)
		{
			_field.ClearScene();
		}
		if (_s.Mode is Mode.Field or Mode.Title)
		{
			_panels.Clear();
			_camera = null;
			_cameraMove = null;
			_shake = null;
			_scriptMusic = null;
			_scriptAmbient = null;
		}
	}

	// The party's area's tiles, for the pictures menus show of its objects.
	private Art.TileSet AreaTiles(Art art) => _s.FieldInfo() is { } f ? art.Tiles(f.AreaKind, f.AreaId) : art.Tiles(Theme.Field);

	private List<string> Lineup() => _s.Party().Where(m => m.InLineup).Select(m => m.Id).ToList();

	private void ResetUi()
	{
		_cursor = 0;
		_tab = 0;
		_pickMember = null;
		_pickAction = null;
		_list.Reset();
		_sub.Reset();
		_confirm.Close();
		_pause = _optionsOverlay = _loadOverlay = _soundTestOverlay = false;
		_soundTestTrack = "";
		_clearRecord = null;
		ResetMenuPages();
		_nameEntry.Reset();
	}

	public void Update(int dtMs, InputState raw)
	{
		// F5 reloads the asset directory, like F11 a key of the window's own outside the bindings.
		if (raw.Pressed(Microsoft.Xna.Framework.Input.Keys.F5))
		{
			ReloadAssets();
		}
		if (_dev && !DevUpdate(ref dtMs, raw))
		{
			return;
		}
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
		if (_battle.Active && _stage is null && _s.Mode == Mode.Title && _s.BattleInfo() is null)
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
			if (IsOverlay(screen))
			{
				if (!IsOverlay(_lastScreen))
				{
					_beforeOverlay = _lastScreen;
				}
			}
			else if (!IsOverlay(_lastScreen) || screen != _beforeOverlay)
			{
				ResetUi();
			}
			_lastScreen = screen;
		}
		if (Capturing is { } capturing)
		{
			Capture(capturing, raw);
		}
		else if (_confirm.Question is not null)
		{
			_confirm.Update(input);
		}
		else
		{
			UpdateScreen(screen, dtMs, input);
		}
		TakeNotices();
		UpdateAudio(dtMs, screen, Screen, input);
	}

	private void UpdateScreen(Screen screen, int dtMs, ActionInput input)
	{
		if (screen != Screen.Field)
		{
			_clickWalk = null;
		}
		switch (screen)
		{
		case Screen.Title: UpdateTitle(input); break;
		case Screen.Field: UpdateField(dtMs, input); break;
		case Screen.Dialogue: UpdateDialogue(dtMs, input); break;
		case Screen.Battle when _stage is not null: UpdateStagePreview(dtMs, input); break;
		case Screen.Battle: UpdateBattle(dtMs, input); break;
		case Screen.Menu: UpdateMenu(input); break;
		case Screen.Shop: UpdateShop(input); break;
		case Screen.Inn: UpdateInn(input); break;
		case Screen.SavePrompt: UpdateSavePrompt(input); break;
		case Screen.NameEntry: UpdateNameEntry(input); break;
		case Screen.Options: UpdateOptions(input); break;
		case Screen.Pause: UpdatePause(input); break;
		case Screen.LoadGame: UpdateLoadGame(input); break;
		case Screen.SoundTest: UpdateSoundTest(input); break;
		case Screen.Travel: UpdateTravel(input); break;
		case Screen.QuestLog:
		case Screen.TextLog:
			if (input.Back || input.Confirm || input.Pressed(GameAction.QuestLog) || input.Pressed(GameAction.TextLog))
			{
				_questOverlay = _logOverlay = false;
			}
			else if (screen == Screen.TextLog)
			{
				ScrollTextLog(input);
			}
			else
			{
				_list.MoveView(input, QuestRows().Count);
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

	private void DrawHint(IDrawSurface s, string hint)
	{
		int h = Px(30);
		s.Fill(0, s.Height - h, s.Width, h, Color.Black * 0.6f);
		hint = Style.Fit(s, hint, s.Width - 32, Style.Small);
		s.Text(hint, (s.Width - s.Measure(hint, Style.Small)) / 2, s.Height - h + Px(5), Style.Dim, Style.Small);
	}

	// The field menu, shop, logs, save prompt and name entry share this: the field dimmed behind a window.
	private void DrawBackdrop(IDrawSurface s, Art art)
	{
		if (DrawField(s is ScaledSurface scaled ? scaled.Inner : s, art))
		{
			s.Fill(0, 0, s.Width, s.Height, Color.Black * 0.55f);
		}
	}

	// The newest notices as small boxes stacked up from `bottom`, newest lowest, fading as they expire; with `right`,
	// `x` is the margin from the right edge, so they stay clear of a menu's lists.
	private void DrawToasts(IDrawSurface s, int x, int bottom, int most = 4, bool right = false)
	{
		s = s is ScaledSurface scaled ? scaled.Inner : s;
		int y = bottom;
		for (int i = _ticker.Count - 1; i >= Math.Max(0, _ticker.Count - most); i--)
		{
			(string text, int msLeft) = _ticker[i];
			float alpha = Math.Clamp(msLeft / 400f, 0f, 1f);
			string shown = Style.Fit(s, text, s.Width - 2 * x - 28, Style.Small);
			y -= 38;
			int boxW = s.Measure(shown, Style.Small) + 28;
			int bx = right ? s.Width - x - boxW : x;
			Style.Box(s, bx, y, boxW, 32, alpha * 0.95f);
			s.Text(shown, bx + 14, y + 7, Style.Text * alpha, Style.Small);
		}
	}

	// The description of a highlighted row, at most two small lines; the session gives "" when the data has none.
	private void DrawDescription(IDrawSurface s, string? text, int x, int y, int w)
	{
		_probeDescription = string.IsNullOrEmpty(text) ? "-" : text;
		int ly = y;
		foreach (string l in Style.Wrap(s, string.IsNullOrEmpty(text) ? T("ui.common.no_description") : text, w, Style.Small).Take(2))
		{
			s.Text(l, x, ly, string.IsNullOrEmpty(text) ? Style.Dim : Style.Text, Style.Small);
			ly += Px(22);
		}
	}

	private string? DescriptionOf(InventoryEntry entry) => _s.Description(entry.IsEquipment ? DescriptionKind.Equipment : DescriptionKind.Item, entry.Id);

	// Text rows in a box, scrolling when they do not fit; the list with the cursor is the one headless runs probe.
	private void DrawItems(IDrawSurface s, ScrollList list, int x, int y, int w, int h, IReadOnlyList<string> items, bool active = true,
		IReadOnlyList<bool>? enabled = null, int size = Style.Body)
	{
		int rowH = Px(size + 10);
		list.Draw(s, x, y, w, h, rowH, items.Count,
			(i, ry, on) => Style.ListRow(s, Style.Fit(s, items[i], w - 40, size), x, ry, w - 14, on, enabled is null || enabled[i], size, Px(size + 8)), active);
		if (active)
		{
			_probeSelected = items.Count > 0 ? items[list.Cursor] : "";
			_probeTop = list.Top;
		}
	}

	public void Draw(IDrawSurface s)
	{
		if (_dev && _preview is { } preview)
		{
			preview.Controller.Draw(s);
			DrawDev(s, preview.Controller._art!);
			return;
		}
		if (_art is null || _rebuildArt)
		{
			if (_rebuildArt)
			{
				s.ClearImages();
				// Unknown, so the font is set again even when the reloaded skin names none.
				_font = "?";
			}
			_rebuildArt = false;
			_art = new Art(s, _assets?.Manifest);
			Style.Skin = _art.Skin;
		}
		Art art = _art;
		// The text language's font from the UI skin, switched only when it changes.
		FontEntry? font = _assets?.Manifest.Ui is { Enabled: true } ui ? ui.Fonts.GetValueOrDefault(_s.Profile().TextLanguage) : null;
		string? fontKey = font is null ? null : _assets!.Manifest.Full(font.File) + "@" + font.Scale.ToString(CultureInfo.InvariantCulture);
		if (fontKey != _font && !(fontKey is null && _font == ""))
		{
			s.UseFont(font is null ? null : _assets!.Manifest.Full(font.File), font?.Scale ?? 1f);
			_font = fontKey;
		}
		_scale = _s.Profile().TextSizePercent / 100f;
		IDrawSurface t = _scale == 1f ? s : new ScaledSurface(s, _scale);
		_probeSelected = "";
		_probeTop = 0;
		_probeCompare = "-";
		_probeDescription = "-";
		_dialogueAt = "-";
		_overlayDrawn.Clear();
		s.Fill(0, 0, s.Width, s.Height, Style.Background);
		switch (Screen)
		{
		case Screen.Title: DrawTitle(t, art); break;
		case Screen.Field:
			DrawField(s, art);
			DrawHud(s);
			_field.DrawBanner(s, AreaKindText);
			DrawToasts(s, 16, s.Height - 40);
			bool aboard = _s.FieldInfo()?.Aboard.Length > 0;
			DrawHint(s, T("ui.hint.field", Walk(), T(aboard ? "ui.hint.drive" : "ui.hint.walk"), Hint(GameAction.Confirm), T(aboard ? "ui.hint.step_off" : "ui.hint.talk"),
				Hint(GameAction.Menu), Hint(GameAction.QuestLog), Hint(GameAction.PartyTalk), Hint(GameAction.TextLog), Hint(GameAction.Pause)));
			break;
		case Screen.Dialogue: DrawDialogue(s, t, art); break;
		case Screen.Battle: DrawBattle(s, art); break;
		case Screen.Menu:
			DrawMenu(t, art);
			DrawToasts(s, 48, s.Height - 92, right: true);
			break;
		case Screen.Shop: DrawShop(t, art); break;
		case Screen.Inn: DrawInn(s, t, art); break;
		case Screen.SavePrompt: DrawSavePrompt(t, art); break;
		case Screen.NameEntry: DrawNameEntry(t, art); break;
		case Screen.Options: DrawOptions(t, art); break;
		case Screen.Pause: DrawPause(s, t, art); break;
		case Screen.LoadGame: DrawLoadGame(t, art); break;
		case Screen.SoundTest: DrawSoundTest(t, art); break;
		case Screen.Travel:
			DrawTravel(t, art);
			DrawToasts(s, 48, s.Height - 92, right: true);
			break;
		case Screen.QuestLog:
			DrawBackdrop(t, art);
			Style.Window(t, 24, 40, s.Width - 48, s.Height - 72, T("ui.quest_log.title"));
			DrawQuestLog(t, 56, 72, s.Width - 112, s.Height - 120);
			break;
		case Screen.TextLog:
			DrawBackdrop(t, art);
			Style.Window(t, 24, 40, s.Width - 48, s.Height - 72, T("ui.text_log.title"));
			DrawTextLog(t, 56, 72, s.Width - 112, s.Height - 140);
			break;
		}
		_confirm.Draw(t, Px, T("ui.confirm.title"), T("ui.confirm.yes"), T("ui.confirm.no"));
		DrawAnimations(s);
		if (_dev)
		{
			DrawDev(s, art);
		}
	}

	// `expect state KEY VALUE` in headless scripts reads these: where the screens stand and what the session holds.
	public string State(string key)
	{
		static string OnOff(bool on) => on ? "on" : "off";
		Profile p = _s.Profile();
		Settings settings = _s.Settings();
		return key switch
		{
			"tab" => Screen == Screen.Menu ? MenuTabs[_tab] : "-",
			"page" => Page(),
			"selected" => _probeSelected,
			"top" => _probeTop.ToString(CultureInfo.InvariantCulture),
			"compare" => _probeCompare,
			"walk" => _clickWalk is { } walkGoal ? $"{walkGoal.X} {walkGoal.Y}" : "-",
			"itemsort" => GameSession.NameOf(NameKind.ItemSort, (int)_itemSort),
			"description" => _probeDescription,
			"prompt" => _confirm.Question ?? "none",
			"buffer" => Screen == Screen.NameEntry ? _nameEntry.Buffer : _macroName.Buffer,
			"cell" => Screen == Screen.NameEntry ? _nameEntry.Cell : _macroName.Cell,
			"line" => _s.Line()?.Text ?? "-",
			"speaker" => _s.Line() is { } spoken ? (spoken.SpeakerName.Length > 0 ? spoken.SpeakerName : "none") : "-",
			"conveyors" => $"{_field.RunningConveyors}/{_field.DrawnConveyors}",
			"reveal" => _s.Line() is { } line ? $"{Revealed(line.Text)}/{line.Text.Length}" : "-",
			"party" => string.Join(", ", _s.Party().Where(m => m.InLineup).Select(m => m.Name)),
			"macros" => string.Join(", ", _s.MacroList().Select(m => $"{m.Name}({m.Steps.Count})" + (m.Name == settings.AutoMacro ? "*" : ""))),
			"area" => _s.FieldInfo()?.AreaName ?? "-",
			"aboard" => _s.FieldInfo() is { Aboard.Length: > 0 } aboard ? aboard.Aboard : "-",
			"vehicles" => _field.DrawnVehicles.Count == 0 ? "none" : string.Join(", ", _field.DrawnVehicles.Select(v => $"{v.Key} {v.Value}")),
			"destinations" => TravelProbe(),
			"panels" => _panels.Count == 0 ? "none" : string.Join(", ", _panels.Select(x => $"{x.Id} {x.Slot} {x.Camera}" + (x.Transition is null ? "" : "~"))),
			"camera" => (_camera ?? "-") + (_cameraMove is null ? "" : "~"),
			"paneltiles" => _panels.Count == 0 ? "none" : string.Join(", ", _panels.Select(x => $"{x.Id} {PanelTileSize(x)}")),
			"shake" => ShakeAmplitude().ToString(CultureInfo.InvariantCulture),
			"dialogue" => _dialogueAt,
			"music" => _audio.Music.Length == 0 ? "-" : _audio.Music,
			"follower" => _s.Follower() is { Length: > 0 } follower ? follower : "-",
			"ambient" => _audio.Ambient.Length == 0 ? "-" : _audio.Ambient,
			"text_speed" => GameSession.NameOf(NameKind.TextSpeed, (int)p.TextSpeed),
			"auto_advance" => OnOff(p.AutoAdvance),
			"text_size" => p.TextSizePercent.ToString(CultureInfo.InvariantCulture),
			"battle_speed" => GameSession.NameOf(NameKind.BattleSpeed, (int)p.BattleSpeed),
			"reduced_flash" => OnOff(p.ReducedFlash),
			"ambient_talk" => OnOff(p.AmbientTalk),
			"shipped_names" => OnOff(p.ShowShippedNames),
			"language" => p.TextLanguage,
			"volumes" => $"{p.MusicVolume} {p.AmbientVolume} {p.EffectsVolume}",
			"difficulty" => settings.Difficulty,
			"reward" => GameSession.NameOf(NameKind.RewardMultiplier, (int)settings.RewardMultiplier),
			"auto_macro" => settings.AutoMacro.Length == 0 ? "-" : settings.AutoMacro,
			_ when key.StartsWith("assetfile.", StringComparison.Ordinal) => _art?.FileOf(key["assetfile.".Length..]) ?? "-",
			_ when key.StartsWith("macro.", StringComparison.Ordinal) => _s.MacroList().FirstOrDefault(m => m.Name == key[6..]) is { } macro
				? string.Join(" / ", macro.Steps.Select(StepText))
				: "-",
			_ => DevState(key) is var dev && dev != "?" ? dev : BattleState(key),
		};
	}
}
