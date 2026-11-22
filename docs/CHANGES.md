
## Sample

Newest first. The MonoGame sample (`bindings/monogame/Psiv.Sample`) only.

* `narration` window (layout, `ui/dialogue.json`): narration lines gather in a window without a box, `narration_lines` at once, over the picture stack.
* `narration_lines`, `narration_cps`, `narration_delay_ms`, `narration_width` (dialogue params): lines held, reveal speed at Normal scaled by the text speed, wait before each line, wrap width.
* `narration_visible`, `narration_rows`, `narration_all_shown` (dialogue fields): the window's state and rows for the layout.
* `narration` (bitmap font role and text style): the narration's font, falling back to `dialogue`.
* `narration`, `narrationreveal` (state probes): the narration rows shown and the line's reveal.
* Window content rule (layout behavior): a closing fade or slide carries its content with the frame; grow kinds show content once full size.
* `loglines` (state probe): the text log's lines as shown.
* Text log without control tags (behavior): the log shows lines as the dialogue showed them, wrapped at the small text size to `wrap_width`.
* Log layouts by frame size (built-in layouts): `quest_log`, `text_log`, `menu_log`, `menu_quests` place by `W`/`H`, unchanged at 1280x720.
* `alt/styles_tiny.json` (small example set): 192x144 styles for the logs check.
* `field_trail.txt`, `small_logs.txt`, `narration.txt`, `narration_ja.txt` (headless runs): followers after load and warp, logs at small sizes, narration in two languages.
