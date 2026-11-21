using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// Options, from the title, the pause overlay or the field menu: the profile's settings, the save's difficulty and
// reward multiplier, and a Controls page over the bindings. Left and Right (or Confirm) change the selected value.
public sealed partial class SampleController
{
	private const string ControlsRow = "Controls";
	private const string ResetRow = "Reset to defaults";

	private readonly ScrollList _optList = new();
	private readonly ScrollList _optSub = new();
	private bool _controlsPage;
	// The action picked on the Controls page, choosing between rebinding and clearing it.
	private GameAction? _optAction;

	// One setting: its label, its value as shown, what it changes, and how it steps (null: it cannot be changed here).
	private sealed record OptionRow(string Label, string Value, string Description, Func<int, PsivResult>? Change);

	private void OpenOptions()
	{
		_optionsOverlay = true;
		_controlsPage = false;
		_optAction = null;
		_optList.Reset();
	}

	private static T Step<T>(T value, int by) where T : struct, Enum
	{
		T[] all = Enum.GetValues<T>();
		return all[(Array.IndexOf(all, value) + by + all.Length) % all.Length];
	}

	private static string OnOff(bool on) => on ? "On" : "Off";

	private static string SpeedName(BattleSpeed speed) => speed switch
	{
		BattleSpeed.X1 => "1x",
		BattleSpeed.X1_5 => "1.5x",
		BattleSpeed.X2 => "2x",
		_ => "3x",
	};

	private static string RewardName(RewardMultiplier reward) => reward switch
	{
		RewardMultiplier.X1 => "1x",
		RewardMultiplier.X1_5 => "1.5x",
		_ => "2x",
	};

	private List<OptionRow> OptionRows()
	{
		Profile p = _s.Profile();
		Settings settings = _s.Settings();
		IReadOnlyList<string> languages = _s.GameInfo().Languages;
		static int Volume(int value, int by) => Math.Clamp(value + 10 * by, 0, 100);
		return
		[
			new("Text speed", p.TextSpeed.ToString(), "How fast dialogue text appears. Confirm shows the rest of a line at once.",
				by => _s.SetProfile(p with { TextSpeed = Step(p.TextSpeed, by) })),
			new("Auto-advance", OnOff(p.AutoAdvance), "Dialogue moves on by itself after a pause that grows with the line's length.",
				_ => _s.SetProfile(p with { AutoAdvance = !p.AutoAdvance })),
			new("Text size", $"{p.TextSizePercent}%", "The size of dialogue and menu text, from 100% to 150%.",
				by => _s.SetProfile(p with { TextSizePercent = Math.Clamp(p.TextSizePercent + 10 * by, 100, 150) })),
			new("Battle speed", SpeedName(p.BattleSpeed), "How fast the battle's messages play back.",
				by => _s.SetProfile(p with { BattleSpeed = Step(p.BattleSpeed, by) })),
			new("Reduced flash", OnOff(p.ReducedFlash), "Softens flashing effects. This sample draws none.",
				_ => _s.SetProfile(p with { ReducedFlash = !p.ReducedFlash })),
			new("Ambient party talk", OnOff(p.AmbientTalk), "Party members remark on places as you arrive, without stopping play.",
				_ => _s.SetProfile(p with { AmbientTalk = !p.AmbientTalk })),
			new("Names", p.ShowShippedNames ? "Shipped" : "Restored", "Show names as they first shipped, or restored to their full form.",
				_ => _s.SetProfile(p with { ShowShippedNames = !p.ShowShippedNames })),
			new("Text language", p.TextLanguage, "The language of every line and menu label the game provides.",
				by => _s.SetProfile(p with { TextLanguage = languages[(Math.Max(0, languages.ToList().IndexOf(p.TextLanguage)) + by + languages.Count) % languages.Count] })),
			new("Music volume", $"{p.MusicVolume}", "Stored for the audio layer; this sample plays no sound yet.",
				by => _s.SetProfile(p with { MusicVolume = Volume(p.MusicVolume, by) })),
			new("Ambient volume", $"{p.AmbientVolume}", "Stored for the audio layer; this sample plays no sound yet.",
				by => _s.SetProfile(p with { AmbientVolume = Volume(p.AmbientVolume, by) })),
			new("Effects volume", $"{p.EffectsVolume}", "Stored for the audio layer; this sample plays no sound yet.",
				by => _s.SetProfile(p with { EffectsVolume = Volume(p.EffectsVolume, by) })),
			new("Difficulty", _s.DisplayName(settings.Difficulty), "Kept in each save. The session names no other presets, so it is shown here only.", null),
			new("Reward multiplier", RewardName(settings.RewardMultiplier), "Multiplies the experience and money battles pay. 1x is the original amount.",
				by => _s.SetSettings(settings.Difficulty, Step(settings.RewardMultiplier, by))),
			new(ControlsRow, "", "Rebind each action's keys and gamepad buttons.", null),
		];
	}

