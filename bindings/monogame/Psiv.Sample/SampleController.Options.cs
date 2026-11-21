using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// Options, from the title, the pause overlay or the field menu: the profile's settings, the save's difficulty and
// reward multiplier, and a Controls page over the bindings. Left and Right (or Confirm) change the selected value.
public sealed partial class SampleController
{
	private string ControlsRow => T("ui.options.controls");

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

	private string SpeedName(BattleSpeed speed) => T(speed switch
	{
		BattleSpeed.X1 => "ui.multiplier.x1",
		BattleSpeed.X1_5 => "ui.multiplier.x1_5",
		BattleSpeed.X2 => "ui.multiplier.x2",
		_ => "ui.multiplier.x3",
	});

	private string RewardName(RewardMultiplier reward) => T(reward switch
	{
		RewardMultiplier.X1 => "ui.multiplier.x1",
		RewardMultiplier.X1_5 => "ui.multiplier.x1_5",
		_ => "ui.multiplier.x2",
	});

	// The save's difficulty: stepped through the session's presets while a game is in progress outside battle, shown
	// only otherwise. Its description is the preset's own, which says what it changes (UI-014).
	private OptionRow DifficultyRow(Settings settings)
	{
		Difficulties d = _s.Difficulties();
		int at = Math.Max(0, d.Presets.ToList().FindIndex(x => x.Id == settings.Difficulty));
		Difficulty current = d.Presets[at];
		string kept = T(d.CanChange ? "ui.options.difficulty_kept" : "ui.options.difficulty_fixed");
		return new(T("ui.options.difficulty"), current.Name, $"{kept} {current.Description}".Trim(),
			d.CanChange ? by => _s.SetSettings(d.Presets[(at + by + d.Presets.Count) % d.Presets.Count].Id, settings.RewardMultiplier) : null);
	}

	private List<OptionRow> OptionRows()
	{
		Profile p = _s.Profile();
		Settings settings = _s.Settings();
		IReadOnlyList<string> languages = _s.GameInfo().Languages;
		static int Volume(int value, int by) => Math.Clamp(value + 10 * by, 0, 100);
		return
		[
			new(T("ui.options.text_speed"), T(NameKind.TextSpeed, (int)p.TextSpeed), T("ui.options.text_speed_help"),
				by => _s.SetProfile(p with { TextSpeed = Step(p.TextSpeed, by) })),
			new(T("ui.options.auto_advance"), OnOffText(p.AutoAdvance), T("ui.options.auto_advance_help"),
				_ => _s.SetProfile(p with { AutoAdvance = !p.AutoAdvance })),
			new(T("ui.options.text_size"), $"{p.TextSizePercent}%", T("ui.options.text_size_help"),
				by => _s.SetProfile(p with { TextSizePercent = Math.Clamp(p.TextSizePercent + 10 * by, 100, 150) })),
			new(T("ui.options.battle_speed"), SpeedName(p.BattleSpeed), T("ui.options.battle_speed_help"),
				by => _s.SetProfile(p with { BattleSpeed = Step(p.BattleSpeed, by) })),
			new(T("ui.options.reduced_flash"), OnOffText(p.ReducedFlash), T("ui.options.reduced_flash_help"),
				_ => _s.SetProfile(p with { ReducedFlash = !p.ReducedFlash })),
			new(T("ui.options.ambient_talk"), OnOffText(p.AmbientTalk), T("ui.options.ambient_talk_help"),
				_ => _s.SetProfile(p with { AmbientTalk = !p.AmbientTalk })),
			new(T("ui.options.names"), T(p.ShowShippedNames ? "ui.options.names_shipped" : "ui.options.names_restored"), T("ui.options.names_help"),
				_ => _s.SetProfile(p with { ShowShippedNames = !p.ShowShippedNames })),
			new(T("ui.options.language"), p.TextLanguage, T("ui.options.language_help"),
				by => _s.SetProfile(p with { TextLanguage = languages[(Math.Max(0, languages.ToList().IndexOf(p.TextLanguage)) + by + languages.Count) % languages.Count] })),
			new(T("ui.options.music_volume"), $"{p.MusicVolume}", T("ui.options.music_volume_help"),
				by => _s.SetProfile(p with { MusicVolume = Volume(p.MusicVolume, by) })),
			new(T("ui.options.ambient_volume"), $"{p.AmbientVolume}", T("ui.options.ambient_volume_help"),
				by => _s.SetProfile(p with { AmbientVolume = Volume(p.AmbientVolume, by) })),
			new(T("ui.options.effects_volume"), $"{p.EffectsVolume}", T("ui.options.effects_volume_help"),
				by => _s.SetProfile(p with { EffectsVolume = Volume(p.EffectsVolume, by) })),
			DifficultyRow(settings),
			new(T("ui.options.reward"), RewardName(settings.RewardMultiplier), T("ui.options.reward_help"),
				_s.Difficulties().CanChange ? by => _s.SetSettings(settings.Difficulty, Step(settings.RewardMultiplier, by)) : null),
			new(ControlsRow, "", T("ui.options.controls_help"), null),
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
					Say(T("ui.options.required", ActionText(action)));
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
				Say(T("ui.options.controls_saved"));
			}
		}
		else if (input.Confirm && _optSub.Cursor == actions.Length)
		{
			_confirm.Ask(T("ui.options.reset_confirm"), ResetControls);
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
		Style.Window(s, x, y, w, h, T(_controlsPage ? "ui.options.controls" : "ui.options.title"));
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
			? T("ui.options.hint_controls", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back))
			: T("ui.options.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Left), Hint(GameAction.Right), Hint(GameAction.Confirm), Hint(GameAction.Back)));
	}

