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
			Report(_s.CloseMenu());
			return;
		}
		ShopInfo? shop = _s.Shop();
		if (shop is null)
		{
			return;
		}
		var list = _tab == 0 ? shop.Stock.ToList() : _s.Inventory().Where(i => i.Price >= 0).ToList();
		_list.Move(input, list.Count);
		if (input.Confirm && list.Count > 0)
		{
			Report(_tab == 0 ? _s.Buy(list[_list.Cursor].Id) : _s.Sell(list[_list.Cursor].Id));
		}
	}

	private void DrawShop(IDrawSurface s, IDrawSurface t, Art art)
	{
		ShopInfo? shop = _s.Shop();
		UiContext ctx = UiData()
			.Set("backdrop", (UiSlot)((_, _, _, _) => DrawBackdrop(t, art)))
			.Set("title", shop is { } open ? T(NameKind.ShopKind, (int)open.Kind) : T("ui.shop.title"))
			.Set("open", shop is not null)
			.Set("tab", _tab);
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

	// The description fields of a highlighted row, at most two small lines; the session gives "" when the data has none.
	private UiContext SetDescription(UiContext ctx, string? text)
	{
		_probeDescription = string.IsNullOrEmpty(text) ? "-" : text;
		return ctx.Set("description", string.IsNullOrEmpty(text) ? T("ui.common.no_description") : text).Set("description_missing", string.IsNullOrEmpty(text));
	}
}
