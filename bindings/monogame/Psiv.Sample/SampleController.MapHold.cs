using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// game.json's `presentation.map_load_hold_ms` (GameInfo.MapLoadHoldMs): after an area change by an exit, a warp or travel
// the screen stays black that long, then fades in, and the new area's music starts once the hold ends; after a load
// only with `map_load_hold_on_load`.
public sealed partial class SampleController
{
	private const int MapFadeInMs = 250;

	// The hold and fade still to run, and the area the last frame stood in ("" at the title, so a load is told apart).
	private int _mapHoldMs;
	private string _mapHoldArea = "";

	private void UpdateMapHold(int dtMs)
	{
		GameInfo game = _gameInfo ??= _s.GameInfo();
		if (game.MapLoadHoldMs <= 0)
		{
			return;
		}
		if (_s.Mode == Mode.Title)
		{
			_mapHoldArea = "";
			_mapHoldMs = 0;
			return;
		}
		string area = _s.FieldInfo()?.AreaId ?? _mapHoldArea;
		if (area.Length > 0 && area != _mapHoldArea)
		{
			_mapHoldMs = _mapHoldArea.Length > 0 || game.MapLoadHoldOnLoad ? game.MapLoadHoldMs + MapFadeInMs : 0;
			_mapHoldArea = area;
			return;
		}
		_mapHoldMs = Math.Max(0, _mapHoldMs - dtMs);
	}

	private void DrawMapHold(IDrawSurface s)
	{
		if (_mapHoldMs > 0 && Screen is Screen.Field or Screen.Dialogue or Screen.Travel)
		{
			float alpha = Math.Min(1f, (float)_mapHoldMs / MapFadeInMs);
			s.Fill(0, 0, s.Width, s.Height, Color.Black * alpha);
		}
	}
}
