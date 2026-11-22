using Microsoft.Xna.Framework.Input;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private readonly Bindings _bindings;
	private bool _usingGamepad;

	public string? ControlsPath { get; }

	// The action whose next key or button becomes its binding; the screens get no input until then.
	public GameAction? Capturing { get; private set; }

	public IReadOnlyList<ControlBinding> Controls() => ControlActions().Select(_bindings.Describe).ToList();

	// The actions the Controls page lists: the real-time field input's own only while it is on, Extras only with the controls
	// layout's `list_extras`.
	private GameAction[] ControlActions() => [.. Enum.GetValues<GameAction>().Where(a => (_realtimeField || !Bindings.FieldActions.Contains(a))
		&& (a != GameAction.Extras || Ui.Param("controls", "list_extras", UiData(), 0) != 0))];

	public void BeginCapture(GameAction action) => Capturing = action;

	// Refused for an action the menus need, which would leave them unusable.
	public bool ClearBinding(GameAction action)
	{
		if (Bindings.Required.Contains(action))
		{
			return false;
		}
		_bindings.Clear(action);
		return true;
	}

	public void ResetControls() => _bindings.ResetAll();

	public bool SaveControls()
	{
		if (ControlsPath is null)
		{
			return false;
		}
		try
		{
			_bindings.Save(ControlsPath);
			return true;
		}
		catch (Exception e) when (e is IOException or UnauthorizedAccessException)
		{
			_log($"controls: cannot save {ControlsPath} ({e.Message})");
			return false;
		}
	}

	// A captured key replaces the action's keys and a captured button its buttons; Escape cancels.
	private void Capture(GameAction action, InputState raw)
	{
		if (raw.Pressed(Keys.Escape))
		{
			Capturing = null;
			_log($"controls: {action} capture cancelled");
		}
		else if (raw.PressedKeys.Any())
		{
			Keys key = raw.PressedKeys.Min();
			Capturing = null;
			_bindings.SetKey(action, key);
			_log($"controls: {action} bound to {Bindings.KeyName(key)}");
		}
		else if (raw.PressedButtons.Any())
		{
			Buttons button = raw.PressedButtons.Min();
			Capturing = null;
			_bindings.SetButton(action, button);
			_log($"controls: {action} bound to {Bindings.ButtonName(button)}");
		}
	}

	// The action's first binding on the device last used, or on the other one when it has none there.
	private string Hint(GameAction action)
	{
		IReadOnlyList<Keys> keys = _bindings.KeysOf(action);
		IReadOnlyList<Buttons> buttons = _bindings.ButtonsOf(action);
		return _usingGamepad && buttons.Count > 0 ? ButtonText(buttons[0])
			: keys.Count > 0 ? KeyText(keys[0])
			: buttons.Count > 0 ? ButtonText(buttons[0])
			: "-";
	}

	private string Walk()
	{
		GameAction[] directions = [GameAction.Up, GameAction.Left, GameAction.Down, GameAction.Right];
		Keys[] arrows = [Keys.Up, Keys.Left, Keys.Down, Keys.Right];
		Buttons[] dpad = [Buttons.DPadUp, Buttons.DPadLeft, Buttons.DPadDown, Buttons.DPadRight];
		bool padShown = _usingGamepad && directions.Any(a => _bindings.ButtonsOf(a).Count > 0);
		if (padShown && directions.Select((a, i) => _bindings.ButtonsOf(a).FirstOrDefault() == dpad[i]).All(x => x))
		{
			return T("ui.hint.d_pad");
		}
		if (!padShown && directions.Select((a, i) => _bindings.KeysOf(a).FirstOrDefault() == arrows[i]).All(x => x))
		{
			return T("ui.hint.arrows");
		}
		return string.Join("/", directions.Select(Hint));
	}
}
