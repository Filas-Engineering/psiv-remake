using Psiv.Session;

namespace Psiv.Sample;

// The travel destination screen: a port's other open ports, or where an item or technique from the field menu can take
// the party (a visited town of the region, the way out of a dungeon).
public sealed partial class SampleController
{
	// A travelling item or technique picked on the field menu's Items or Status page, waiting for its destination.
	private (TravelKind Kind, string? User, string Thing)? _travelPick;
	private readonly ScrollList _travelList = new();

	// The travel layout's message before leaving and after arriving (params `leave_key`, `arrive_key`): `leave` while it
	// waits to go to `_travelTo`, `arrive` over the field after the party has gone, else "". `_travelAtOnce`: the use went
	// without its list (`one_destination` "go"), so cancelling the message leaves the menu as it was.
	private string _travelPhase = "";
	private string _travelMessage = "";
	private int _travelMessageMs;
	private TravelDestination? _travelTo;
	private bool _travelAtOnce;
	private string _travelThing = "";

	private bool TravelCancelRow => Ui.Param("travel", "cancel_row", UiData(), 1) != 0;

	// From the field menu: the travel screen when there is somewhere to go, else the session's refusal as a toast; with
	// "go" for the thing's id, else its kind, in the layout's `one_destination_for`, else its `one_destination`, one place to
	// go goes at once.
	private void PickTravel(TravelKind kind, string? user, string thing)
	{
		IReadOnlyList<TravelDestination> destinations = _s.TravelDestinations(kind);
		if (destinations.Count == 0)
		{
			Report(_s.Travel(kind, null, user, thing));
			return;
		}
		_travelPick = (kind, user, thing);
		_travelList.Reset();
		Dictionary<string, string> by = Layout.Screen("travel").OneDestination;
		string once = by.GetValueOrDefault(thing) ?? by.GetValueOrDefault(kind == TravelKind.Town ? "town" : "dungeon_exit")
			?? Ui.ParamText("travel", "one_destination", UiData(), "list");
		_travelAtOnce = destinations.Count == 1 && once == "go";
		if (_travelAtOnce)
		{
			TravelTo(destinations[0]);
		}
	}

	// The message for `key` (a param's text key), the thing's own `KEY.THING` first: {0} the thing's name, {1} the place's.
	private string? TravelMessage(string param, string thing, string place)
	{
		string key = Ui.ParamText("travel", param, UiData(), "");
		if (key.Length == 0)
		{
			return null;
		}
		string[] values = [_s.DisplayName(thing), place];
		return _s.FormatText($"{key}.{thing}", values) is var own && own != $"{key}.{thing}" ? Shown(own) : T(key, values[0], values[1]);
	}

	private void TravelTo(TravelDestination to)
	{
		if (TravelMessage("leave_key", _travelPick!.Value.Thing, to.AreaName) is { } leaving)
		{
			_travelPhase = "leave";
			_travelMessage = leaving;
			_travelMessageMs = 0;
			_travelTo = to;
			return;
		}
		TravelGo(to);
	}

	private void TravelGo(TravelDestination to)
	{
		(TravelKind kind, string? user, string thing) = _travelPick!.Value;
		PsivResult result = _s.Travel(kind, to.Id, user, thing);
		Report(result);
		_travelPhase = "";
		if (!result.Ok)
		{
			_travelPick = _travelAtOnce ? null : _travelPick;
			return;
		}
		_travelPick = null;
		if (TravelMessage("arrive_key", thing, to.AreaName) is { } arrived)
		{
			_travelPhase = "arrive";
			_travelMessage = arrived;
			_travelMessageMs = 0;
			_travelThing = thing;
		}
	}

	private IReadOnlyList<TravelDestination> TravelRows() =>
		_s.MenuKind == MenuKind.Port ? _s.TravelDestinations(TravelKind.Port) : _travelPick is { } pick ? _s.TravelDestinations(pick.Kind) : [];

