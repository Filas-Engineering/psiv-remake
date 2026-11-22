using Psiv.Session;

namespace Psiv.Sample;

public sealed partial class SampleController
{
	private void UpdateShop(ActionInput input)
	{
		if (input.Pressed(GameAction.PagePrev) || input.Pressed(GameAction.PageNext))
		{
			_tab = 1 - _tab;
			_list.Reset();
		}
		if (input.Back)
		{
			_keeperPoint = null;
			Report(_s.CloseMenu());
			return;
		}
		ShopInfo? shop = _s.Shop();
		KeeperOpened(shop?.Id, "welcome");
		if (shop is null)
		{
			return;
		}
		var list = _tab == 0 ? shop.Stock.ToList() : _s.Inventory().Where(i => i.Price >= 0).ToList();
		_list.Move(input, list.Count);
		if (input.Confirm && list.Count > 0)
		{
			PsivResult traded = _tab == 0 ? _s.Buy(list[_list.Cursor].Id) : _s.Sell(list[_list.Cursor].Id);
			KeeperAnswered(traded);
			Report(traded);
		}
	}

	private void DrawShop(IDrawSurface s, IDrawSurface t, Art art)
	{
		ShopInfo? shop = _s.Shop();
		KeeperOpened(shop?.Id, "welcome");
		UiContext ctx = UiData()
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art, "shop")))
			.Set("title", shop is { } open ? T(NameKind.ShopKind, (int)open.Kind) : T("ui.shop.title"))
			.Set("open", shop is not null)
			.Set("tab", _tab)
			.Set("money_value", _s.WorldInfo().Money)
			.Set("keeper", shop is null ? -1 : Keeper(art, shop.Id))
			.Set("voice_line", shop is null ? "" : VoiceLine(shop.Voice, "shop", _keeperMoment));
		List<InventoryEntry> entries = shop is null ? [] : _tab == 0 ? shop.Stock.ToList() : _s.Inventory().Where(i => i.Price >= 0).ToList();
		if (shop is not null)
		{
			ctx.Set("tabs", new List<string> { T("ui.shop.buy"), T("ui.shop.sell") })
				.Set("money", T("ui.common.money", _s.WorldInfo().Money))
				.Set("stall", AreaTiles(art).Stall)
				.Set("hint", T("ui.shop.hint", Hint(GameAction.PagePrev), Hint(GameAction.PageNext), Hint(GameAction.Confirm), Hint(GameAction.Back)))
				.Set("toasts", (UiSlot)((x, y, _, _) => DrawToasts(t, x, y, right: true)));
		}
		ctx.Set("rows", entries.Select(i => new UiContext(ctx).Set("text", i.Name).Set("price", _tab == 0 ? i.Price : i.Price * shop!.SellPercent / 100).Set("have", i.Count)).ToList())
			.Set("list", _list)
			.Set("compare", "");
		if (entries.Count > 0)
		{
			InventoryEntry selected = entries[Math.Clamp(_list.Cursor, 0, entries.Count - 1)];
			SetDescription(ctx, DescriptionOf(selected));
			if (selected.IsEquipment && LineupMembers().FirstOrDefault() is { } leader && _s.EquipPreview(leader.Id, selected.Id) is { } preview)
			{
				ctx.Set("compare", $"{BonusText(preview.Bonus)}   " + WearersText(preview.WearerNames));
			}
		}
		Ui.Draw("shop", s, t, ctx);
		if (entries.Count > 0)
		{
			_probeSelected = entries[_list.Cursor].Name;
		}
	}

	// The portrait the assets' `keepers` give a shop or rest point, -1 without one.
	private int Keeper(Art art, string point)
	{
		string? keeper = _assets?.Manifest.Keepers.GetValueOrDefault(point);
		_probeKeeper = keeper ?? "-";
		return keeper is null ? -1 : art.Portrait(keeper, keeper);
	}

	// The keeper the last shop or inn frame showed, `-` for none.
	private string _probeKeeper = "-";

	// The shop or rest point open and what its keeper says now: `welcome` (a shop opening), `confirm` (a rest point
	// opening), `thanks` after a trade, `cannot_afford` after a refusal for money.
	private string? _keeperPoint;
	private string _keeperMoment = "welcome";

	private void KeeperOpened(string? point, string moment)
	{
		if (point != _keeperPoint)
		{
			_keeperPoint = point;
			_keeperMoment = moment;
		}
	}

	private void KeeperAnswered(PsivResult result) =>
		_keeperMoment = result.Ok ? "thanks" : result.Error!.Key == "game.refuse.no_money" ? "cannot_afford" : _keeperMoment;

	// The keeper's line: VOICE.MOMENT when the point has a voice and the tables have that key, else ui.shop.MOMENT or
	// ui.inn.MOMENT.
	private string VoiceLine(string voice, string kind, string moment, params object[] parameters)
	{
		string[] values = [.. parameters.Select(p => Convert.ToString(p, System.Globalization.CultureInfo.InvariantCulture) ?? "")];
		string key = voice + "." + moment;
		string own = voice.Length > 0 ? _s.FormatText(key, values) : key;
		_probeVoice = own != key ? key : $"ui.{kind}.{moment}";
		return own != key ? own : T($"ui.{kind}.{moment}", parameters);
	}

	// The text key of the keeper's line the last shop or inn frame showed.
	private string _probeVoice = "-";

	// The description fields of a highlighted row, at most two small lines; the session gives "" when the data has none.
	private UiContext SetDescription(UiContext ctx, string? text)
	{
		_probeDescription = string.IsNullOrEmpty(text) ? "-" : text;
		return ctx.Set("description", string.IsNullOrEmpty(text) ? T("ui.common.no_description") : text).Set("description_missing", string.IsNullOrEmpty(text));
	}
}
