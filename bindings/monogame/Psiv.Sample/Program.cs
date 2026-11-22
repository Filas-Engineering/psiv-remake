using System.Globalization;

using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

using Psiv.Session;
using Psiv.Session.Scripting;

namespace Psiv.Sample;

public static class Program
{
	private const string Usage =
		"Psiv.Sample: plays the placeholder adventure through libpsiv_session\n" +
		"\n" +
		"  Psiv.Sample [--data DIR] [--saves DIR] [--seed N] [--font FILE] [--exit-after-ms N] [--screenshot FILE.png] [--mute] [--window-size WxH] [--fullscreen]\n" +
		"              [--assets DIR] [--watch-assets] [--dev] [--battle-layout front|side] [--language CODE]\n" +
		"  Psiv.Sample --window --script FILE ...   the window, with keys from the script instead of the keyboard\n" +
		"              [--realtime]   the real-time field input and the window's own clock instead of 16 ms frames\n" +
		"              [--play-defaults]   a scripted run starts a new profile as normal play does: flashing reduced, with the title's notice\n" +
		"              [--pacing-log FILE]   each drawn frame's wall time and the leader's drawn position, for frame pacing\n" +
		"  Psiv.Sample --script FILE --frames N [--data DIR] [--saves DIR] [--seed N] [--assets DIR] [--frame-digests FILE] [--language CODE] [--strict-text]\n" +
		"              headless, no window, no sound\n" +
		"  Psiv.Sample --dump-audio DIR   writes a generated track and three cues as WAV files and checks every generated sound\n" +
		"  Psiv.Sample --check-assets [--assets DIR]   checks the asset directory's manifest and files against the data\n" +
		"  Psiv.Sample --export-placeholders DIR   writes every generated picture and sound and a manifest that reproduces them\n" +
		"  Psiv.Sample --capabilities   prints, as JSON, every asset, layout, animation and script name this build accepts\n" +
		"  Psiv.Sample --version   prints the library version\n" +
		"\n" +
		"The asset directory defaults to assets/ beside the data directory, when there is one; F5 in the window reloads it.\n" +
		"--language sets the profile's text language at start; --strict-text fails a headless run on a text key that language lacks.\n" +
		"--dev turns on the developer tools (docs/dev-tools.md): ` console, F6 data reload, F2-F4 and F7-F10 overlays, F12 previewer,\n" +
		"[ ] speed, \\ pause, . step.\n" +
		"\n" +
		"Headless script lines: press KEY [COUNT] | hold KEY FRAMES | wait FRAMES | pressuntil KEY SCREEN MAXFRAMES |\n" +
		"pad BUTTON [COUNT] | padhold BUTTON FRAMES | paduntil BUTTON SCREEN MAXFRAMES | padstick X Y FRAMES |\n" +
		"hold KEY+KEY FRAMES | padhold BUTTON+BUTTON FRAMES | fieldinput realtime|classic | trace PROBE[,PROBE]|off |\n" +
		"controls list|reset|save|restart | controls capture|clear ACTION | controls write JSON |\n" +
		"cmd GAME_CLI_COMMAND | expect screen NAME | expect mode NAME | expect actor ID X Y [FACING]|walking|hidden | expect pose ID POSE [marked] |\n" +
		"expect label none | expect toast none|TEXT | expect notoast TEXT | expect binding ACTION KEYS \"|\" BUTTONS |\n" +
		"expect state KEY VALUE | expect statehas KEY TEXT | expect audio TEXT | expect noaudio TEXT | audio mark|log |\n" +
		"savefile damage SLOT | click X Y | clickcell X Y | expect letterbox W H X Y WIDTH HEIGHT PERCENT | window W H | window fullscreen |\n" +
		"assets reload | assets swap FILE WITH | expect asset KEY file|generated | present mark | expect present STEP \"|\" ... | expect presenthas TEXT |\n" +
		"waitstate MAXFRAMES KEY VALUE | waitstatehas MAXFRAMES KEY TEXT | dev CONSOLE_COMMAND | devrefused CONSOLE_COMMAND | datafile swap FILE WITH |\n" +
			"battlelayout front|side|data | animlog mark | expect animlog ENTRY \"|\" ... | expect animloghas TEXT |\n" +
		"dump | shot FILE.png (window only) | # comment.\n" +
		"Keys are MonoGame Keys names, buttons MonoGame Buttons names, actions the sample's GameAction names.\n" +
		"Exit codes: 0 done, 1 a failure, 2 bad arguments.\n";