	private void UpdateTravel(int dtMs, ActionInput input)
	{
		if (_travelPhase.Length > 0)
		{
			// A message goes on with Confirm, or by itself after the layout's `message_ms`; Back before leaving cancels.
			_travelMessageMs += dtMs;
			int hold = Ui.Param("travel", "message_ms", UiData(), 0);
			if (_travelPhase == "leave" && input.Back)
			{
				_travelPhase = "";
				_travelPick = _travelAtOnce ? null : _travelPick;
			}
			else if (input.Confirm || hold > 0 && _travelMessageMs >= hold)
			{
				if (_travelPhase == "leave")
				{
					TravelGo(_travelTo!);
				}
				else
				{
					_travelPhase = "";
				}
			}
			return;
		}
		IReadOnlyList<TravelDestination> rows = TravelRows();
		bool port = _s.MenuKind == MenuKind.Port;
		bool cancelRow = TravelCancelRow;
		_travelList.Move(input, rows.Count + (cancelRow ? 1 : 0));
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
			if (port)
			{
				Report(_s.Travel(TravelKind.Port, to.Id));
				return;
			}
			TravelTo(to);
		}
	}

	private void DrawTravel(IDrawSurface s, IDrawSurface t, Art art)
	{
		bool port = _s.MenuKind == MenuKind.Port;
		IReadOnlyList<TravelDestination> rows = TravelRows();
		bool cancelRow = TravelCancelRow;
		// At a ship's helm the same list is the ship's ports.
		bool helm = port && _s.FieldInfo()?.AreaKind == AreaKind.Vessel;
		// The cursor where the list will show it (the rows and the leave row), for the description under it.
		_travelList.Cursor = Math.Clamp(_travelList.Cursor, 0, cancelRow ? rows.Count : Math.Max(0, rows.Count - 1));
		string detail = _travelList.Cursor < rows.Count ? T("ui.travel.arrive", rows[_travelList.Cursor].AreaName) : T(port ? "ui.travel.stay" : "ui.travel.keep_menu");
		string thing = _travelPick?.Thing ?? (_travelPhase == "arrive" ? _travelThing : "");
		List<string> labels = [.. rows.Select(d => d.AreaName + (d.Region.Length > 0 ? "   " + T("ui.travel.region", d.Region) : ""))];
		// The layout's `over_menu`: from the field menu the menu's own windows stay under the list.
		bool overMenu = _travelPick is not null && _s.MenuKind == MenuKind.Field && Ui.Param("travel", "over_menu", UiData(), 0) != 0;
		Ui.Draw("travel", s, t, SetDescription(UiData(), detail)
			.Set("backdrop", (UiSlot)((_, _, _, _) =>
			{
				if (overMenu)
				{
					DrawUnder("travel", () => DrawMenu(s, t, art));
				}
				else
				{
					DrawBackdrop(t, art, "travel");
				}
			}))
			.Set("title", helm ? T("ui.travel.ship") : port ? T("ui.travel.ferry") : thing.Length > 0 ? _s.DisplayName(thing) : T("ui.travel.title"))
			.Set("question", T(helm ? "ui.travel.ship_question" : port ? "ui.travel.ferry_question" : _travelPick?.Kind == TravelKind.DungeonExit ? "ui.travel.exit_question" : "ui.travel.town_question"))
			.Set("labels", cancelRow ? [.. labels, T(port ? "ui.travel.leave" : "ui.travel.cancel")] : labels)
			.Set("destinations", rows.Select((d, i) => new UiContext().Set("text", labels[i]).Set("name", d.AreaName).Set("region", d.Region).Set("area", d.Area)).ToList())
			.Set("list", _travelList)
			.Set("cancel_row", cancelRow)
			.Set("phase", _travelPhase.Length > 0 ? _travelPhase : "list")
			.Set("message", _travelPhase.Length > 0 ? _travelMessage : "")
			.Set("message_ms", _travelMessageMs)
			.Set("hint", T(port ? "ui.travel.hint_port" : "ui.travel.hint", Hint(GameAction.Confirm), Hint(GameAction.Back)))
			.Set("toasts", (UiSlot)((x, y, _, _) => DrawToasts(s, x, y, right: true))));
	}

	// The probe `destinations`: what the travel screen lists, as area ids.
	private string TravelProbe() => Screen == Screen.Travel ? string.Join(", ", TravelRows().Select(d => d.Area)) : "-";
}
