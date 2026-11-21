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

	private void DrawTravel(IDrawSurface s, Art art)
	{
		DrawBackdrop(s, art);
		bool port = _s.MenuKind == MenuKind.Port;
		IReadOnlyList<TravelDestination> rows = TravelRows();
		var labels = rows.Select(d => d.AreaName + (d.Region.Length > 0 ? "   " + T("ui.travel.region", d.Region) : "")).Append(T(port ? "ui.travel.leave" : "ui.travel.cancel")).ToList();
		// A window as tall as its rows, centered above the toasts' corner.
		int w = 640;
		int rowH = Px(30);
		int descH = Px(56);
		int listH = Math.Min(labels.Count, 8) * rowH + Px(8);
		int h = Px(40) + listH + descH;
		int x = (s.Width - w) / 2;
		int y = Math.Max(96, (s.Height - h) / 2 - 60);
		string title = port ? T("ui.travel.ferry") : _travelPick is { } pick ? _s.DisplayName(pick.Thing) : T("ui.travel.title");
		Style.Window(s, x - 24, y - 40, w + 48, h + 64, title);
		string question = T(port ? "ui.travel.ferry_question" : _travelPick?.Kind == TravelKind.DungeonExit ? "ui.travel.exit_question" : "ui.travel.town_question");
		s.Text(question, x, y, Style.Accent);
		DrawItems(s, _travelList, x, y + Px(40), w, listH, labels);
		_probeSelected = labels[Math.Clamp(_travelList.Cursor, 0, labels.Count - 1)];
		string detail = _travelList.Cursor < rows.Count
			? T("ui.travel.arrive", rows[_travelList.Cursor].AreaName)
			: T(port ? "ui.travel.stay" : "ui.travel.keep_menu");
		s.Fill(x, y + h - descH, w - 14, 1, Style.PanelEdge * 0.4f);
		DrawDescription(s, detail, x + 14, y + h - descH + Px(8), w - 40);
		DrawHint(s, T(port ? "ui.travel.hint_port" : "ui.travel.hint", Hint(GameAction.Confirm), Hint(GameAction.Back)));
	}

	// The probe `destinations`: what the travel screen lists, as area ids.
	private string TravelProbe() => Screen == Screen.Travel ? string.Join(", ", TravelRows().Select(d => d.Area)) : "-";
}