	public static int Main(string[] args)
	{
		string? data = null;
		string? saves = null;
		string? font = null;
		string? script = null;
		int frames = 0;
		int exitAfterMs = 0;
		ulong seed = 1;
		bool window = false;
		bool mute = false;
		bool fullscreen = false;
		(int, int)? windowSize = null;
		string? screenshot = null;
		string? dumpAudio = null;
		string? assets = null;
		string? export = null;
		string? digests = null;
		bool checkAssets = false;
		bool capabilities = false;
		bool watchAssets = false;
		bool dev = false;
		bool realtime = false;
		bool playDefaults = false;
		string? pacingLog = null;
		BattleLayout? battleLayout = null;
		string? language = null;
		bool strictText = false;
		for (int i = 0; i < args.Length; i++)
		{
			string? Next() => i + 1 < args.Length ? args[++i] : null;
			switch (args[i])
			{
			case "--data": data = Next(); break;
			case "--saves": saves = Next(); break;
			case "--font": font = Next(); break;
			case "--window": window = true; break;
			case "--mute": mute = true; break;
			case "--fullscreen": fullscreen = true; break;
			case "--window-size" when Next()?.Split('x') is [string ws, string hs] && int.TryParse(ws, CultureInfo.InvariantCulture, out int sw) &&
				int.TryParse(hs, CultureInfo.InvariantCulture, out int sh):
				windowSize = (sw, sh);
				break;
			case "--dump-audio": dumpAudio = Next(); break;
			case "--assets": assets = Next(); break;
			case "--check-assets": checkAssets = true; break;
			case "--capabilities": capabilities = true; break;
			case "--watch-assets": watchAssets = true; break;
			case "--dev": dev = true; break;
			case "--realtime": realtime = true; break;
			case "--play-defaults": playDefaults = true; break;
			case "--pacing-log": pacingLog = Next(); break;
			case "--battle-layout" when BattlePresentation.ParseLayout(Next()) is { } layout: battleLayout = layout; break;
			case "--language": language = Next(); break;
			case "--strict-text": strictText = true; break;
			case "--export-placeholders": export = Next(); break;
			case "--frame-digests": digests = Next(); break;
			case "--screenshot": screenshot = Next(); break;
			case "--script": script = Next(); break;
			case "--frames" when int.TryParse(Next(), CultureInfo.InvariantCulture, out int n): frames = n; break;
			case "--exit-after-ms" when int.TryParse(Next(), CultureInfo.InvariantCulture, out int n): exitAfterMs = n; break;
			case "--seed" when ulong.TryParse(Next(), CultureInfo.InvariantCulture, out ulong n): seed = n; break;
			case "--help" or "-h":
				Console.Write(Usage);
				return 0;
			case "--version":
				Console.WriteLine(GameSession.LibraryVersion);
				return 0;
			default:
				Console.Error.Write(Usage);
				return 2;
			}
		}
		if (dumpAudio is not null)
		{
			return DumpAudio(dumpAudio);
		}
		data ??= FindData();
		if (data is null)
		{
			Console.Error.WriteLine("cannot find game/data; pass --data DIR");
			return 2;
		}
		assets ??= Path.Combine(Path.GetDirectoryName(Path.GetFullPath(data))!, "assets") is var beside && Directory.Exists(beside) ? beside : null;
		if (checkAssets && assets is null)
		{
			Console.Error.WriteLine("no asset directory to check: pass --assets DIR");
			return 2;
		}
		if (script is not null && !window && frames <= 0)
		{
			Console.Error.WriteLine("--script needs --frames N");
			return 2;
		}
		if (script is null || window)
		{
			saves ??= Path.Combine(AppContext.BaseDirectory, "saves");
		}
		if (saves is not null)
		{
			Directory.CreateDirectory(saves);
		}

		// A new profile in normal play (no script, or --play-defaults) starts at the data's flashing default; scripted runs keep the
		// profile's default, standard, so their transcripts do not depend on it.
		bool newProfile = saves is not null && !File.Exists(Path.Combine(saves, "profile.cfg"));
		GameSession session;
		try
		{
			session = GameSession.Create(new SessionOptions { DataDirectory = data, SaveDirectory = saves, Seed = seed, WallClock = script is null, Presentation = Presentation.Wait });
		}
		catch (PsivException e)
		{
			Console.Error.WriteLine(e.Error.Text);
			return 1;
		}
		using (session)
		{
			if (language is not null)
			{
				if (!session.GameInfo().Languages.Contains(language))
				{
					Console.Error.WriteLine($"the data has no text language {language}; it has {string.Join(", ", session.GameInfo().Languages)}");
					return 2;
				}
				if (session.SetProfile(session.Profile() with { TextLanguage = language }).Error is { } error)
				{
					Console.Error.WriteLine(error.Text);
					return 1;
				}
			}
			GameData gameData = GameData.Load(session, data);
			if (newProfile && (script is null || playDefaults))
			{
				SampleController.NewProfile = true;
			}
			if (capabilities)
			{
				Console.Write(Capabilities.Report(gameData));
				return 0;
			}
			if (checkAssets)
			{
				return CheckAssets(assets!, gameData);
			}
			if (export is not null)
			{
				return PlaceholderExport.Run(export, gameData);
			}
			if (script is not null && !window)
			{
				// PSIV_DIGEST_DIR keeps every run's digests for a before-and-after parity check, named by the arguments so the
				// same make target writes the same file.
				if (digests is null && Environment.GetEnvironmentVariable("PSIV_DIGEST_DIR") is { Length: > 0 } digestDir)
				{
					Directory.CreateDirectory(digestDir);
					byte[] hash = System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(string.Join('\n', args)));
					digests = Path.Combine(digestDir, $"{Path.GetFileNameWithoutExtension(script)}-{Convert.ToHexString(hash, 0, 6)}.txt");
				}
				return Headless.Run(session, gameData, script, frames, saves, assets, digests, dev, data, battleLayout, strictText);
			}
			try
			{
				using var game = new SampleGame(session, gameData, font, exitAfterMs, saves, script, screenshot, mute, windowSize, fullscreen, assets, watchAssets, dev, data, battleLayout,
					realtime, pacingLog);
				game.Run();
				return game.Failures == 0 ? 0 : 1;
			}
			catch (Exception e)
			{
				Console.Error.WriteLine("the window could not run: " + e);
				return 1;
			}
		}
	}

	// The asset counterpart of game_cli --check: every problem with its file, entry and reason, then what loaded.
	private static int CheckAssets(string dir, GameData data)
	{
		Assets assets = Assets.Load(dir, data);
		foreach (AssetProblem problem in assets.Problems)
		{
			Console.WriteLine("assets: " + problem);
		}
		AssetManifest m = assets.Manifest;
		Console.WriteLine($"assets: {m.Tilesets.Count} tilesets, {m.Characters.Count} characters, {m.Portraits.Count} portraits, {m.Figures.Count} figures, " +
			$"{m.FieldEnemies.Count} field enemies, {m.Vehicles.Count} vehicles, {m.Backdrops.Count} backdrops, {m.Effects.Count} effects, {m.Sprites.Count} sprites, {m.StatusIcons.Count} status icons, " +
			$"{(m.Ui is null ? "no UI skin" : m.Ui.Enabled ? "a UI skin" : "a UI skin turned off")}, {m.Audio.Count} sounds, " +
			$"{(m.BattlePresentation is null ? "the built-in battle presentation" : "a battle presentation table")}, {assets.Animation.Summary()}, " +
			$"{(m.MapsEnabled ? $"{m.Maps.Count} area pictures" : "area pictures turned off")}, {m.Objects.Count} objects, {m.VehicleOverlays.Count} vehicle overlays" +
			(m.BitmapFontsEnabled switch { true => $", {m.BitmapFonts.Count} bitmap fonts", false => ", bitmap fonts turned off", null => "" }) +
			(m.BackdropMaps.Count > 0 ? $", {m.BackdropMaps.Count} backdrop maps" : "") + (m.TitleArt is null ? "" : ", title art") +
			(m.CommandIcons.Count > 0 ? $", {m.CommandIcons.Count} command icons" : "") + (m.Pictures.Count > 0 ? $", {m.Pictures.Count} pictures" : "") +
			(m.Keepers.Count > 0 ? $", {m.Keepers.Count} keepers" : "") + (m.AreaMotions.Count > 0 ? $", {m.AreaMotions.Count} areas with motion" : "") +
			(m.Tilesets.Count(t => t.Palettes is { Count: > 0 }) is int recolored and > 0 ? $", {recolored} recolored tilesets" : "") +
			(assets.Layout.IsDefault ? m.UiLayoutEnabled == false ? ", layouts turned off" : "" : $", {assets.Layout.FromFiles.Count} layout files ({assets.Layout.Styles.Width}x{assets.Layout.Styles.Height})"));
		int problems = assets.Problems.Count;
		Console.WriteLine(problems == 0 ? "OK: no problems." : $"FAILED: {problems} problems.");
		return problems == 0 ? 0 : 1;
	}

	// One generated track and three cues as WAV files, then the synthesizer's self-check over every sound the sample
	// and the placeholder scripts name, the placeholder data's tracks, and the tracks derived where data names none.
	private static int DumpAudio(string dir)
	{
		Directory.CreateDirectory(dir);
		(SoundKind Kind, string Id)[] dumped = [(SoundKind.Music, "town_tidewater"), (SoundKind.Cue, "confirm"), (SoundKind.Cue, "hit"), (SoundKind.Cue, "level_up")];
		foreach ((SoundKind kind, string id) in dumped)
		{
			short[] pcm = Synth.Generate(kind, id);
			string path = Path.Combine(dir, $"{kind.ToString().ToLowerInvariant()}_{id}.wav");
			Synth.WriteWav(path, pcm);
			Console.WriteLine($"audio: {path}, {pcm.Length} samples ({pcm.Length * 1000 / Synth.Rate} ms), peak {pcm.Max(v => Math.Abs((int)v))}");
		}
		List<(SoundKind, string)> all = Synth.SampleCues.Select(c => (SoundKind.Cue, c))
			.Concat(new[] { "title", "town_tidewater", "field_reed_path", "dungeon_bell_cove", "battle_field", "battle_dungeon", "battle_scripted", "brass_alarm", "reed_lookout",
				"title_harbor_dawn", "town_tide_bells", "field_marsh_reeds", "dungeon_kiln_halls", "battle_marsh_ambush", "battle_brass_warden", "victory_bright_sail",
				"defeat_low_tide" }
				.Select(t => (SoundKind.Music, t)))
			.Append((SoundKind.Ambient, "marsh_wind"))
			.Append((SoundKind.Cue, "lookout_creak"))
			.ToList();
		List<string> problems = Synth.Check(all);
		foreach (string problem in problems)
		{
			Console.WriteLine("audio check: " + problem);
		}
		Console.WriteLine($"audio check: {all.Count} sounds generated twice each, {problems.Count} problems");
		return problems.Count == 0 ? 0 : 1;
	}

	private static string? FindData()
	{
		// A macOS app bundle keeps the binary in Contents/MacOS and the data in Contents/Resources.
		string bundled = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "Resources", "game", "data"));
		if (File.Exists(Path.Combine(bundled, "game.json")))
		{
			return bundled;
		}
		foreach (string start in new[] { AppContext.BaseDirectory, Directory.GetCurrentDirectory() })
		{
			for (DirectoryInfo? dir = new(start); dir is not null; dir = dir.Parent)
			{
				string candidate = Path.Combine(dir.FullName, "game", "data");
				if (File.Exists(Path.Combine(candidate, "game.json")))
				{
					return candidate;
				}
			}
		}
		return null;
	}
}

