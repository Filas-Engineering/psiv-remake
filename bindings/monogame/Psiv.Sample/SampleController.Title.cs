using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private static readonly string LibraryVersion = GameSession.LibraryVersion;
	private string _soundTestTrack = "";
	// Whether the story was finished, read once each time the title comes back.
	private bool? _clearRecord;

	private List<(string Label, Func<PsivResult> Run)> TitleOptions()
	{
		var options = new List<(string, Func<PsivResult>)>();
		if (_s.WorldInfo().CanRetry)
		{
			options.Add((T("ui.title.retry"), _s.RetryBattle));
		}
		options.Add((T("ui.title.new_game"), _s.NewGame));
		options.Add((T("ui.title.continue"), _s.ContinueGame));
		options.Add((T("ui.title.load"), () =>
		{
			_loadOverlay = true;
			_list.Reset();
			return PsivResult.Success;
		}));
		_clearRecord ??= _s.ClearRecordExists();
		if (_clearRecord == true)
		{
			options.Add((T("ui.title.sound_test"), () =>
			{
				_soundTestOverlay = true;
				_list.Reset();
				return PsivResult.Success;
			}));
		}
		options.Add((T("ui.title.options"), () =>
		{
			OpenOptions();
			return PsivResult.Success;
		}));
		options.Add((T("ui.title.quit"), () =>
		{
			QuitRequested = true;
			return PsivResult.Success;
		}));
		return options;
	}

	private void UpdateTitle(ActionInput input)
	{
		var options = TitleOptions();
		Move(input, options.Count);
		if (input.Confirm)
		{
			Report(options[_cursor].Run());
		}
	}

	// The field scene behind the title, the load list and the title's options.
	private void DrawTitleScene(IDrawSurface s, Art art)
	{
		Art.TileSet tiles = art.Tiles(Theme.Field);
		static CellFlags Terrain(int x, int y) => y <= 1 ? CellFlags.Wall : y is 11 or 12 ? CellFlags.Water : CellFlags.Floor;
		int frame = tiles.WaterFrame(_clockMs);
		for (int y = 0; y * Art.Tile < s.Height; y++)
		{
			for (int x = 0; x * Art.Tile < s.Width; x++)
			{
				FieldRenderer.DrawCell(s, tiles, Terrain, x, y, x * Art.Tile, y * Art.Tile, Noise.Hash(x, y, 7), frame);
			}
		}
		s.Fill(0, 0, s.Width, s.Height, Style.Background * 0.35f);
	}

	private void DrawTitle(IDrawSurface s, IDrawSurface t, Art art)
	{
		var options = TitleOptions();
		_probeSelected = options[Math.Clamp(_cursor, 0, options.Count - 1)].Label;
		int walk = 1 + _clockMs / 260 % 2;
		Ui.Draw("title", s, t, UiData()
			.Set("scene", (UiSlot)((_, _, _, _) => DrawTitleScene(t, art)))
			.Set("title", _s.GameInfo().Title)
			.Set("subtitle", T("ui.title.subtitle"))
			.Set("hero_frame", art.ActorFrame(Art.Hero, Direction.East, walk == 1 ? 1 : 0))
			.Set("creature_frame", art.Creature(Noise.Hash("title"), _clockMs / 400 % 2))
			.Set("game_over", _s.WorldInfo().GameOver)
			.Set("options", options.Select(o => o.Label).ToList())
			.Set("cursor", _cursor)
			.Set("version", LibraryVersion)
			.Set("toasts", (UiSlot)((_, _, _, _) => DrawToasts(t, 16, t.Height - 40, right: true)))
			.Set("hint", T("ui.title.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm))));
	}

	// F15: every track the data lists, once the story has been finished. Confirm plays one; back returns to the title
	// and its own music.
	private void UpdateSoundTest(ActionInput input)
	{
		IReadOnlyList<string> tracks = _s.AudioIds();
		_list.Move(input, tracks.Count);
		if (input.Back)
		{
			_soundTestOverlay = false;
			_soundTestTrack = "";
		}
		else if (input.Confirm && tracks.Count > 0)
		{
			_soundTestTrack = tracks[_list.Cursor];
		}
	}

	private void DrawSoundTest(IDrawSurface s, IDrawSurface t, Art art)
	{
		Ui.Draw("sound_test", s, t, UiData()
			.Set("scene", (UiSlot)((_, _, _, _) => DrawTitleScene(t, art)))
			.Set("tracks", _s.AudioIds().Select(id => T("ui.track." + id)).ToList())
			.Set("list", _list)
			.Set("playing", _soundTestTrack.Length > 0 ? T("ui.track." + _soundTestTrack) : "")
			.Set("hint", T("ui.sound_test.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back))));
	}
}