	private void UpdateOptions(ActionInput input)
	{
		if (_controlsPage)
		{
			UpdateControlsPage(input);
			return;
		}
		List<OptionRow> rows = OptionRows();
		_optList.Move(input, rows.Count);
		OptionRow row = rows[_optList.Cursor];
		if (input.Back)
		{
			_optionsOverlay = false;
			return;
		}
		int by = input.Pressed(GameAction.Left) ? -1 : input.Pressed(GameAction.Right) || input.Confirm ? 1 : 0;
		if (row.Label == ControlsRow && input.Confirm)
		{
			_controlsPage = true;
			_optAction = null;
			_optSub.Reset();
		}
		else if (by != 0 && row.Change is not null)
		{
			Report(row.Change(by));
		}
	}

	private void UpdateControlsPage(ActionInput input)
	{
		GameAction[] actions = Enum.GetValues<GameAction>();
		if (_optAction is { } action)
		{
			_optList.Move(input, 2);
			if (input.Back)
			{
				_optAction = null;
			}
			else if (input.Confirm)
			{
				_optAction = null;
				if (_optList.Cursor == 0)
				{
					BeginCapture(action);
				}
				else if (!ClearBinding(action))
				{
					Say($"{action} is needed to use the menus, so it cannot be left unbound.");
				}
			}
			return;
		}
		_optSub.Move(input, actions.Length + 1);
		if (input.Back)
		{
			_controlsPage = false;
			_optList.Cursor = OptionRows().FindIndex(r => r.Label == ControlsRow);
			if (SaveControls())
			{
				Say("Controls saved.");
			}
		}
		else if (input.Confirm && _optSub.Cursor == actions.Length)
		{
			_confirm.Ask("Reset every control to its default?", ResetControls);
		}
		else if (input.Confirm)
		{
			_optAction = actions[_optSub.Cursor];
			_optList.Reset();
		}
	}