// The keys and gamepad buttons held for one frame.
public readonly record struct InputFrame(Keys[] Keys, Buttons[] Buttons, (int X, int Y)? Click = null)
{
	public static readonly InputFrame None = new([], []);
}

// Plays an input script frame by frame: each call to Next gives the keys and buttons held for one frame. Lines
// that take no frames (cmd, controls, expect, dump) run between frames.
public sealed class ScriptPlayer
{
	private readonly GameSession _session;
	private readonly Func<SampleController>? _restart;
	private SampleController _controller;
	private readonly ScriptRunner _runner;
	private readonly StringWriter _commandOutput = new();
	private readonly Func<IReadOnlyList<string>> _frameText;
	private readonly Func<int> _frame;
	private readonly Action<string>? _shot;
	private readonly string? _saves;
	private readonly Action<int, int, bool>? _resize;
	private readonly string _name;
	private readonly string[] _lines;
	private int _next;
	private IEnumerator<InputFrame>? _action;
	private string? _pendingFailure;
	private int _audioMark;
	private int _presentMark;
	// Probes printed after every frame (`trace`), and the field input a restarted controller keeps.
	private string[] _trace = [];
	private bool _realtimeField;

	// `shot` saves the next drawn frame to a file; null (headless) skips shot lines. `restart` makes a new controller
	// for `controls restart`.
	public ScriptPlayer(GameSession session, SampleController controller, string path, Func<IReadOnlyList<string>> frameText, Func<int> frame,
		Action<string>? shot = null, Func<SampleController>? restart = null, string? saves = null, Action<int, int, bool>? resize = null)
	{
		_shot = shot;
		_resize = resize;
		_saves = saves;
		_restart = restart;
		_session = session;
		_controller = controller;
		_runner = new ScriptRunner(session, _commandOutput, autoBattle: false);
		_frameText = frameText;
		_frame = frame;
		_name = Path.GetFileName(path);
		_lines = File.ReadAllLines(path);
		_realtimeField = controller.RealtimeField;
		_action = NextAction();
	}

