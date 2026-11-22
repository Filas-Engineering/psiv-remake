using System.Globalization;

using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// The sample's own words come from the game's text tables (keys under ui.), in the profile's text language.
public sealed partial class SampleController
{
	// Keys with a word for a name; letters, digits and function keys show as themselves, and other keys by their MonoGame name.
	private static readonly HashSet<Keys> NamedKeys =
	[
		Keys.Back, Keys.Delete, Keys.Down, Keys.End, Keys.Enter, Keys.Escape, Keys.Home, Keys.Insert, Keys.Left, Keys.LeftAlt, Keys.LeftControl,
		Keys.LeftShift, Keys.PageDown, Keys.PageUp, Keys.Right, Keys.RightAlt, Keys.RightControl, Keys.RightShift, Keys.Space, Keys.Tab, Keys.Up,
	];

	// Called with each key the text table of the profile's language lacks; the key itself is shown in its place.
	public Action<string>? MissingText { get; set; }

	// The bitmap font the surface was given last, null when none (a run without bitmap fonts never gives one).
	private BitmapFont? _bitmapFont;
	// The dialogue box's lines as the last frame wrapped them: how many, and each one's measured width.
	private string _probeWrapped = "-";

	// Text in the asset directory's bitmap font for the text language and `role` from now on, if it gives one. Given every
	// time once a font was in use, since the developer previewer draws through the same surface with its own.
	private void UseTextRole(IDrawSurface s, TextRole role)
	{
		BitmapFont? font = _art?.BitmapFontFor(_s.Profile().TextLanguage, role);
		if (font is not null || _bitmapFont is not null)
		{
			s.UseBitmapFont(font);
		}
		_bitmapFont = font;
	}

	// The step from one line of wrapped text to the next at a text size: the bitmap font's line height when one draws the
	// text, else the sample's own.
	private int LineStep(int size) => _bitmapFont?.LineStep((int)MathF.Round(size * _scale)) ?? Px(Style.LineHeight);

	// The bitmap font for each role in the text language, as `ROLE FILE` or `ROLE -`; the hint only when it has a font
	// other than the menu's.
	private string BitmapFontProbe() => string.Join(", ", Enum.GetValues<TextRole>()
		.Where(r => r switch
		{
			TextRole.Hint => _art?.BitmapFontFor(_s.Profile().TextLanguage, r) != _art?.BitmapFontFor(_s.Profile().TextLanguage, TextRole.Menu),
			TextRole.Narration => _art?.BitmapFontFor(_s.Profile().TextLanguage, r) != _art?.BitmapFontFor(_s.Profile().TextLanguage, TextRole.Dialogue),
			_ => true,
		})
		.Select(r => $"{Snake(r.ToString())} {_art?.BitmapFontFor(_s.Profile().TextLanguage, r)?.Key ?? "-"}"));

	// Text from the game's tables as a screen other than the dialogue box shows it: control tags acted on as the text log
	// does (a `{var:...}` filled with its value, presentation tags dropped, a `{name}` token the glossary lacks kept as
	// written), the pages joined by a space. ShownPages keeps each `{page}`'s text apart, and ShownTagPages each page's tags
	// with it, at their places in the trimmed text.
	private string Shown(string text) => text.Contains('{') ? string.Join(" ", ShownPages(text)) : text;

	private List<string> ShownPages(string text) => [.. ShownTagPages(text).Select(page => page.Text)];

	private List<DialoguePage> ShownTagPages(string text) => [.. Parse(text, null).Select(page =>
	{
		string trimmed = page.Text.Trim();
		int lead = page.Text.Length - page.Text.TrimStart().Length;
		return new DialoguePage(trimmed, [.. page.Tags.Select(tag => tag with { At = Math.Clamp(tag.At - lead, 0, trimmed.Length) })]);
	}).Where(page => page.Text.Length > 0)];

