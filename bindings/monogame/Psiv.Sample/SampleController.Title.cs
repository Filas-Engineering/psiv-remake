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

	private void DrawTitle(IDrawSurface s, Art art)
	{
		DrawTitleScene(s, art);
		var options = TitleOptions();
		int rowH = Px(32);
		int w = 600;
		int top = Px(132);
		int h = top + Px(44) + options.Count * rowH;
		int bx = (s.Width - w) / 2;
		int by = (s.Height - h) / 2;
		Style.Box(s, bx, by, w, h);
		string title = _s.GameInfo().Title;
		s.Text(title, (s.Width - s.Measure(title, Style.Title)) / 2, by + Px(26), Style.Accent, Style.Title);
		string subtitle = T("ui.title.subtitle");
		s.Text(subtitle, (s.Width - s.Measure(subtitle, Style.Small)) / 2, by + Px(66), Style.Dim, Style.Small);
		int walk = 1 + _clockMs / 260 % 2;
		s.Image(art.ActorFrame(Art.Hero, Direction.East, walk == 1 ? 1 : 0), bx + 28, by + 18, 64, 64, Color.White);
		s.Image(art.Creature(Noise.Hash("title"), _clockMs / 400 % 2), bx + w - 92, by + 18, 64, 64, Color.White);
		if (_s.WorldInfo().GameOver)
		{
			string fallen = T("ui.title.fallen");
			s.Text(fallen, (s.Width - s.Measure(fallen)) / 2, by + Px(96), Style.Bad);
		}
		for (int i = 0; i < options.Count; i++)
		{
			Style.ListRow(s, options[i].Label, bx + 80, by + top + i * rowH, w - 140, i == _cursor, height: Px(28));
		}
		_probeSelected = options[Math.Clamp(_cursor, 0, options.Count - 1)].Label;
		s.Text(LibraryVersion, s.Width - s.Measure(LibraryVersion, Style.Small) - 12, 8, Style.Dim, Style.Small);
		DrawToasts(s, 16, s.Height - 40, right: true);
		DrawHint(s, T("ui.title.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm)));
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

	private void DrawSoundTest(IDrawSurface s, Art art)
	{
		DrawTitleScene(s, art);
		s.Fill(0, 0, s.Width, s.Height, Color.Black * 0.5f);
		int x = 120;
		int w = s.Width - 240;
		Style.Window(s, x, 50, w, s.Height - 100, T("ui.sound_test.title"));
		List<string> rows = [.. _s.AudioIds().Select(id => T("ui.track." + id))];
		DrawItems(s, _list, x + 40, 90, w - 80, s.Height - 220, rows);
		if (_soundTestTrack.Length > 0)
		{
			s.Text(T("ui.sound_test.playing", T("ui.track." + _soundTestTrack)), x + 40, s.Height - 120, Style.Accent);
		}
		Widgets.Hints(s, x + 24, s.Height - 50 - Px(36), w - 48, T("ui.sound_test.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back)));
	}
}