	public int Failures { get; private set; }

	public SampleController Controller => _controller;

	public bool Done => _action is null && _next >= _lines.Length;

	private void Fail(string message)
	{
		Failures++;
		Console.WriteLine($"[{_frame()}] FAIL: {message}");
	}

	private static IEnumerable<InputFrame> Press(InputFrame frame, int count)
	{
		for (int i = 0; i < count; i++)
		{
			yield return frame;
			yield return InputFrame.None;
		}
	}

	private static IEnumerable<InputFrame> Repeat(InputFrame frame, int count)
	{
		for (int i = 0; i < count; i++)
		{
			yield return frame;
		}
	}

	// The buttons a connected pad reports with its left stick at x, y (each -1 to 1, y up), through the same dead zone
	// as a real pad.
	// With the real-time field input the stick goes through the window's hysteresis, kept from one `padstick` to the next.
	private static InputFrame Stick(float x, float y, StickSnap? snap) =>
		new([], [.. Pad.ButtonsDown(new GamePadState(new GamePadThumbSticks(new Vector2(x, y), Vector2.Zero), new GamePadTriggers(), new GamePadButtons(), new GamePadDPad()), snap)]);

	private readonly StickSnap _snap = new();

	// `A+B`: keys or buttons held together, each by its MonoGame name.
	private static T[]? Several<T>(string names) where T : struct, Enum
	{
		var all = new List<T>();
		foreach (string name in names.Split('+'))
		{
			if (!Enum.TryParse(name, ignoreCase: true, out T value) || !Enum.IsDefined(value))
			{
				return null;
			}
			all.Add(value);
		}
		return [.. all];
	}

	// One frame with no keys, so the frame drawn is the one the script reached.
	private IEnumerable<InputFrame> Shot(string path)
	{
		_shot?.Invoke(path);
		yield return InputFrame.None;
	}

	private IEnumerable<InputFrame> PressUntil(InputFrame frame, Screen screen, int max, string source)
	{
		for (int i = 0; i < max; i += 2)
		{
			if (_controller.Screen == screen)
			{
				yield break;
			}
			yield return frame;
			yield return InputFrame.None;
		}
		if (_controller.Screen != screen)
		{
			_pendingFailure = $"{source}: still on {_controller.Screen} after {max} frames";
		}
	}

