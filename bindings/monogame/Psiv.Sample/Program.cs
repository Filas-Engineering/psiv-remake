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
		"  Psiv.Sample [--data DIR] [--saves DIR] [--seed N] [--font FILE] [--exit-after-ms N] [--screenshot FILE.png]\n" +
		"  Psiv.Sample --window --script FILE ...   the window, with keys from the script instead of the keyboard\n" +
		"  Psiv.Sample --script FILE --frames N [--data DIR] [--saves DIR] [--seed N]   headless, no window\n" +
		"\n" +
		"Headless script lines: press KEY [COUNT] | hold KEY FRAMES | wait FRAMES | pressuntil KEY SCREEN MAXFRAMES |\n" +
		"pad BUTTON [COUNT] | padhold BUTTON FRAMES | paduntil BUTTON SCREEN MAXFRAMES | padstick X Y FRAMES |\n" +
		"controls list|reset|save|restart | controls capture|clear ACTION | controls write JSON |\n" +
		"cmd GAME_CLI_COMMAND | expect screen NAME | expect mode NAME | expect actor ID X Y [FACING]|walking|hidden | expect pose ID POSE [marked] |\n" +
		"expect label none | expect toast none|TEXT | expect notoast TEXT | expect binding ACTION KEYS \"|\" BUTTONS |\n" +
		"expect state KEY VALUE | expect statehas KEY TEXT | savefile damage SLOT | dump | shot FILE.png (window only) | # comment.\n" +
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
		string? screenshot = null;
		for (int i = 0; i < args.Length; i++)
		{
			string? Next() => i + 1 < args.Length ? args[++i] : null;
			switch (args[i])
			{
			case "--data": data = Next(); break;
			case "--saves": saves = Next(); break;
			case "--font": font = Next(); break;
			case "--window": window = true; break;
			case "--screenshot": screenshot = Next(); break;
			case "--script": script = Next(); break;
			case "--frames" when int.TryParse(Next(), CultureInfo.InvariantCulture, out int n): frames = n; break;
			case "--exit-after-ms" when int.TryParse(Next(), CultureInfo.InvariantCulture, out int n): exitAfterMs = n; break;
			case "--seed" when ulong.TryParse(Next(), CultureInfo.InvariantCulture, out ulong n): seed = n; break;
			case "--help" or "-h":
				Console.Write(Usage);
				return 0;
			default:
				Console.Error.Write(Usage);
				return 2;
			}
		}
		data ??= FindData();
		if (data is null)
		{
			Console.Error.WriteLine("cannot find game/data; pass --data DIR");
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
			if (script is not null && !window)
			{
				return Headless.Run(session, script, frames, saves);
			}
			try
			{
				using var game = new SampleGame(session, font, exitAfterMs, saves, script, screenshot);
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

	private static string? FindData()
	{
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
public readonly record struct InputFrame(Keys[] Keys, Buttons[] Buttons)
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
	private readonly string _name;
	private readonly string[] _lines;
	private int _next;
	private IEnumerator<InputFrame>? _action;
	private string? _pendingFailure;

	// `shot` saves the next drawn frame to a file; null (headless) skips shot lines. `restart` makes a new controller
	// for `controls restart`.
	public ScriptPlayer(GameSession session, SampleController controller, string path, Func<IReadOnlyList<string>> frameText, Func<int> frame,
		Action<string>? shot = null, Func<SampleController>? restart = null, string? saves = null)
	{
		_shot = shot;
		_saves = saves;
		_restart = restart;
		_session = session;
		_controller = controller;
		_runner = new ScriptRunner(session, _commandOutput, autoBattle: false);
		_frameText = frameText;
		_frame = frame;
		_name = Path.GetFileName(path);
		_lines = File.ReadAllLines(path);
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
	private static InputFrame Stick(float x, float y) =>
		new([], [.. Pad.ButtonsDown(new GamePadState(new GamePadThumbSticks(new Vector2(x, y), Vector2.Zero), new GamePadTriggers(), new GamePadButtons(), new GamePadDPad()))]);

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
			switch (w[0])
			{
			case "press" when keyOk:
				return Press(new([key], []), w.Length > 2 ? int.Parse(w[2], CultureInfo.InvariantCulture) : 1).GetEnumerator();
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
				return Repeat(Stick(stickX, stickY), int.Parse(w[3], CultureInfo.InvariantCulture)).GetEnumerator();
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
			case "expect" when w.Length >= 3 && w[1] is "state" or "statehas":
				// Runs of spaces read as one, as the script's own words do.
				string value = string.Join(" ", _controller.State(w[2]).Split(' ', StringSplitOptions.RemoveEmptyEntries));
				string want = string.Join(" ", w[3..]);
				if (value == "?" || (w[1] == "state" ? value != want : !value.Contains(want, StringComparison.Ordinal)))
				{
					Fail($"{source}: {w[2]} is '{value}', expected " + (w[1] == "state" ? "" : "it to hold ") + $"'{want}'");
				}
				break;
			case "savefile" when w.Length == 3 && w[1] == "damage" && _saves is not null:
				// Flips a byte in the middle of the slot's file, so its checksum no longer matches.
				string file = Path.Combine(_saves, w[2] + ".sav");
				byte[] bytes = File.ReadAllBytes(file);
				bytes[bytes.Length / 2] ^= 0x5a;
				File.WriteAllBytes(file, bytes);
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
			Console.WriteLine($"[{_frame()}] controls| restarted the controller");
			break;
		default:
			Fail($"{source}: cannot run '{string.Join(" ", w)}'");
			break;
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
	private const int FrameMs = 16;

	public static int Run(GameSession session, string scriptPath, int frames, string? saves)
	{
		int frame = 0;
		int failures = 0;
		var surface = new RecordingSurface();
		string? controls = saves is null ? null : Path.Combine(saves, SampleGame.ControlsFile);
		SampleController NewController() => new(session, line => Console.WriteLine($"[{frame}] {line}"), controls);
		SampleController controller = NewController();
		var input = new InputState();
		try
		{
			var player = new ScriptPlayer(session, controller, scriptPath, () => surface.FrameText, () => frame, restart: NewController, saves: saves);
			while (frame < frames && !controller.QuitRequested)
			{
				InputFrame next = player.Next();
				controller = player.Controller;
				input.Set(next.Keys, next.Buttons);
				controller.Update(FrameMs, input);
				surface.BeginFrame();
				controller.Draw(surface);
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
		WorldInfo world = session.WorldInfo();
		Console.WriteLine($"headless: {frame} frames, screen {controller.Screen}, mode {session.Mode}, money {world.Money}, play time {world.PlayTimeMs} ms, " +
			$"{surface.Rects} rects, {surface.Images} images and {surface.Texts} texts drawn, {failures} failures");
		return failures == 0 ? 0 : 1;
	}
}
