using System.Globalization;

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
		"cmd GAME_CLI_COMMAND | expect screen NAME | expect mode NAME | dump | shot FILE.png (window only) | # comment.\n" +
		"Keys are MonoGame Keys names.\n" +
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
				return Headless.Run(session, script, frames);
			}
			try
			{
				using var game = new SampleGame(session, font, exitAfterMs, script, screenshot);
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

// Plays a key script frame by frame: each call to Next gives the keys held for one frame. Lines that take
// no frames (cmd, expect, dump) run between frames.
public sealed class ScriptPlayer
{
	private readonly GameSession _session;
	private readonly SampleController _controller;
	private readonly ScriptRunner _runner;
	private readonly StringWriter _commandOutput = new();
	private readonly Func<IReadOnlyList<string>> _frameText;
	private readonly Func<int> _frame;
	private readonly Action<string>? _shot;
	private readonly string _name;
	private readonly string[] _lines;
	private int _next;
	private IEnumerator<Keys[]>? _action;
	private string? _pendingFailure;

	// `shot` saves the next drawn frame to a file; null (headless) skips shot lines.
	public ScriptPlayer(GameSession session, SampleController controller, string path, Func<IReadOnlyList<string>> frameText, Func<int> frame, Action<string>? shot = null)
	{
		_shot = shot;
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

	public bool Done => _action is null && _next >= _lines.Length;

	private void Fail(string message)
	{
		Failures++;
		Console.WriteLine($"[{_frame()}] FAIL: {message}");
	}

	private static IEnumerable<Keys[]> Press(Keys key, int count)
	{
		for (int i = 0; i < count; i++)
		{
			yield return [key];
			yield return [];
		}
	}

	private static IEnumerable<Keys[]> Repeat(Keys[] keys, int count)
	{
		for (int i = 0; i < count; i++)
		{
			yield return keys;
		}
	}

	// One frame with no keys, so the frame drawn is the one the script reached.
	private IEnumerable<Keys[]> Shot(string path)
	{
		_shot?.Invoke(path);
		yield return [];
	}

	private IEnumerable<Keys[]> PressUntil(Keys key, Screen screen, int max, string source)
	{
		for (int i = 0; i < max; i += 2)
		{
			if (_controller.Screen == screen)
			{
				yield break;
			}
			yield return [key];
			yield return [];
		}
		if (_controller.Screen != screen)
		{
			_pendingFailure = $"{source}: still on {_controller.Screen} after {max} frames";
		}
	}

	private IEnumerator<Keys[]>? NextAction()
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
			switch (w[0])
			{
			case "press" when keyOk:
				return Press(key, w.Length > 2 ? int.Parse(w[2], CultureInfo.InvariantCulture) : 1).GetEnumerator();
			case "hold" when keyOk && w.Length == 3:
				return Repeat([key], int.Parse(w[2], CultureInfo.InvariantCulture)).GetEnumerator();
			case "wait" when w.Length == 2:
				return Repeat([], int.Parse(w[1], CultureInfo.InvariantCulture)).GetEnumerator();
			case "pressuntil" when keyOk && w.Length == 4 && Enum.TryParse(w[2], true, out Screen screen):
				return PressUntil(key, screen, int.Parse(w[3], CultureInfo.InvariantCulture), source).GetEnumerator();
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

	// The keys for the next frame; none once the script is done.
	public Keys[] Next()
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
		return [];
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

// Runs the same controller and screens as the window, frame by frame at 16 ms, with scripted key presses
// and a drawing surface that only records.
public static class Headless
{
	private const int FrameMs = 16;

	public static int Run(GameSession session, string scriptPath, int frames)
	{
		int frame = 0;
		int failures = 0;
		var surface = new RecordingSurface();
		var controller = new SampleController(session, line => Console.WriteLine($"[{frame}] {line}"));
		var input = new InputState();
		try
		{
			var player = new ScriptPlayer(session, controller, scriptPath, () => surface.FrameText, () => frame);
			while (frame < frames && !controller.QuitRequested)
			{
				input.Set(player.Next());
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
