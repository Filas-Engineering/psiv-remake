using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateInn(ActionInput input)
	{
		Move(input, 2);
		if (input.Back || (input.Confirm && _cursor == 1))
		{
			Report(_s.CloseMenu());
		}
		else if (input.Confirm)
		{
			Report(_s.Rest());
		}
	}

	private void DrawInn(IDrawSurface s, IDrawSurface t, Art art)
	{
		bool field = DrawField(s, art);
		InnInfo? inn = _s.Inn();
		Ui.Draw("inn", s, t, UiData()
			.Set("top", DialogueTop(s, field))
			.Set("bed", AreaTiles(art).Bed)
			.Set("cost", inn?.Cost ?? 0)
			.Set("money", _s.WorldInfo().Money)
			.Set("options", new List<string> { T("ui.inn.rest"), T("ui.inn.leave") })
			.Set("cursor", _cursor));
	}
}
