using Psiv.Session;

namespace Psiv.Sample;

// What plays: a script's music and ambient loop while its event runs, otherwise the battle's or the area's track,
// and named cues for the sample's own actions and the battle's events. Tracks come from the data first (an area's
// music, an encounter's, game.json's title, victory and defeat tracks); where the data names none they are derived
// from what the session reports: `<kind>_<area>` (town_tidewater), `battle_<kind>` for visible and random battles,
// `battle_scripted` for an event's battle, `title`, and nothing on a battle's result screen.
public sealed partial class SampleController
{
	// The layout's `music_fade_ms`: a track's cross-fade into the next, 0 cutting.
	private int CrossfadeMs => Timing("music_fade_ms");
	private const int StopFadeMs = 150;
	// Rate limits: one reveal tick per this much frame time, and one footstep per this much.
	private const int StepCueMs = 200;

	private readonly IAudioOut _audio;
	// What a script set while its event runs: null when it set nothing (the area's music plays), "" when it stopped it.
	private string? _scriptMusic;
	private string? _scriptAmbient;
	private bool _refused;
	private int _lastTextTickMs = int.MinValue / 2;
	private int _lastStepMs = int.MinValue / 2;
	private (string Area, int X, int Y) _leaderCell;

	public IAudioOut Audio => _audio;

	private static readonly Screen[] MenuScreens =
		[Screen.Menu, Screen.Shop, Screen.Inn, Screen.SavePrompt, Screen.NameEntry, Screen.QuestLog, Screen.TextLog, Screen.Options, Screen.Pause, Screen.LoadGame, Screen.SoundTest, Screen.Extras];

	// The cue for a battle event, played as its line shows.
	private static string? BattleCue(BattleEvent e, int partyCount) => e.Type switch
	{
		BattleEventType.DamageDealt => "hit",
		BattleEventType.Missed => "miss",
		BattleEventType.Critical => "critical",
		BattleEventType.Healed or BattleEventType.Revived => "heal",
		BattleEventType.StatusApplied or BattleEventType.StatModified => "status",
		BattleEventType.CombatantDefeated when e.Target >= partyCount => "enemy_down",
		BattleEventType.BattleEnded when e.GameOver => "game_over",
		BattleEventType.BattleEnded when e.Outcome == Outcome.Victory => "victory",
		_ => null,
	};

	private static string? NoticeCue(Notice n) => n.Key switch
	{
		"game.saved" => "save",
		"game.level_up" => "level_up",
		_ => null,
	};

	// A script's audio action: music and ambient switch at once when instant (the rebuild after a skip or a load) and
	// otherwise fade; a cue that is instant was skipped, so nothing plays. Returns how long the event waits.
	private int PresentAudio(PresentationAction a)
	{
		int fade = a.Instant ? 0 : CrossfadeMs;
		switch (a.Type)
		{
		case ActionType.MusicPlay:
			_scriptMusic = a.Subject;
			_audio.PlayMusic(a.Subject, fade);
			return 0;
		case ActionType.MusicStop:
			_scriptMusic = "";
			_audio.StopMusic(a.Instant ? 0 : StopFadeMs);
			return 0;
		case ActionType.MusicFade:
			_scriptMusic = "";
			int ms = a.Instant ? 0 : Math.Max(1, (int)(a.Seconds * 1000));
			_audio.StopMusic(ms);
			return ms;
		case ActionType.AmbientPlay:
			_scriptAmbient = a.Subject;
			_audio.PlayAmbient(a.Subject, fade);
			return 0;
		case ActionType.AmbientStop:
			_scriptAmbient = "";
			_audio.StopAmbient(a.Instant ? 0 : StopFadeMs);
			return 0;
		default:
			if (!a.Instant)
			{
				_audio.PlayCue(a.Subject);
			}
			return 0;
		}
	}

	private GameInfo? _gameInfo;

