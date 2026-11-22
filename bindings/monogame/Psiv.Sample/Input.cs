using System.Text.Json;
using System.Text.Json.Nodes;

using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

public enum GameAction { Up, Down, Left, Right, Confirm, Back, Menu, QuestLog, PartyTalk, TextLog, Skip, PagePrev, PageNext, Pause, FastForward, Interact, Run, RunToggle }

// Keys and gamepad buttons down this frame and the ones that went down since the last frame.
public sealed class InputState
{
	private readonly HashSet<Keys> _down = [];
	private readonly HashSet<Keys> _pressed = [];
	private readonly HashSet<Buttons> _buttonsDown = [];
	private readonly HashSet<Buttons> _buttonsPressed = [];

	public void Set(IEnumerable<Keys> keys, IEnumerable<Buttons> buttons, bool padConnected = true, (int X, int Y)? click = null)
	{
		Click = click;
		Advance(_down, _pressed, keys);
		Advance(_buttonsDown, _buttonsPressed, buttons);
		if (_pressed.Count > 0 || !padConnected)
		{
			UsingGamepad = false;
		}
		else if (_buttonsPressed.Count > 0)
		{
			UsingGamepad = true;
		}
	}

	private static void Advance<T>(HashSet<T> down, HashSet<T> pressed, IEnumerable<T> now)
	{
		var current = new HashSet<T>(now);
		pressed.Clear();
		foreach (T item in current)
		{
			if (!down.Contains(item))
			{
				pressed.Add(item);
			}
		}
		down.Clear();
		down.UnionWith(current);
	}

	// Whether the last key or button that went down was on the gamepad.
	public bool UsingGamepad { get; private set; }
	// A mouse click this frame, in the sample's own logical pixels (1280x720 by default).
	public (int X, int Y)? Click { get; private set; }

	private readonly Dictionary<Keys, int> _heldMs = [];
	private readonly Dictionary<Buttons, int> _buttonHeldMs = [];

	// Held keys and buttons that `repeats` picks press again after `delayMs`, then every `everyMs`: the layout's list
	// scroll repeat. Called once a frame after Set.
	public void Repeat(int dtMs, int delayMs, int everyMs, Func<Keys, bool> keyRepeats, Func<Buttons, bool> buttonRepeats)
	{
		RepeatHeld(_heldMs, _down, _pressed, dtMs, delayMs, everyMs, keyRepeats);
		RepeatHeld(_buttonHeldMs, _buttonsDown, _buttonsPressed, dtMs, delayMs, everyMs, buttonRepeats);
	}

	private static void RepeatHeld<T>(Dictionary<T, int> held, HashSet<T> down, HashSet<T> pressed, int dtMs, int delayMs, int everyMs, Func<T, bool> repeats) where T : notnull
	{
		int Count(int ms) => ms < delayMs ? 0 : 1 + (ms - delayMs) / Math.Max(1, everyMs);
		foreach (T item in held.Keys.Where(k => !down.Contains(k)).ToList())
		{
			held.Remove(item);
		}
		foreach (T item in down.Where(repeats))
		{
			int before = pressed.Contains(item) ? 0 : held.GetValueOrDefault(item);
			int after = before + (pressed.Contains(item) ? 0 : dtMs);
			held[item] = after;
			if (Count(after) > Count(before))
			{
				pressed.Add(item);
			}
		}
	}

	public bool Down(Keys key) => _down.Contains(key);
	public bool Pressed(Keys key) => _pressed.Contains(key);
	public bool Down(Buttons button) => _buttonsDown.Contains(button);
	public bool Pressed(Buttons button) => _buttonsPressed.Contains(button);
	public IEnumerable<Keys> PressedKeys => _pressed;
	public IEnumerable<Buttons> PressedButtons => _buttonsPressed;
}

// One frame of input read through the bindings.
public readonly struct ActionInput(InputState raw, Bindings bindings)
{
	public InputState Raw => raw;

	public bool Pressed(GameAction action) => bindings.KeysOf(action).Any(raw.Pressed) || ButtonPressed(action);
	public bool ButtonPressed(GameAction action) => bindings.ButtonsOf(action).Any(raw.Pressed);
	public bool Down(GameAction action) => bindings.KeysOf(action).Any(raw.Down) || bindings.ButtonsOf(action).Any(raw.Down);
	public bool Confirm => Pressed(GameAction.Confirm);
	public bool Back => Pressed(GameAction.Back);

	public Direction Held =>
		Down(GameAction.Up) ? Direction.North : Down(GameAction.Right) ? Direction.East : Down(GameAction.Down) ? Direction.South : Down(GameAction.Left) ? Direction.West : Direction.None;
}

