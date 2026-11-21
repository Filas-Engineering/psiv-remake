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
			.. RealtimeFieldRows(),
			new(ControlsRow, "", T("ui.options.controls_help"), null),
		];
	}

	// Only while the real-time field input is on, since nothing else reads them.
	private IEnumerable<OptionRow> RealtimeFieldRows()
	{
		if (!_realtimeField)
		{
			yield break;
		}
		yield return new(FieldText("ui.options.always_run"), OnOffText(_bindings.AlwaysRun), FieldText("ui.options.always_run_help"),
			_ => { _bindings.AlwaysRun = !_bindings.AlwaysRun; return SaveFieldOptions(); });
		yield return new(FieldText("ui.options.interact_prompt"), OnOffText(_bindings.InteractPrompt), FieldText("ui.options.interact_prompt_help"),
			_ => { _bindings.InteractPrompt = !_bindings.InteractPrompt; return SaveFieldOptions(); });
	}

	// The two options live in controls.json beside the bindings, saved at once like the profile's settings.
	private PsivResult SaveFieldOptions()
	{
		SaveControls();
		return PsivResult.Success;
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
		GameAction[] actions = ControlActions();
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

	private void DrawOptions(IDrawSurface s, IDrawSurface t, Art art)
	{
		UiContext data = UiData()
			.Set("title_mode", _s.Mode == Mode.Title)
			.Set("scene", (UiSlot)((x, y, w, h) => DrawTitleScene(t, art, x, y, w, h)))
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art, "options")))
			.Set("title", T(_controlsPage ? "ui.options.controls" : "ui.options.title"))
			.Set("on_controls", _controlsPage)
			.Set("controls", (UiSlot)((x, y, w, h) => DrawControlsPage(s, t, x, y, w, h)))
			.Set("toasts", (UiSlot)((x, y, w, h) => DrawToasts(t, t.Width - x - w, y + h, right: true)))
			.Set("hint", _controlsPage
				? T("ui.options.hint_controls", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back))
				: T("ui.options.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Left), Hint(GameAction.Right), Hint(GameAction.Confirm), Hint(GameAction.Back)));
		if (!_controlsPage)
		{
			List<OptionRow> rows = OptionRows();
			data.Set("rows", rows.Select(row => new UiContext(data)
					.Set("text", $"{row.Label}: {row.Value}".TrimEnd(' ', ':'))
					.Set("label", row.Label)
					.Set("value", row.Value)
					.Set("changeable", row.Change is not null)
					.Set("enabled", row.Change is not null || row.Label == ControlsRow)).ToList())
				.Set("list", _optList)
				.Set("description", rows[Math.Clamp(_optList.Cursor, 0, rows.Count - 1)].Description);
		}
		Ui.Draw("options", s, t, data);
	}

	private void DrawControlsPage(IDrawSurface s, IDrawSurface t, int x, int y, int w, int h)
	{
		GameAction[] actions = ControlActions();
		string resetRow = T("ui.options.reset");
		UiContext data = UiArea(x, y, w, h);
		Ui.Draw("controls", s, t, data
			.Set("actions", actions.Select(a => new UiContext(data)
					.Set("label", ActionText(a))
					.Set("keys", BindingsText(_bindings.KeysOf(a).Select(KeyText)))
					.Set("buttons", BindingsText(_bindings.ButtonsOf(a).Select(ButtonText)))
					.Set("reset", false))
				.Append(new UiContext(data).Set("label", resetRow).Set("reset", true)).ToList())
			.Set("list", _optSub)
			.Set("active", _optAction is null && Capturing is null)
			.Set("picking", _optAction is not null)
			.Set("action_name", _optAction is { } picked ? ActionText(picked) : "")
			.Set("choices", new List<string> { T("ui.options.rebind"), T("ui.options.clear") })
			.Set("choice", _optList.Cursor)
			.Set("capturing", Capturing is not null)
			.Set("ask", Capturing is { } capturing ? T("ui.options.press_for", ActionText(capturing), KeyText(Keys.Escape)) : ""));
		_probeSelected = _optSub.Cursor == actions.Length ? resetRow : ActionText(actions[_optSub.Cursor]);
		_probeTop = _optSub.Top;
		if (_optAction is not null)
		{
			_probeSelected = T(_optList.Cursor == 0 ? "ui.options.rebind" : "ui.options.clear");
		}
	}
}
