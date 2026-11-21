using Psiv.Session;

namespace Psiv.Sample;

// The travel destination screen: a port's other open ports, or where an item or technique from the field menu can take
// the party (a visited town of the region, the way out of a dungeon).
public sealed partial class SampleController
{
	// A travelling item or technique picked on the field menu's Items or Status page, waiting for its destination.
	private (TravelKind Kind, string? User, string Thing)? _travelPick;
	private readonly ScrollList _travelList = new();

	// From the field menu: the travel screen when there is somewhere to go, else the session's refusal as a toast.
	private void PickTravel(TravelKind kind, string? user, string thing)
	{
		if (_s.TravelDestinations(kind).Count == 0)
		{
			Report(_s.Travel(kind, null, user, thing));
			return;
		}
		_travelPick = (kind, user, thing);
		_travelList.Reset();
	}

	private IReadOnlyList<TravelDestination> TravelRows() =>
		_s.MenuKind == MenuKind.Port ? _s.TravelDestinations(TravelKind.Port) : _travelPick is { } pick ? _s.TravelDestinations(pick.Kind) : [];

	private void UpdateTravel(ActionInput input)
	{
		IReadOnlyList<TravelDestination> rows = TravelRows();
		bool port = _s.MenuKind == MenuKind.Port;
		_travelList.Move(input, rows.Count + 1);
		bool leave = input.Back || (input.Confirm && _travelList.Cursor == rows.Count);
		if (leave)
		{
			if (port)
			{
				Report(_s.CloseMenu());
			}
			_travelPick = null;
			return;
		}
		if (input.Confirm)
		{
			TravelDestination to = rows[_travelList.Cursor];
			PsivResult result = port ? _s.Travel(TravelKind.Port, to.Id) : _s.Travel(_travelPick!.Value.Kind, to.Id, _travelPick.Value.User, _travelPick.Value.Thing);
			Report(result);
			if (result.Ok)
			{
				_travelPick = null;
			}
		}
	}

	private void DrawTravel(IDrawSurface s, IDrawSurface t, Art art)
	{
		bool port = _s.MenuKind == MenuKind.Port;
		IReadOnlyList<TravelDestination> rows = TravelRows();
		// At a ship's helm the same list is the ship's ports.
		bool helm = port && _s.FieldInfo()?.AreaKind == AreaKind.Vessel;
		// The cursor where the list will show it (the rows and the leave row), for the description under it.
		_travelList.Cursor = Math.Clamp(_travelList.Cursor, 0, rows.Count);
		string detail = _travelList.Cursor < rows.Count ? T("ui.travel.arrive", rows[_travelList.Cursor].AreaName) : T(port ? "ui.travel.stay" : "ui.travel.keep_menu");
		Ui.Draw("travel", s, t, SetDescription(UiData(), detail)
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art, "travel")))
			.Set("title", helm ? T("ui.travel.ship") : port ? T("ui.travel.ferry") : _travelPick is { } pick ? _s.DisplayName(pick.Thing) : T("ui.travel.title"))
			.Set("question", T(helm ? "ui.travel.ship_question" : port ? "ui.travel.ferry_question" : _travelPick?.Kind == TravelKind.DungeonExit ? "ui.travel.exit_question" : "ui.travel.town_question"))
			.Set("labels", rows.Select(d => d.AreaName + (d.Region.Length > 0 ? "   " + T("ui.travel.region", d.Region) : "")).Append(T(port ? "ui.travel.leave" : "ui.travel.cancel")).ToList())
			.Set("list", _travelList)
			.Set("hint", T(port ? "ui.travel.hint_port" : "ui.travel.hint", Hint(GameAction.Confirm), Hint(GameAction.Back)))
			.Set("toasts", (UiSlot)((x, y, _, _) => DrawToasts(s, x, y, right: true))));
	}

	// The probe `destinations`: what the travel screen lists, as area ids.
	private string TravelProbe() => Screen == Screen.Travel ? string.Join(", ", TravelRows().Select(d => d.Area)) : "-";
}