	private string T(string key, params object[] parameters)
	{
		string text = _s.FormatText(key, [.. parameters.Select(p => Convert.ToString(p, CultureInfo.InvariantCulture) ?? "")]);
		if (text == key && SampleWords.TryGetValue(key, out string? word))
		{
			return string.Format(CultureInfo.InvariantCulture, word, parameters);
		}
		if (text == key)
		{
			MissingText?.Invoke(key);
		}
		return text;
	}

	// The library's name of an enumeration value in the text language: Element 1 is heat, so ui.element.heat.
	private string T(NameKind kind, int value) => T($"ui.{Snake(kind.ToString())}.{GameSession.NameOf(kind, value)}");

	// A built-in element's word from the sample's table; an element the data declares by the game's own name for it.
	private string ElementText(Element element) => element <= Element.Holy ? T(NameKind.Element, (int)element)
		: _s.Elements().FirstOrDefault(e => e.Index == element)?.Name ?? element.ToString();

	private static string Snake(string name) => string.Concat(name.Select((c, i) => (i > 0 && char.IsUpper(c) ? "_" : "") + char.ToLowerInvariant(c)));

	private string KeyText(Keys key) => NamedKeys.Contains(key) ? T("ui.key." + Snake(key.ToString())) : Bindings.KeyName(key);

	private string ButtonText(Buttons button) => T("ui.button." + Snake(button.ToString()));

	private string ActionText(GameAction action) => T("ui.action." + Snake(action.ToString()));

	// The sample's words for keys the game's text tables do not carry: the field options' help, the credits screen, the
	// macro editor's auto play, fallback and option actions, the macro option values, the automatic and lost-turn step
	// statuses, the title's empty erase list, the Items page's actions and discard, the battle's auto macro mark, macro
	// check, trouble and auto mode lines, the bag line, the auto mode settings, `dev rest` and `combo`, the camera
	// overlay's scene camera, the field menu list's open hint, the shop's and inn's farewell, and the travel messages. T
	// takes a table's own text for the key when it has one, else this English, without counting the key as missing.
	private static readonly Dictionary<string, string> SampleWords = new()
	{
		["ui.battle.auto_macro_row"] = "{0} (auto)",
		["ui.battle.log.auto_mode"] = "Auto-battle: {0} in trouble, {1} choices.",
		["ui.battle.log.auto_play"] = "Auto-battle plays {0} every round.",
		["ui.battle.log.trouble"] = "Trouble ({0}): {1}; automatic choice for {2}.",
		["ui.battle.log.trouble_nobody"] = "nobody",
		["ui.battle.macro_step_issue"] = "{0}: {1}",
		["ui.battle.macro_trouble"] = "This round would be in trouble.",
		["ui.battle.title.auto_policy"] = "Choices",
		["ui.battle.title.auto_trouble"] = "In trouble",
		["ui.common.bag"] = "Bag {0}/{1}",
		["ui.credits.hint"] = "{0} end the credits",
		["ui.dev.camera_scene"] = "scene at {0}",
		["ui.dev.camera_scene_back"] = "scene, back to the leader",
		["ui.dev.combo"] = "combination {0} leads the round",
		["ui.dev.rested"] = "rested: hit and technique points refilled",
		["ui.inn.farewell"] = "Safe travels.",
		["ui.items.action.discard"] = "Discard",
		["ui.items.action.look"] = "Look",
		["ui.items.action.use"] = "Use",
		["ui.items.discard_confirm"] = "Throw away {1} {0}?  {2} yes, {3} no",
		["ui.items.discard_count"] = "Throw away how many {0}?  {1} of {2}",
		["ui.macro_policy.default"] = "as the rules say",
		["ui.macro_policy.expected"] = "expected",
		["ui.macro_policy.power"] = "power",
		["ui.macro_recover_who.all"] = "everyone",
		["ui.macro_recover_who.default"] = "as the rules say",
		["ui.macro_recover_who.troubled"] = "those in trouble",
		["ui.macro_recover_who.uncovered"] = "those without a step",
		["ui.macro_step_status.automatic"] = "automatic",
		["ui.macro_step_status.lost_turn"] = "waiting (turn lost)",
		["ui.macro_trouble.continue"] = "continue",
		["ui.macro_trouble.default"] = "as the rules say",
		["ui.macro_trouble.recover"] = "recover",
		["ui.macro_trouble.stop"] = "stop",
		["ui.macro_unnamed.attack"] = "attack",
		["ui.macro_unnamed.auto"] = "automatic choice",
		["ui.macro_unnamed.default"] = "as the rules say",
		["ui.macro_unnamed.none"] = "nothing",
		["ui.macros.fallback_automatic"] = "Stuck steps: automatic choice",
		["ui.macros.fallback_basic"] = "Stuck steps: basic attack",
		["ui.macros.option.on_trouble"] = "In trouble: {0}",
		["ui.macros.option.policy"] = "Choices: {0}",
		["ui.macros.option.recover_who"] = "Recover: {0}",
		["ui.macros.option.unnamed"] = "Without a step: {0}",
		["ui.macros.options"] = "Options",
		["ui.macros.options_title"] = "The macro's options",
		["ui.macros.stop_auto_play"] = "Stop auto-battle playing it every round",
		["ui.macros.use_auto_play"] = "Auto-battle plays it every round",
		["ui.menu.hint.open"] = "{0} open",
		["ui.options.always_run_help"] = "Walk at running pace without holding Run; holding Run then walks.",
		["ui.options.auto_policy"] = "Auto-battle choices",
		["ui.options.auto_policy_help"] = "How auto-battle's automatic choices pick: by power, by the damage expected, or as the macro and rules say.",
		["ui.options.auto_trouble"] = "Auto-battle in trouble",
		["ui.options.auto_trouble_help"] = "What auto-battle does in a round where a member has fallen or a step cannot run: go on, recover, or stop.",
		["ui.options.interact_prompt_help"] = "Shows the key and a mark over what the leader can talk to, open or board.",
		["ui.shop.farewell"] = "Come again.",
		["ui.title.erase_none"] = "No saves to erase.",
		["ui.travel.arrived"] = "The party arrives at {1}.",
		["ui.travel.leaving"] = "{0} carries the party to {1}.",
};