	private IEnumerator<InputFrame>? NextAction()
	{
		while (_next < _lines.Length)
		{
			string source = $"{_name}:{_next + 1}";
			string line = _lines[_next++];
			int hash = line.IndexOf('#');
			string[] w = (hash >= 0 ? line[..hash] : line).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
			if (w.Length == 0)
			{
				continue;
			}
			Keys key = Keys.None;
			bool keyOk = w.Length > 1 && Enum.TryParse(w[1], ignoreCase: true, out key);
			Buttons button = default;
			bool buttonOk = w.Length > 1 && Enum.TryParse(w[1], ignoreCase: true, out button) && Enum.IsDefined(button);
			GameAction action = default;
			bool actionOk = w.Length > 2 && Enum.TryParse(w[2], ignoreCase: true, out action) && Enum.IsDefined(action);
			// The verbs `--capabilities` reports are the only ones the player reads.
			if (!Capabilities.Verbs.Contains(w[0]) || w[0] == "expect" && (w.Length < 2 || !Capabilities.ExpectKinds.Contains(w[1])))
			{
				Fail($"{source}: cannot read '{line.Trim()}'");
				continue;
			}
			switch (w[0])
			{
			case "press" when keyOk:
				return Press(new([key], []), w.Length > 2 ? int.Parse(w[2], CultureInfo.InvariantCulture) : 1).GetEnumerator();
			case "hold" when w.Length == 3 && w[1].Contains('+') && Several<Keys>(w[1]) is { } keys:
				return Repeat(new(keys, []), int.Parse(w[2], CultureInfo.InvariantCulture)).GetEnumerator();
			case "padhold" when w.Length == 3 && w[1].Contains('+') && Several<Buttons>(w[1]) is { } buttons:
				return Repeat(new([], buttons), int.Parse(w[2], CultureInfo.InvariantCulture)).GetEnumerator();
			case "fieldinput" when w.Length == 2 && w[1] is "realtime" or "classic":
				_controller.RealtimeField = _realtimeField = w[1] == "realtime";
				break;
			case "trace" when w.Length == 2:
				_trace = w[1] == "off" ? [] : w[1].Split(',');
				break;
			case "hold" when keyOk && w.Length == 3:
				return Repeat(new([key], []), int.Parse(w[2], CultureInfo.InvariantCulture)).GetEnumerator();
			case "wait" when w.Length == 2:
				return Repeat(InputFrame.None, int.Parse(w[1], CultureInfo.InvariantCulture)).GetEnumerator();
			case "pressuntil" when keyOk && w.Length == 4 && Enum.TryParse(w[2], true, out Screen screen):
				return PressUntil(new([key], []), screen, int.Parse(w[3], CultureInfo.InvariantCulture), source).GetEnumerator();
			case "pad" when buttonOk:
				return Press(new([], [button]), w.Length > 2 ? int.Parse(w[2], CultureInfo.InvariantCulture) : 1).GetEnumerator();
			case "padhold" when buttonOk && w.Length == 3:
				return Repeat(new([], [button]), int.Parse(w[2], CultureInfo.InvariantCulture)).GetEnumerator();
			case "paduntil" when buttonOk && w.Length == 4 && Enum.TryParse(w[2], true, out Screen padScreen):
				return PressUntil(new([], [button]), padScreen, int.Parse(w[3], CultureInfo.InvariantCulture), source).GetEnumerator();
			case "padstick" when w.Length == 4 && float.TryParse(w[1], CultureInfo.InvariantCulture, out float stickX) &&
				float.TryParse(w[2], CultureInfo.InvariantCulture, out float stickY):
				return Repeat(Stick(stickX, stickY, _realtimeField ? _snap : null), int.Parse(w[3], CultureInfo.InvariantCulture)).GetEnumerator();
			case "click" when w.Length == 3 && int.TryParse(w[1], CultureInfo.InvariantCulture, out int clickX) &&
				int.TryParse(w[2], CultureInfo.InvariantCulture, out int clickY):
				return Press(new([], [], (clickX, clickY)), 1).GetEnumerator();
			case "clickcell" when w.Length == 3 && int.TryParse(w[1], CultureInfo.InvariantCulture, out int cellX) &&
				int.TryParse(w[2], CultureInfo.InvariantCulture, out int cellY):
				return Press(new([], [], _controller.CellCenter(cellX, cellY)), 1).GetEnumerator();
			case "expect" when w.Length == 9 && w[1] == "letterbox":
				int[] n = w[2..].Select(v => int.Parse(v, CultureInfo.InvariantCulture)).ToArray();
				Letterbox box = Letterbox.Fit(n[0], n[1], _controller.LogicalSize.Width, _controller.LogicalSize.Height, _controller.Scaling);
				if ((box.X, box.Y, box.Width, box.Height, (int)MathF.Round(box.Scale * 100)) != (n[2], n[3], n[4], n[5], n[6]))
				{
					Fail($"{source}: a {n[0]}x{n[1]} window letterboxes to {box.X} {box.Y} {box.Width} {box.Height} at {box.Scale * 100:0}%");
				}
				break;
			case "window" when w.Length == 3 && int.TryParse(w[1], CultureInfo.InvariantCulture, out int windowW) &&
				int.TryParse(w[2], CultureInfo.InvariantCulture, out int windowH):
				_resize?.Invoke(windowW, windowH, false);
				return Repeat(InputFrame.None, 2).GetEnumerator();
			case "window" when w.Length == 2 && w[1] == "fullscreen":
				_resize?.Invoke(0, 0, true);
				return Repeat(InputFrame.None, 2).GetEnumerator();
			case "controls" when w.Length >= 2:
				Controls(w, actionOk ? action : null, source);
				break;
			case "expect" when w.Length >= 4 && w[1] == "binding" && actionOk:
				ControlBinding row = _controller.Controls().First(r => r.Action == action);
				string bound = $"{row.KeyNames} | {row.ButtonNames}";
				string expected = string.Join(" ", w[3..]);
				if (bound != expected)
				{
					Fail($"{source}: {action} is bound to {bound}, expected {expected}");
				}
				break;
			case "cmd" when w.Length > 1:
				int before = _runner.Errors;
				_runner.Execute(string.Join(" ", w[1..]));
				foreach (string output in _commandOutput.ToString().Split('\n', StringSplitOptions.RemoveEmptyEntries))
				{
					Console.WriteLine($"[{_frame()}] cmd| {output}");
				}
				_commandOutput.GetStringBuilder().Clear();
				if (_runner.Errors != before)
				{
					Fail($"{source}: command failed");
				}
				break;
			case "expect" when w.Length == 3 && w[1] == "screen":
				if (!string.Equals(_controller.Screen.ToString(), w[2], StringComparison.OrdinalIgnoreCase))
				{
					Fail($"{source}: expected screen {w[2]}, on {_controller.Screen}");
				}
				break;
			case "expect" when w.Length == 3 && w[1] == "mode":
				if (!string.Equals(_session.Mode.ToString(), w[2], StringComparison.OrdinalIgnoreCase))
				{
					Fail($"{source}: expected mode {w[2]}, in {_session.Mode}");
				}
				break;
			case "expect" when w.Length >= 3 && w[1] == "actor":
				ExpectActor(w, source);
				break;
			case "expect" when (w.Length == 4 || (w.Length == 5 && w[4] == "marked")) && w[1] == "pose":
				_controller.DrawnActors.TryGetValue(w[2], out DrawnPerson person);
				if (!Enum.TryParse(w[3], true, out Pose pose))
				{
					Fail($"{source}: no pose named {w[3]}");
				}
				else if (!_controller.DrawnActors.ContainsKey(w[2]) || person.Pose != pose || (w.Length == 5 && !person.Marked))
				{
					Fail($"{source}: {w[2]} is not drawn in pose {w[3]}" + (w.Length == 5 ? " with motion marks" : "") +
						(_controller.DrawnActors.ContainsKey(w[2]) ? $" (in {person.Pose}{(person.Marked ? ", marked" : "")})" : " (not drawn)"));
				}
				break;
			case "expect" when w.Length == 4 && w[1] == "apart" && int.TryParse(w[3], CultureInfo.InvariantCulture, out int apartFrames):
				return Apart(w[2], apartFrames, source).GetEnumerator();
			case "expect" when w.Length == 3 && w[1] == "label" && w[2] == "none":
				if (_controller.Labels.Any())
				{
					Fail($"{source}: labels show [{string.Join(" | ", _controller.Labels)}]");
				}
				break;
			// No text the last frame drew holds TEXT (`expect notext {` checks that no control tag reached the screen).
			case "expect" when w.Length >= 3 && w[1] == "notext":
				string absent = string.Join(" ", w[2..]);
				if (_frameText().FirstOrDefault(t => t.Contains(absent, StringComparison.Ordinal)) is { } drawn)
				{
					Fail($"{source}: the frame draws {absent}: {drawn}");
				}
				break;
			case "expect" when w.Length >= 3 && w[1] == "notoast":
				string unwanted = string.Join(" ", w[2..]);
				if (_controller.Toasts.FirstOrDefault(t => t.Contains(unwanted, StringComparison.Ordinal)) is { } shown)
				{
					Fail($"{source}: a toast shows {unwanted}: {shown}");
				}
				break;
			case "expect" when w.Length >= 3 && w[1] == "toast":
				string wanted = string.Join(" ", w[2..]);
				List<string> toasts = _controller.Toasts.ToList();
				if (wanted == "none" ? toasts.Count > 0 : !toasts.Any(t => t.Contains(wanted, StringComparison.Ordinal)))
				{
					Fail($"{source}: expected toast {wanted}, showing [{string.Join(" | ", toasts)}]");
				}
				break;
			case "expect" when w.Length >= 3 && w[1] is "state" or "statehas" or "statenot":
				// Runs of spaces read as one, as the script's own words do; `statenot` expects the text nowhere in the value.
				string value = string.Join(" ", _controller.State(w[2]).Split(' ', StringSplitOptions.RemoveEmptyEntries));
				string want = string.Join(" ", w[3..]);
				bool met = w[1] switch { "state" => value == want, "statehas" => value.Contains(want, StringComparison.Ordinal), _ => !value.Contains(want, StringComparison.Ordinal) };
				if (value == "?" || !met)
				{
					Fail($"{source}: {w[2]} is '{value}', expected " + (w[1] switch { "state" => "", "statehas" => "it to hold ", _ => "it not to hold " }) + $"'{want}'");
				}
				break;
			case "savefile" when w.Length == 3 && w[1] == "damage" && _saves is not null:
				// Flips a byte in the middle of the slot's file, so its checksum no longer matches.
				string file = Path.Combine(_saves, w[2] + ".sav");
				byte[] bytes = File.ReadAllBytes(file);
				bytes[bytes.Length / 2] ^= 0x5a;
				File.WriteAllBytes(file, bytes);
				break;
			case "audio" when w.Length == 2 && w[1] == "mark" && _controller.Audio is RecordingAudio marked:
				_audioMark = marked.CallCount;
				break;
			case "audio" when w.Length == 2 && w[1] == "log" && _controller.Audio is RecordingAudio logged:
				foreach (string heardCall in logged.CallsSince(_audioMark))
				{
					Console.WriteLine($"[{_frame()}] audio| {heardCall}");
				}
				break;
			case "expect" when w.Length >= 3 && w[1] is "audio" or "noaudio" && _controller.Audio is RecordingAudio heard:
				// The calls since the last `audio mark`: one holds the text, or none does.
				string call = string.Join(" ", w[2..]);
				List<string> calls = heard.CallsSince(_audioMark).ToList();
				if (calls.Any(c => c.Contains(call, StringComparison.Ordinal)) != (w[1] == "audio"))
				{
					Fail($"{source}: expected {(w[1] == "audio" ? "a" : "no")} call holding '{call}' since the mark, heard [{string.Join(" | ", calls)}]");
				}
				break;
			case "assets" when w.Length == 2 && w[1] == "reload":
				_controller.ReloadAssets();
				break;
			case "assets" when w.Length == 4 && w[1] == "swap" && _controller.AssetDirectory is { } assetDir:
				// Copies WITH over FILE in the asset directory, then reloads, as an artist saving over a file would.
				File.Copy(Path.Combine(assetDir, w[3]), Path.Combine(assetDir, w[2]), overwrite: true);
				File.SetLastWriteTimeUtc(Path.Combine(assetDir, w[2]), DateTime.UtcNow);
				_controller.ReloadAssets();
				break;
			case "expect" when w.Length == 4 && w[1] == "asset" && w[3] is "file" or "generated":
				if (_controller.AssetFromFile(w[2]) != (w[3] == "file"))
				{
					Fail($"{source}: {w[2]} is {(_controller.AssetFromFile(w[2]) ? "from a file" : "generated")}, expected {w[3]}");
				}
				break;
			case "waitstate" or "waitstatehas" when w.Length >= 4 && int.TryParse(w[1], CultureInfo.InvariantCulture, out int maxWait):
				return WaitState(w[2], string.Join(" ", w[3..]), w[0] == "waitstatehas", maxWait, source).GetEnumerator();
			case "animlog" when w.Length == 2 && w[1] == "mark":
				_controller.MarkAnimLog();
				break;
			case "expect" when w.Length >= 3 && w[1] is "animlog" or "animloghas":
				// The animation events since the last `animlog mark`, joined by " | ": all of them, or holding the text.
				string anims = string.Join(" | ", _controller.AnimLogSinceMark);
				string wantAnims = string.Join(" ", w[2..]);
				if (w[1] == "animlog" ? anims != wantAnims : !anims.Contains(wantAnims, StringComparison.Ordinal))
				{
					Fail($"{source}: animation log [{anims}], expected {(w[1] == "animlog" ? "" : "it to hold ")}[{wantAnims}]");
				}
				break;
			case "present" when w.Length == 2 && w[1] == "mark":
				_presentMark = _controller.PresentedSteps.Count;
				break;
			case "expect" when w.Length >= 3 && w[1] is "present" or "presenthas":
				// The presentation steps started since the last `present mark`, joined by " | ": all of them, or holding the text.
				string steps = string.Join(" | ", _controller.PresentedSteps.Skip(_presentMark));
				string wantSteps = string.Join(" ", w[2..]);
				if (w[1] == "present" ? steps != wantSteps : !steps.Contains(wantSteps, StringComparison.Ordinal))
				{
					Fail($"{source}: presented [{steps}], expected {(w[1] == "present" ? "" : "it to hold ")}[{wantSteps}]");
				}
				break;
			case "datafile" when w.Length == 4 && w[1] == "swap" && _controller.DataDirectory is { } dataDir:
				// Copies WITH over FILE in the data directory, as a writer saving a data file would; `dev reload` or F6 reads it.
				File.Copy(Path.Combine(dataDir, w[3]), Path.Combine(dataDir, w[2]), overwrite: true);
				break;
			case "dev" or "devrefused" when w.Length > 1:
				// The console's command language; `devrefused` expects the command to be refused, as every one is without --dev.
				bool accepted = _controller.DevCommand(string.Join(" ", w[1..]));
				if (accepted != (w[0] == "dev"))
				{
					Fail($"{source}: the dev command was {(accepted ? "accepted" : "refused")}");
				}
				break;
			case "flashing" when w.Length == 2 && Enum.TryParse(w[1], true, out FlashLevel level) && level != FlashLevel.Unset:
				_controller.SetFlashing(level, confirmed: true);
				break;
			case "equip" when w.Length is 3 or 4:
				// A piece worn through the session's equip call, as the equip screen wears it.
				if (_controller.ScriptEquip(w[1], w[2], w.Length == 4 ? w[3] : null) is { } refusedEquip)
				{
					Fail($"{source}: equip refused: {refusedEquip}");
				}
				break;
			case "combo" when w.Length == 2:
				// A combination's abilities chosen by the members who know them, and the round resolved.
				if (_controller.ScriptCombo(w[1]) is { } refusedCombo)
				{
					Fail($"{source}: combo refused: {refusedCombo}");
				}
				break;
			case "battlelayout" when w.Length == 2:
				// The flag's override from here on; `data` goes back to the battle presentation table's layout.
				if (!_controller.SetBattleLayout(w[1]))
				{
					Fail($"{source}: no battle layout named {w[1]}: front, side or data");
				}
				break;
			case "shot" when w.Length == 2:
				return Shot(w[1]).GetEnumerator();
			case "dump":
				foreach (string text in _frameText())
				{
					Console.WriteLine($"[{_frame()}] | {text}");
				}
				break;
			default:
				Fail($"{source}: cannot read '{line.Trim()}'");
				break;
			}
		}
		return null;
	}