	private string MusicFor()
	{
		GameInfo game = _gameInfo ??= _s.GameInfo();
		if (_s.Mode == Mode.Title && !_battle.Active)
		{
			if (_soundTestTrack.Length > 0)
			{
				return _soundTestTrack;
			}
			return game.TitleMusic.Length > 0 ? game.TitleMusic : "title";
		}
		// The credits' own track, while they play, over the scene's.
		if (_s.Mode == Mode.Menu && _s.MenuKind == MenuKind.Credits && _s.Credits() is { Music.Length: > 0 } credits)
		{
			return credits.Music;
		}
		if (_scriptMusic is not null)
		{
			return _scriptMusic;
		}
		FieldInfo? f = _s.FieldInfo();
		if (_battle.Active)
		{
			BattleInfo? battle = _s.BattleInfo();
			if (_battle.Phase == BattlePhase.Ended)
			{
				return battle?.GameOver == true ? game.DefeatMusic : battle?.Outcome is Outcome.Victory or Outcome.ScriptedEnd ? game.VictoryMusic : "";
			}
			if (battle?.Music.Length > 0)
			{
				return battle.Music;
			}
			string kind = FieldRenderer.KindName(f?.AreaKind ?? AreaKind.Field).ToLowerInvariant();
			return battle?.Source == BattleSource.Event ? "battle_scripted" : "battle_" + kind;
		}
		return f is null ? "" : f.Music.Length > 0 ? f.Music : $"{FieldRenderer.KindName(f.AreaKind).ToLowerInvariant()}_{f.AreaId}";
	}

	// After each update: the music and ambient loop for where play stands, the profile's volumes, the cues for what
	// the input did on the screen it reached, and footsteps.
	private void UpdateAudio(int dtMs, Screen before, Screen after, ActionInput input)
	{
		Profile p = _s.Profile();
		_audio.SetVolumes(p.MusicVolume, p.AmbientVolume, p.EffectsVolume);
		string music = MusicFor();
		if (music.Length == 0)
		{
			_audio.StopMusic(StopFadeMs);
		}
		else
		{
			_audio.PlayMusic(music, CrossfadeMs);
		}
		if (string.IsNullOrEmpty(_scriptAmbient))
		{
			_audio.StopAmbient(StopFadeMs);
		}
		else
		{
			_audio.PlayAmbient(_scriptAmbient, CrossfadeMs);
		}

		bool menuBefore = Array.IndexOf(MenuScreens, before) >= 0, menuAfter = Array.IndexOf(MenuScreens, after) >= 0;
		if (menuBefore != menuAfter)
		{
			_audio.PlayCue(menuAfter ? "menu_open" : "menu_close");
		}
		else if (menuBefore || before == Screen.Title || (before == Screen.Dialogue && _s.Choice() is not null) ||
			(before == Screen.Battle && Choosing(_battle.Phase)))
		{
			if (_refused)
			{
				_audio.PlayCue("refuse");
			}
			else if (input.Confirm)
			{
				_audio.PlayCue("confirm");
			}
			else if (input.Back)
			{
				_audio.PlayCue("back");
			}
			else if (input.Pressed(GameAction.Up) || input.Pressed(GameAction.Down) || input.Pressed(GameAction.Left) || input.Pressed(GameAction.Right))
			{
				_audio.PlayCue("cursor");
			}
		}
		else if (_refused)
		{
			_audio.PlayCue("refuse");
		}
		_refused = false;

		if (after == Screen.Field && _s.FieldInfo() is { } field)
		{
			(string, int, int) cell = (field.AreaId, field.LeaderX, field.LeaderY);
			if (cell != _leaderCell && cell.Item1 == _leaderCell.Area && _clockMs - _lastStepMs >= StepCueMs)
			{
				_audio.PlayCue("step");
				_lastStepMs = _clockMs;
			}
			_leaderCell = cell;
		}
		_audio.Update(dtMs);
	}
}
