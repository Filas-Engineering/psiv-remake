using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	// The inn layout's `flow`: the steps the inn goes through, in order (default: the choice alone, with the price in its
	// box, as before).
	private static readonly List<string> InnDefaultFlow = ["choice"];

	private List<string> InnFlow => Layout.Screen("inn").Flow ?? InnDefaultFlow;

	private string _innStep = "choice";
	// A rest closes the inn in the session; a flow with a result or farewell step keeps the inn's screen up over the field
	// with the rest point it was until they are confirmed.
	private InnInfo? _innAfter;
	private bool _innRested;

	private static string InnMoment(string step) => step switch { "welcome" => "welcome", "farewell" => "farewell", _ => "confirm" };

	private void InnGo(string step)
	{
		_innStep = step;
		_keeperMoment = InnMoment(step);
	}

	// Leaving: the farewell step when the flow has one, else the inn closes.
	private void InnLeave()
	{
		if (_innStep != "farewell" && InnFlow.Contains("farewell"))
		{
			InnGo("farewell");
			return;
		}
		_keeperPoint = null;
		if (_innAfter is not null)
		{
			_innAfter = null;
			return;
		}
		Report(_s.CloseMenu());
	}

	private void UpdateInn(ActionInput input)
	{
		InnInfo? inn = _innAfter ?? _s.Inn();
		if (_innAfter is null && (inn is null || inn.Id != _keeperPoint))
		{
			_innStep = InnFlow[0];
			_innRested = false;
		}
		KeeperOpened(inn?.Id, InnMoment(_innStep));
		switch (_innStep)
		{
		case "welcome" or "price":
			if (input.Back)
			{
				InnLeave();
			}
			else if (input.Confirm)
			{
				InnGo(InnFlow[InnFlow.IndexOf(_innStep) + 1]);
			}
			break;
		case "choice":
			Move(input, 2);
			if (input.Back || (input.Confirm && _cursor == 1))
			{
				InnLeave();
			}
			else if (input.Confirm && inn is not null)
			{
				PsivResult rested = _s.Rest();
				KeeperAnswered(rested);
				bool result = InnFlow.Contains("result");
				if (rested.Ok && (result || InnFlow.Contains("farewell")))
				{
					_innAfter = inn;
					_innRested = true;
					if (!result)
					{
						InnGo("farewell");
					}
					else
					{
						_innStep = "result";
					}
				}
				else if (rested.Ok)
				{
					_keeperPoint = null;
					Say(VoiceLine(inn.Voice, "inn", "thanks", "", inn.Cost));
				}
				else if (result)
				{
					_innStep = "result";
				}
				Report(rested);
			}
			break;
		case "result":
			if (input.Confirm || input.Back)
			{
				if (_innRested)
				{
					InnLeave();
				}
				else
				{
					InnGo("choice");
				}
			}
			break;
		case "farewell":
			if (input.Confirm || input.Back)
			{
				InnLeave();
			}
			break;
		}
	}

	private void DrawInn(IDrawSurface s, IDrawSurface t, Art art)
	{
		bool field = DrawField(s, art);
		InnInfo? inn = _innAfter ?? _s.Inn();
		KeeperOpened(inn?.Id, InnMoment(_innStep));
		Ui.Draw("inn", s, t, UiData()
			.Set("top", DialogueTop(s, field))
			.Set("bed", AreaTiles(art).Bed)
			.Set("cost", inn?.Cost ?? 0)
			.Set("name", inn?.Name ?? "")
			.Set("title", inn is { Name.Length: > 0 } named ? named.Name : T("ui.inn.title"))
			.Set("per_member", inn?.PerMember ?? false)
			.Set("cost_each", inn is { PerMember: true } ? inn.Cost / Math.Max(1, LineupMembers().Count) : inn?.Cost ?? 0)
			.Set("voice_line", inn is null ? "" : VoiceLine(inn.Voice, "inn", _keeperMoment, "", inn.Cost))
			.Set("money", _s.WorldInfo().Money)
			.Set("options", new List<string> { T("ui.inn.rest"), T("ui.inn.leave") })
			.Set("cursor", _cursor)
			.Set("keeper", inn is null ? -1 : Keeper(art, inn.Id))
			.Set("step", _innStep)
			.Set("rested", _innRested));
	}
}