	// `controls ...`: the controller's rebinding calls, a later options screen's, so scripts can test them.
	private void Controls(string[] w, GameAction? action, string source)
	{
		switch (w[1])
		{
		case "list" when w.Length == 2:
			foreach (ControlBinding row in _controller.Controls())
			{
				Console.WriteLine($"[{_frame()}] controls| {row.Action}: {row.KeyNames} | {row.ButtonNames}");
			}
			break;
		case "capture" when action is { } capture && w.Length == 3:
			_controller.BeginCapture(capture);
			break;
		case "clear" when action is { } clear && w.Length == 3:
			Console.WriteLine($"[{_frame()}] controls| clear {clear}: {(_controller.ClearBinding(clear) ? "cleared" : "refused")}");
			break;
		case "reset" when w.Length == 2:
			_controller.ResetControls();
			break;
		case "save" when w.Length == 2:
			if (!_controller.SaveControls())
			{
				Fail($"{source}: the controls were not saved");
			}
			break;
		case "write" when w.Length > 2 && _controller.ControlsPath is { } path:
			File.WriteAllText(path, string.Join(" ", w[2..]));
			break;
		case "restart" when w.Length == 2 && _restart is not null:
			_controller = _restart();
			_controller.RealtimeField = _realtimeField;
			Console.WriteLine($"[{_frame()}] controls| restarted the controller");
			break;
		default:
			Fail($"{source}: cannot run '{string.Join(" ", w)}'");
			break;
		}
	}