// The left stick as one of four directions for the real-time field input, with hysteresis: a direction holds until the
// stick falls back inside a smaller dead zone, and the other axis takes over only once it leads clearly, so a stick
// held near a diagonal or near the dead zone's edge does not flicker.
public sealed class StickSnap
{
	private const float Enter = 0.5f;
	private const float Leave = 0.35f;
	private const float AxisLead = 1.3f;

	private Buttons? _held;

	public Buttons? Snap(float x, float y)
	{
		float ax = Math.Abs(x), ay = Math.Abs(y);
		bool horizontal = _held is Buttons.LeftThumbstickLeft or Buttons.LeftThumbstickRight;
		if (_held is null ? Math.Max(ax, ay) < Enter : Math.Max(ax, ay) < Leave)
		{
			_held = null;
		}
		else if (_held is null ? ax > ay : horizontal ? ay <= ax * AxisLead : ax > ay * AxisLead)
		{
			_held = x > 0 ? Buttons.LeftThumbstickRight : Buttons.LeftThumbstickLeft;
		}
		else
		{
			_held = y > 0 ? Buttons.LeftThumbstickUp : Buttons.LeftThumbstickDown;
		}
		return _held;
	}
}

public static class Pad
{
	// Past this the stick counts as the direction of its stronger axis only, so a diagonal does not flicker between two.
	private const float DeadZone = 0.5f;

	private static readonly Buttons[] Digital =
	[
		Buttons.A, Buttons.B, Buttons.X, Buttons.Y, Buttons.Back, Buttons.Start, Buttons.BigButton, Buttons.LeftShoulder, Buttons.RightShoulder,
		Buttons.LeftTrigger, Buttons.RightTrigger, Buttons.LeftStick, Buttons.RightStick, Buttons.DPadUp, Buttons.DPadDown, Buttons.DPadLeft, Buttons.DPadRight,
	];

	// With `snap`, the stick goes through its hysteresis instead of the plain dead zone.
	public static IEnumerable<Buttons> ButtonsDown(GamePadState state, StickSnap? snap = null)
	{
		if (!state.IsConnected)
		{
			yield break;
		}
		foreach (Buttons b in Digital)
		{
			if (state.IsButtonDown(b))
			{
				yield return b;
			}
		}
		float x = state.ThumbSticks.Left.X;
		float y = state.ThumbSticks.Left.Y;
		if (snap is not null)
		{
			if (snap.Snap(x, y) is { } stick)
			{
				yield return stick;
			}
			yield break;
		}
		if (Math.Max(Math.Abs(x), Math.Abs(y)) < DeadZone)
		{
			yield break;
		}
		yield return Math.Abs(x) > Math.Abs(y)
			? x > 0 ? Buttons.LeftThumbstickRight : Buttons.LeftThumbstickLeft
			: y > 0 ? Buttons.LeftThumbstickUp : Buttons.LeftThumbstickDown;
	}
}

public sealed record ControlBinding(GameAction Action, string KeyNames, string ButtonNames);

// Each action's keyboard keys and gamepad buttons, saved as controls.json.
public sealed class Bindings
{
	// Without these the menus cannot be used, so a file never leaves them unbound.
	public static readonly GameAction[] Required = [GameAction.Up, GameAction.Down, GameAction.Left, GameAction.Right, GameAction.Confirm, GameAction.Back];
	// Read only by the real-time field input, so the Controls page and `controls list` show them only while it is on.
	public static readonly GameAction[] FieldActions = [GameAction.Interact, GameAction.Run, GameAction.RunToggle];

	// The real-time field's interaction prompt, kept beside the bindings; Always run is the profile's.
	public bool InteractPrompt { get; set; } = true;

	// The battle's macro options, kept beside them and written only when set: the macro auto-battle plays every round
	// (GameSession.SetAutoPlayMacro; "" for the save's auto macro through StartAuto, as before), and whether a macro used
	// from the battle menu gives each member whose step cannot run core's automatic choice (MacroFallback.Automatic).
	public string AutoPlayMacro { get; set; } = "";
	public bool MacroFallbackAutomatic { get; set; }