	private void DrawOptions(IDrawSurface s, Art art)
	{
		if (_s.Mode == Mode.Title)
		{
			DrawTitleScene(s, art);
			s.Fill(0, 0, s.Width, s.Height, Color.Black * 0.5f);
		}
		else
		{
			DrawBackdrop(s, art);
		}
		int x = 80;
		int w = s.Width - 160;
		int y = 50;
		int h = s.Height - 100;
		Style.Window(s, x, y, w, h, _controlsPage ? "Controls" : "Options");
		int footH = Px(64);
		int listY = y + Px(30);
		int listH = h - (listY - y) - footH - 10;
		int ix = x + 40;
		int iw = w - 70;
		if (_controlsPage)
		{
			DrawControlsPage(s, ix, listY, iw, listH);
		}
		else
		{
			List<OptionRow> rows = OptionRows();
			int valueX = ix + iw * 55 / 100;
			_optList.Draw(s, ix, listY, iw, listH, Px(30), rows.Count, (i, ry, on) =>
			{
				OptionRow row = rows[i];
				Style.ListRow(s, row.Label, ix, ry, iw - 14, on, row.Change is not null || row.Label == ControlsRow, height: Px(28));
				string value = row.Value.Length == 0 ? "" : on && row.Change is not null ? $"<  {row.Value}  >" : row.Value;
				s.Text(value, valueX, ry, on ? Style.Accent : Style.Text);
			});
			OptionRow selected = rows[_optList.Cursor];
			_probeSelected = $"{selected.Label}: {selected.Value}".TrimEnd(' ', ':');
			_probeTop = _optList.Top;
			foreach ((string l, int i) in Style.Wrap(s, selected.Description, iw, Style.Small).Take(2).Select((l, i) => (l, i)))
			{
				s.Text(l, ix, y + h - footH - Px(4) + i * Px(22), Style.Dim, Style.Small);
			}
		}
		DrawToasts(s, x + 24, y + h - footH - Px(20), right: true);
		Widgets.Hints(s, ix, y + h - Px(24) - 10, iw, _controlsPage
			? $"{Hint(GameAction.Up)}/{Hint(GameAction.Down)} choose   {Hint(GameAction.Confirm)} change   {Hint(GameAction.Back)} save and back"
			: $"{Hint(GameAction.Up)}/{Hint(GameAction.Down)} choose   {Hint(GameAction.Left)}/{Hint(GameAction.Right)} or {Hint(GameAction.Confirm)} change   {Hint(GameAction.Back)} back");
	}

	private void DrawControlsPage(IDrawSurface s, int x, int y, int w, int h)
	{
		GameAction[] actions = Enum.GetValues<GameAction>();
		IReadOnlyList<ControlBinding> controls = Controls();
		int keysX = x + w * 26 / 100;
		int buttonsX = x + w * 60 / 100;
		s.Text("Keys", keysX, y, Style.Dim, Style.Small);
		s.Text("Gamepad", buttonsX, y, Style.Dim, Style.Small);
		int listY = y + Px(28);
		_optSub.Draw(s, x, listY, w, h - Px(28), Px(30), actions.Length + 1, (i, ry, on) =>
		{
			if (i == actions.Length)
			{
				Style.ListRow(s, ResetRow, x, ry, w - 14, on, height: Px(28));
				return;
			}
			Style.ListRow(s, actions[i].ToString(), x, ry, w - 14, on, height: Px(28));
			s.Text(Style.Fit(s, controls[i].KeyNames, buttonsX - keysX - 20, Style.Small), keysX, ry + Px(2), on ? Style.Accent : Style.Text, Style.Small);
			s.Text(Style.Fit(s, controls[i].ButtonNames, x + w - buttonsX - 30, Style.Small), buttonsX, ry + Px(2), on ? Style.Accent : Style.Text, Style.Small);
		}, _optAction is null && Capturing is null);
		_probeSelected = _optSub.Cursor == actions.Length ? ResetRow : actions[_optSub.Cursor].ToString();
		_probeTop = _optSub.Top;
		if (_optAction is { } action)
		{
			int bw = Px(320);
			int bh = Px(110);
			int bx = x + w - bw - 20;
			int by = y + Px(40);
			Style.Window(s, bx, by, bw, bh, action.ToString());
			for (int i = 0; i < 2; i++)
			{
				Style.ListRow(s, i == 0 ? "Rebind" : "Clear", bx + 30, by + Px(28) + i * Px(32), bw - 50, i == _optList.Cursor, height: Px(28));
			}
			_probeSelected = _optList.Cursor == 0 ? "Rebind" : "Clear";
		}
		if (Capturing is { } capturing)
		{
			string ask = $"Press a key or a gamepad button for {capturing}. Esc cancels.";
			int bw = s.Measure(ask) + Px(60);
			int bx = (s.Width - bw) / 2;
			int by = s.Height / 2 - Px(40);
			Style.Window(s, bx, by, bw, Px(80), "Rebind");
			s.Text(ask, bx + Px(30), by + Px(28), Style.Text);
		}
	}
}
