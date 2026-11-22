using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateInn(ActionInput input)
	{
		InnInfo? inn = _s.Inn();
		KeeperOpened(inn?.Id, "confirm");
		Move(input, 2);
		if (input.Back || (input.Confirm && _cursor == 1))
		{
			_keeperPoint = null;
			Report(_s.CloseMenu());
		}
		else if (input.Confirm && inn is not null)
		{
			PsivResult rested = _s.Rest();
			KeeperAnswered(rested);
			if (rested.Ok)
			{
				_keeperPoint = null;
				Say(VoiceLine(inn.Voice, "inn", "thanks", "", inn.Cost));
			}
			Report(rested);
		}
	}

	private void DrawInn(IDrawSurface s, IDrawSurface t, Art art)
	{
		bool field = DrawField(s, art);
		InnInfo? inn = _s.Inn();
		KeeperOpened(inn?.Id, "confirm");
		Ui.Draw("inn", s, t, UiData()
			.Set("top", DialogueTop(s, field))
			.Set("bed", AreaTiles(art).Bed)
			.Set("cost", inn?.Cost ?? 0)
			.Set("per_member", inn?.PerMember ?? false)
			.Set("cost_each", inn is { PerMember: true } ? inn.Cost / Math.Max(1, LineupMembers().Count) : inn?.Cost ?? 0)
			.Set("voice_line", inn is null ? "" : VoiceLine(inn.Voice, "inn", _keeperMoment, "", inn.Cost))
			.Set("money", _s.WorldInfo().Money)
			.Set("options", new List<string> { T("ui.inn.rest"), T("ui.inn.leave") })
			.Set("cursor", _cursor)
			.Set("keeper", inn is null ? -1 : Keeper(art, inn.Id)));
	}
}