	// `waitstate MAXFRAMES KEY VALUE` and `waitstatehas MAXFRAMES KEY TEXT`: frames with no keys until the probe equals the
	// value, or holds the text.
	private IEnumerable<InputFrame> WaitState(string key, string want, bool holds, int max, string source)
	{
		bool Met()
		{
			string value = string.Join(" ", _controller.State(key).Split(' ', StringSplitOptions.RemoveEmptyEntries));
			return holds ? value.Contains(want, StringComparison.Ordinal) : value == want;
		}
		for (int i = 0; i < max && !Met(); i++)
		{
			yield return InputFrame.None;
		}
		if (!Met())
		{
			_pendingFailure = $"{source}: {key} is still '{_controller.State(key)}' after {max} frames";
		}
	}

	// `expect apart ID FRAMES`: the last frame and the next FRAMES frames, played with no keys, draw nobody else
	// overlapping the person, less than a cell away on both axes.
	private IEnumerable<InputFrame> Apart(string id, int frames, string source)
	{
		for (int i = 0; ; i++)
		{
			if (!_controller.DrawnActors.TryGetValue(id, out DrawnPerson at))
			{
				_pendingFailure = $"{source}: {id} is not drawn, {i} frames in";
				yield break;
			}
			foreach ((string other, DrawnPerson p) in _controller.DrawnActors)
			{
				if (other != id && Math.Abs(p.X - at.X) < 1f && Math.Abs(p.Y - at.Y) < 1f)
				{
					_pendingFailure = $"{source}: {other} is drawn at {p.X} {p.Y}, over {id} at {at.X} {at.Y}, {i} frames in";
					yield break;
				}
			}
			if (i == frames)
			{
				yield break;
			}
			yield return InputFrame.None;
		}
	}

