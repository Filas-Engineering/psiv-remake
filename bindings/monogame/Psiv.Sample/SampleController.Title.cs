using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private static readonly string LibraryVersion = GameSession.LibraryVersion;
	private string _soundTestTrack = "";
	// The title art's sizes as the last title frame gave them to the layout.
	private string _probeTitleSizes = "-";
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

	// The field scene behind the title, the load list and the title's options, or the title art's background, then the
	// title art's layers, in the rectangle the layout's `scene` slot gives (clipped to it when it is not the whole screen).
	private void DrawTitleScene(IDrawSurface s, Art art, int left, int top, int width, int height)
	{
		bool clip = left != 0 || top != 0 || width != s.Width || height != s.Height;
		if (clip)
		{
			s.Clip(left, top, width, height);
		}
		if (art.Title?.Background is int background)
		{
			s.Image(background, left, top, width, height, Color.White);
		}
		else
		{
			Art.TileSet tiles = art.Tiles(Theme.Field);
			static CellFlags Terrain(int x, int y) => y <= 1 ? CellFlags.Wall : y is 11 or 12 ? CellFlags.Water : CellFlags.Floor;
			int frame = tiles.WaterFrame(_clockMs), cell = Layout.Styles.Param("field_cell");
			for (int y = 0; y * cell < height; y++)
			{
				for (int x = 0; x * cell < width; x++)
				{
					FieldRenderer.DrawCell(s, tiles, Terrain, x, y, left + x * cell, top + y * cell, Noise.Hash(x, y, 7), frame, cell);
				}
			}
			s.Fill(left, top, width, height, Style.Background * 0.35f);
		}
		foreach ((TitleLayer layer, int[] images) in art.Title?.Layers ?? [])
		{
			int w = layer.Image.FrameWidth * layer.Scale, h = layer.Image.FrameHeight * layer.Scale;
			int image = images[layer.Run.FrameAt(_clockMs, loop: true)];
			// A moving layer starts one copy before the screen's edge on its axis and repeats to the far edge.
			static (int First, int Last, int Step) Span(int at, float scroll, int clockMs, int size, int screen)
			{
				int offset = at + (int)MathF.Floor(scroll * clockMs / 1000f);
				return scroll == 0 ? (offset, offset, size) : ((offset % size + size) % size - size, screen, size);
			}
			(int x0, int x1, int dx) = Span(layer.X, layer.ScrollX, _clockMs, w, width);
			(int y0, int y1, int dy) = Span(layer.Y, layer.ScrollY, _clockMs, h, height);
			for (int y = y0; y <= y1; y += dy)
			{
				for (int x = x0; x <= x1; x += dx)
				{
					s.Image(image, left + x, top + y, w, h, Color.White);
				}
			}
		}
		if (clip)
		{
			s.Unclip();
		}
	}

	private void DrawTitle(IDrawSurface s, IDrawSurface t, Art art)
	{
		var options = TitleOptions();
		_probeSelected = options[Math.Clamp(_cursor, 0, options.Count - 1)].Label;
		int walk = 1 + _clockMs / 260 % 2;
		// At a reference height the title art's logo, traveler and creature draw at a whole multiple of their pixels; else
		// the logo at its size and the two figures in today's 64-pixel squares.
		int multiple = art.Title?.Multiple ?? 0;
		(int W, int H) Natural(Figure? figure) => multiple > 0 && figure is not null ? (art.SizeOf(figure.Images[0]).W * multiple, art.SizeOf(figure.Images[0]).H * multiple) : (64, 64);
		(int heroW, int heroH) = Natural(art.Title?.Traveler);
		(int creatureW, int creatureH) = Natural(art.Title?.Creature);
		(int logoW, int logoH) = art.Title?.Logo is int logo ? art.SizeOf(logo) : (0, 0);
		logoW *= Math.Max(1, multiple);
		logoH *= Math.Max(1, multiple);
		_probeTitleSizes = $"logo {logoW}x{logoH} traveler {heroW}x{heroH} creature {creatureW}x{creatureH} scale {multiple}";
		Ui.Draw("title", s, t, UiData()
			.Set("scene", (UiSlot)((x, y, w, h) => DrawTitleScene(t, art, x, y, w, h)))
			.Set("title", _s.GameInfo().Title)
			.Set("subtitle", T("ui.title.subtitle"))
			.Set("hero_frame", art.Title?.Traveler is { } traveler ? traveler.Images[traveler.Idle.FrameAt(_clockMs, loop: true)]
				: art.ActorFrame(Art.Hero, Direction.East, walk == 1 ? 1 : 0))
			.Set("creature_frame", art.Title?.Creature is { } creature ? creature.Images[creature.Idle.FrameAt(_clockMs, loop: true)]
				: art.Creature(Noise.Hash("title"), _clockMs / 400 % 2))
			.Set("logo", art.Title?.Logo ?? -1)
			.Set("logo_w", logoW)
			.Set("logo_h", logoH)
			.Set("hero_w", heroW)
			.Set("hero_h", heroH)
			.Set("creature_w", creatureW)
			.Set("creature_h", creatureH)
			.Set("art_scale", multiple)
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
			.Set("scene", (UiSlot)((x, y, w, h) => DrawTitleScene(t, art, x, y, w, h)))
			.Set("tracks", _s.AudioIds().Select(id => T("ui.track." + id)).ToList())
			.Set("list", _list)
			.Set("playing", _soundTestTrack.Length > 0 ? T("ui.track." + _soundTestTrack) : "")
			.Set("hint", T("ui.sound_test.hint", Hint(GameAction.Up), Hint(GameAction.Down), Hint(GameAction.Confirm), Hint(GameAction.Back))));
	}
}