	private void DrawControlsPage(IDrawSurface s, int x, int y, int w, int h)
	{
		GameAction[] actions = Enum.GetValues<GameAction>();
		string resetRow = T("ui.options.reset");
		int keysX = x + w * 26 / 100;
		int buttonsX = x + w * 60 / 100;
		s.Text(T("ui.options.keys"), keysX, y, Style.Dim, Style.Small);
		s.Text(T("ui.options.gamepad"), buttonsX, y, Style.Dim, Style.Small);
		int listY = y + Px(28);
		_optSub.Draw(s, x, listY, w, h - Px(28), Px(30), actions.Length + 1, (i, ry, on) =>
		{
			if (i == actions.Length)
			{
				Style.ListRow(s, resetRow, x, ry, w - 14, on, height: Px(28));
				return;
			}
			string keys = BindingsText(_bindings.KeysOf(actions[i]).Select(KeyText));
			string buttons = BindingsText(_bindings.ButtonsOf(actions[i]).Select(ButtonText));
			Style.ListRow(s, ActionText(actions[i]), x, ry, w - 14, on, height: Px(28));
			s.Text(Style.Fit(s, keys, buttonsX - keysX - 20, Style.Small), keysX, ry + Px(2), on ? Style.Accent : Style.Text, Style.Small);
			s.Text(Style.Fit(s, buttons, x + w - buttonsX - 30, Style.Small), buttonsX, ry + Px(2), on ? Style.Accent : Style.Text, Style.Small);
		}, _optAction is null && Capturing is null);
		_probeSelected = _optSub.Cursor == actions.Length ? resetRow : ActionText(actions[_optSub.Cursor]);
		_probeTop = _optSub.Top;
		if (_optAction is { } action)
		{
			int bw = Px(320);
			int bh = Px(110);
			int bx = x + w - bw - 20;
			int by = y + Px(40);
			Style.Window(s, bx, by, bw, bh, ActionText(action));
			for (int i = 0; i < 2; i++)
			{
				Style.ListRow(s, T(i == 0 ? "ui.options.rebind" : "ui.options.clear"), bx + 30, by + Px(28) + i * Px(32), bw - 50, i == _optList.Cursor, height: Px(28));
			}
			_probeSelected = T(_optList.Cursor == 0 ? "ui.options.rebind" : "ui.options.clear");
		}
		if (Capturing is { } capturing)
		{
			string ask = T("ui.options.press_for", ActionText(capturing), KeyText(Keys.Escape));
			int bw = s.Measure(ask) + Px(60);
			int bx = (s.Width - bw) / 2;
			int by = s.Height / 2 - Px(40);
			Style.Window(s, bx, by, bw, Px(80), T("ui.options.rebind"));
			s.Text(ask, bx + Px(30), by + Px(28), Style.Text);
		}
	}
}