	// `expect actor ID X Y [FACING]`: the last frame drew the person (a script actor or a party member) standing on
	// that cell, facing that way if given. `expect actor ID walking`: it drew the person between cells. `expect actor
	// ID hidden`: it did not draw the person.
	private void ExpectActor(string[] w, string source)
	{
		bool drawn = _controller.DrawnActors.TryGetValue(w[2], out DrawnPerson at);
		if (w.Length == 4 && w[3] == "walking")
		{
			if (!drawn || !at.Walking)
			{
				Fail($"{source}: {w[2]} is not drawn walking");
			}
		}
		else if (w.Length == 4 && w[3] == "hidden")
		{
			if (drawn)
			{
				Fail($"{source}: {w[2]} is drawn at {at.X} {at.Y}");
			}
		}
		else if (w.Length is 5 or 6 && int.TryParse(w[3], CultureInfo.InvariantCulture, out int x) && int.TryParse(w[4], CultureInfo.InvariantCulture, out int y))
		{
			Direction facing = Direction.None;
			if (w.Length == 6 && !Enum.TryParse(w[5], true, out facing))
			{
				Fail($"{source}: no facing named {w[5]}");
				return;
			}
			if (!drawn || at.Walking || at.X != x || at.Y != y || (facing != Direction.None && at.Facing != facing))
			{
				Fail($"{source}: {w[2]} is not drawn standing at {x} {y}" + (facing != Direction.None ? $" facing {w[5]}" : "") +
					(drawn ? $" (at {at.X} {at.Y} facing {at.Facing}{(at.Walking ? ", walking" : "")})" : ""));
			}
		}
		else
		{
			Fail($"{source}: cannot read the actor expectation");
		}
	}

	// The keys and buttons for the next frame; none once the script is done.
	public InputFrame Next()
	{
		if (_trace.Length > 0)
		{
			Console.WriteLine($"[{_frame()}] trace| " + string.Join("  ", _trace.Select(k => $"{k}={_controller.State(k)}")));
		}
		while (_action is not null)
		{
			if (_action.MoveNext())
			{
				return _action.Current;
			}
			if (_pendingFailure is not null)
			{
				Fail(_pendingFailure);
				_pendingFailure = null;
			}
			_action = NextAction();
		}
		return InputFrame.None;
	}

	public void Finish(int frames)
	{
		if (_pendingFailure is not null)
		{
			Fail(_pendingFailure);
		}
		if (!Done)
		{
			Fail($"the script did not finish in {frames} frames (at line {_next})");
		}
	}
}

// Runs the same controller and screens as the window, frame by frame at 16 ms, with scripted key and button
// presses and a drawing surface that only records.
public static class Headless
{
	public const int FrameMs = 16;

	// With `strictText`, each text key the profile's language lacks fails the run once.
	public static int Run(GameSession session, GameData data, string scriptPath, int frames, string? saves, string? assets, string? digests, bool dev = false,
		string? dataDir = null, BattleLayout? battleLayout = null, bool strictText = false)
	{
		int frame = 0;
		int failures = 0;
		var surface = new RecordingSurface();
		string? controls = saves is null ? null : Path.Combine(saves, SampleGame.ControlsFile);
		var audio = new RecordingAudio();
		var missing = new HashSet<string>();
		void MissingText(string key)
		{
			if (strictText && missing.Add(key))
			{
				failures++;
				Console.WriteLine($"[{frame}] FAIL: no text {key} in language {session.Profile().TextLanguage}");
			}
		}
		SampleController NewController() => new(session, data, line => Console.WriteLine($"[{frame}] {line}"), controls, audio, assets, dev, dataDir, battleLayout) { MissingText = MissingText, PixelAt = surface.PixelAt, ImagePixel = surface.ImagePixel };
		SampleController controller = NewController();
		var input = new InputState();
		try
		{
			var player = new ScriptPlayer(session, controller, scriptPath, () => surface.FrameText, () => frame, restart: NewController, saves: saves);
			while (frame < frames && !controller.QuitRequested)
			{
				InputFrame next = player.Next();
				controller = player.Controller;
				input.Set(next.Keys, next.Buttons, click: next.Click);
				controller.Update(FrameMs, input);
				(surface.Width, surface.Height) = controller.LogicalSize;
				surface.BeginFrame();
				controller.Draw(surface);
				surface.EndFrame();
				frame++;
			}
			player.Finish(frames);
			failures += player.Failures;
		}
		catch (Exception e)
		{
			failures++;
			Console.WriteLine($"[{frame}] FAIL: exception: {e}");
		}
		if (digests is not null)
		{
			// One line per frame, so two runs compare with cmp and differ at a frame number diff can show.
			File.WriteAllLines(digests, surface.Digests.Select((d, i) => $"{i} {d:x16}"));
		}
		WorldInfo world = session.WorldInfo();
		Console.WriteLine($"headless: {frame} frames, screen {controller.Screen}, mode {session.Mode}, money {world.Money}, play time {world.PlayTimeMs} ms, " +
			$"{surface.Rects} rects, {surface.Images} images and {surface.Texts} texts drawn, {failures} failures");
		return failures == 0 ? 0 : 1;
	}
}