	private string AreaKindText(AreaKind kind) => T(NameKind.AreaKind, (int)kind);

	// A name pad's sizes from the screen's `pad_*` params, each left out at the built-in size.
	private PadMetrics Pad(string screen, UiContext ctx)
	{
		PadMetrics d = TextEntry.DefaultMetrics(Px);
		int P(string name, int fallback) => Ui.Param(screen, name, ctx, fallback);
		return new PadMetrics(P("pad_box_h", d.BoxH), P("pad_gap", d.Gap), P("pad_cell_w", d.CellW), P("pad_cell_h", d.CellH), P("pad_entry_size", d.EntrySize),
			P("pad_label_size", d.LabelSize), P("pad_text_x", d.TextX), P("pad_text_y", d.TextY), P("pad_label_y", d.LabelY));
	}

	private (string Space, string Erase, string Done) PadLabels() => (T("ui.name_pad.space"), T("ui.name_pad.erase"), T("ui.name_pad.done"));

	private string ListText(IEnumerable<string> items) => string.Join(T("ui.common.separator"), items);

	private string WearersText(IReadOnlyList<string> names) => T("ui.equip.can_wear", names.Count == 0 ? T("ui.equip.nobody") : ListText(names));

	private string MemberRow(Member m) => T("ui.menu.member_row", m.Name, m.Level, m.Hp, m.MaxHp, m.Tp, m.MaxTp);

	private string BindingsText(IEnumerable<string> names) => ListText(names) is { Length: > 0 } joined ? joined : "-";

	private string OnOffText(bool on) => T(on ? "ui.common.on" : "ui.common.off");
}