	private readonly Dictionary<GameAction, List<Keys>> _keys = [];
	private readonly Dictionary<GameAction, List<Buttons>> _buttons = [];

	public Bindings() => ResetAll();

	private static (Keys[] Keys, Buttons[] Buttons) Default(GameAction action) => action switch
	{
		GameAction.Up => ([Keys.Up], [Buttons.DPadUp, Buttons.LeftThumbstickUp]),
		GameAction.Down => ([Keys.Down], [Buttons.DPadDown, Buttons.LeftThumbstickDown]),
		GameAction.Left => ([Keys.Left], [Buttons.DPadLeft, Buttons.LeftThumbstickLeft]),
		GameAction.Right => ([Keys.Right], [Buttons.DPadRight, Buttons.LeftThumbstickRight]),
		GameAction.Confirm => ([Keys.Enter, Keys.Z, Keys.Space], [Buttons.A]),
		GameAction.Back => ([Keys.Escape, Keys.X, Keys.Back], [Buttons.B]),
		GameAction.Menu => ([Keys.M, Keys.Escape], [Buttons.Start, Buttons.Y]),
		GameAction.QuestLog => ([Keys.Q], [Buttons.LeftTrigger]),
		GameAction.PartyTalk => ([Keys.T], [Buttons.X]),
		GameAction.TextLog => ([Keys.L], [Buttons.RightTrigger]),
		GameAction.Skip => ([Keys.Tab], [Buttons.Back]),
		GameAction.PagePrev => ([Keys.Left], [Buttons.LeftShoulder, Buttons.DPadLeft]),
		GameAction.PageNext => ([Keys.Right], [Buttons.RightShoulder, Buttons.DPadRight]),
		// Start opens the field menu, so Select pauses: it is Skip only while an event runs, where there is no pause.
		GameAction.Pause => ([Keys.P], [Buttons.Back]),
		// Held during a battle's playback; the shoulder button is PageNext only in menus.
		GameAction.FastForward => ([Keys.F], [Buttons.RightShoulder]),
		// Confirm interacts as well; this key only interacts.
		GameAction.Interact => ([Keys.E], []),
		// The shoulder buttons are PagePrev and PageNext only in menus.
		GameAction.Run => ([Keys.LeftShift], [Buttons.LeftShoulder]),
		GameAction.RunToggle => ([Keys.CapsLock], [Buttons.LeftStick]),
		_ => ([], []),
	};

	public IReadOnlyList<Keys> KeysOf(GameAction action) => _keys[action];
	public IReadOnlyList<Buttons> ButtonsOf(GameAction action) => _buttons[action];

	public void SetKey(GameAction action, Keys key) => _keys[action] = [key];
	public void SetButton(GameAction action, Buttons button) => _buttons[action] = [button];

	public void Clear(GameAction action)
	{
		_keys[action] = [];
		_buttons[action] = [];
	}

	public void Reset(GameAction action)
	{
		(Keys[] keys, Buttons[] buttons) = Default(action);
		_keys[action] = [.. keys];
		_buttons[action] = [.. buttons];
	}

	public void ResetAll()
	{
		foreach (GameAction action in Enum.GetValues<GameAction>())
		{
			Reset(action);
		}
	}

	public ControlBinding Describe(GameAction action) =>
		new(action, Names(_keys[action].Select(KeyName)), Names(_buttons[action].Select(ButtonName)));

	private static string Names(IEnumerable<string> names) => string.Join(", ", names) is { Length: > 0 } joined ? joined : "-";

	public static string KeyName(Keys key) => key switch
	{
		Keys.Escape => "Esc",
		Keys.Back => "Backspace",
		>= Keys.D0 and <= Keys.D9 => ((char)('0' + (key - Keys.D0))).ToString(),
		_ => key.ToString(),
	};

	public static string ButtonName(Buttons button) => button switch
	{
		Buttons.Back => "Select",
		Buttons.LeftShoulder => "LB",
		Buttons.RightShoulder => "RB",
		Buttons.LeftTrigger => "LT",
		Buttons.RightTrigger => "RT",
		Buttons.LeftStick => "L3",
		Buttons.RightStick => "R3",
		Buttons.DPadUp => "D-pad Up",
		Buttons.DPadDown => "D-pad Down",
		Buttons.DPadLeft => "D-pad Left",
		Buttons.DPadRight => "D-pad Right",
		Buttons.LeftThumbstickUp => "Stick Up",
		Buttons.LeftThumbstickDown => "Stick Down",
		Buttons.LeftThumbstickLeft => "Stick Left",
		Buttons.LeftThumbstickRight => "Stick Right",
		_ => button.ToString(),
	};

