using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private List<(string Label, Func<PsivResult> Run)> TitleOptions()
	{
		var options = new List<(string, Func<PsivResult>)>();
		if (_s.WorldInfo().CanRetry)
		{
			options.Add(("Retry the battle", _s.RetryBattle));
		}
		options.Add(("New game", _s.NewGame));
		options.Add(("Continue", _s.ContinueGame));
		options.Add(("Load", () =>
		{
			_loadOverlay = true;
			_list.Reset();
			return PsivResult.Success;
		}));
		options.Add(("Options", () =>
		{
			OpenOptions();
			return PsivResult.Success;
		}));
		options.Add(("Quit", () =>
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
		int frame = _clockMs / 220 % Art.WaterFrames;
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
		const string subtitle = "A placeholder adventure played through libpsiv_session";
		s.Text(subtitle, (s.Width - s.Measure(subtitle, Style.Small)) / 2, by + Px(66), Style.Dim, Style.Small);
		int walk = 1 + _clockMs / 260 % 2;
		s.Image(art.ActorFrame(Art.Hero, Direction.East, walk == 1 ? 1 : 0), bx + 28, by + 18, 64, 64, Color.White);
		s.Image(art.Creature(Noise.Hash("title"), _clockMs / 400 % 2), bx + w - 92, by + 18, 64, 64, Color.White);
		if (_s.WorldInfo().GameOver)
		{
			const string fallen = "The party has fallen.";
			s.Text(fallen, (s.Width - s.Measure(fallen)) / 2, by + Px(96), Style.Bad);
		}
		for (int i = 0; i < options.Count; i++)
		{
			Style.ListRow(s, options[i].Label, bx + 80, by + top + i * rowH, w - 140, i == _cursor, height: Px(28));
		}
		_probeSelected = options[Math.Clamp(_cursor, 0, options.Count - 1)].Label;
		DrawToasts(s, 16, s.Height - 40, right: true);
		DrawHint(s, $"{Hint(GameAction.Up)}/{Hint(GameAction.Down)} choose   {Hint(GameAction.Confirm)} confirm");
	}
}