	// A missing file gives the defaults; one that does not read gives the defaults and one log line. An action the
	// file leaves out keeps its default, and a required action the file leaves unbound gets its default back.
	public static Bindings Load(string? path, Action<string> log)
	{
		var bindings = new Bindings();
		if (path is null || !File.Exists(path))
		{
			return bindings;
		}
		try
		{
			if (JsonNode.Parse(File.ReadAllText(path)) is not JsonObject root)
			{
				throw new FormatException("not a JSON object");
			}
			foreach ((string name, JsonNode? entry) in root)
			{
				if (name == "field" && entry is JsonObject options)
				{
					bindings.InteractPrompt = options["interact_prompt"]?.GetValue<bool>() ?? true;
					continue;
				}
				if (name == "battle" && entry is JsonObject battle)
				{
					bindings.AutoPlayMacro = battle["auto_play_macro"]?.GetValue<string>() ?? "";
					bindings.MacroFallbackAutomatic = battle["macro_fallback"]?.GetValue<string>() == "automatic";
					continue;
				}
				if (!TryName(name, out GameAction action) || entry is not JsonObject fields)
				{
					throw new FormatException($"cannot read the entry {name}");
				}
				bindings._keys[action] = ReadNames<Keys>(fields["keys"]);
				bindings._buttons[action] = ReadNames<Buttons>(fields["buttons"]);
			}
		}
		catch (Exception e) when (e is JsonException or FormatException or InvalidOperationException or IOException or UnauthorizedAccessException)
		{
			log($"controls: {path} cannot be read ({e.Message}); using the defaults");
			return new Bindings();
		}
		foreach (GameAction action in Required.Where(a => bindings._keys[a].Count == 0 && bindings._buttons[a].Count == 0))
		{
			bindings.Reset(action);
			log($"controls: {action} has no binding in {path}; restored its default");
		}
		return bindings;
	}

	private static List<T> ReadNames<T>(JsonNode? node) where T : struct, Enum
	{
		if (node is null)
		{
			return [];
		}
		var values = new List<T>();
		foreach (JsonNode? item in node.AsArray())
		{
			string name = item?.GetValue<string>() ?? "";
			if (!TryName(name, out T value))
			{
				throw new FormatException($"unknown {typeof(T).Name} name {name}");
			}
			if (!values.Contains(value))
			{
				values.Add(value);
			}
		}
		return values;
	}

	// Only the exact enum name: no numbers, no other case.
	private static bool TryName<T>(string name, out T value) where T : struct, Enum =>
		Enum.TryParse(name, ignoreCase: false, out value) && Enum.GetName(value) == name;

	public void Save(string path)
	{
		using var stream = new MemoryStream();
		using (var json = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true, IndentCharacter = '\t', IndentSize = 1 }))
		{
			json.WriteStartObject();
			foreach (GameAction action in Enum.GetValues<GameAction>())
			{
				json.WriteStartObject(action.ToString());
				json.WriteStartArray("keys");
				foreach (Keys key in _keys[action])
				{
					json.WriteStringValue(key.ToString());
				}
				json.WriteEndArray();
				json.WriteStartArray("buttons");
				foreach (Buttons button in _buttons[action])
				{
					json.WriteStringValue(button.ToString());
				}
				json.WriteEndArray();
				json.WriteEndObject();
			}
			json.WriteStartObject("field");
			json.WriteBoolean("interact_prompt", InteractPrompt);
			json.WriteEndObject();
			if (AutoPlayMacro.Length > 0 || MacroFallbackAutomatic)
			{
				json.WriteStartObject("battle");
				json.WriteString("auto_play_macro", AutoPlayMacro);
				json.WriteString("macro_fallback", MacroFallbackAutomatic ? "automatic" : "basic_attack");
				json.WriteEndObject();
			}
			json.WriteEndObject();
		}
		stream.WriteByte((byte)'\n');
		File.WriteAllBytes(path, stream.ToArray());
	}
}
