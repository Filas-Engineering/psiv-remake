# Dialogue and text

How text is presented, what may and may not change in it, the name glossary, localization, the in-game encyclopedia, text speed and the text log. Text boxes inside live panels are placed per [EVT-028](events-and-live-panels.md). Menu screens are in [menus-and-ui.md](menus-and-ui.md).

## Script lock

| ID | Requirement | Label | Source |
|---|---|---|---|
| TXT-001 | There is no voice acting. All dialogue is shown as text | Decided | D-VOICE |
| TXT-002 | Dialogue and story text is the English text as shipped in the 1990s release | Decided | D-DIALOGUE |
| TXT-003 | Dialogue changes only in two listed ways: a mention of a renamed item, technique, skill or enemy uses the new name; and plain typos are fixed. Every instance of either goes in the dialogue change list with the line ID, old text, new text and reason | Proposed (scope) | S-SCRIPT |
| TXT-004 | A build check compares every English dialogue string with the locked shipped text. Any difference that is not in the dialogue change list fails the build | Proposed (spec) | D-DIALOGUE, S-RISKS |
| TXT-005 | Dialogue refers to renamable names through name tokens, not literal text, so the name shown is resolved at display time (TXT-016) | Proposed (spec) | S-SCRIPT |
| TXT-006 | To measure: the exact count of dialogue messages (estimated at about 2,170) and of system text entries (estimated at about 865), and every inline code in use. Method: phase 0 audit of the rights holder's script; cross-check against the cartridge text count. Feeds TXT-030 and localization budgets | To measure | S-SCRIPT |

## System text and the name glossary

| ID | Requirement | Label | Source |
|---|---|---|---|
| TXT-010 | Menu labels and the names of items, techniques, skills and enemies are restored to full names, now that length limits are gone | Decided | D-SYSTEXT |
| TXT-011 | A name changes only to restore what the length limit cut, or to make a menu clearer. It is never rebranded | Decided | D-NAMERULE |
| TXT-012 | System text that the Western release dropped or left unclear is restored or rewritten: in-battle status messages, unclear feedback messages (for example a character with no usable field skill, or the label for discarding an item), missing descriptions for late-game equipment, and abbreviated location labels, menu choices and soundtrack titles. Restored text is written from the Japanese source | Proposed (scope) | S-FAN-TEXT |
| TXT-013 | The Japanese original is the reference for what a restored name should be | Proposed (scope) | S-SCRIPT |
| TXT-014 | Every renamed string has a glossary entry: ID, shipped name, new name, reason, pronunciation, sign-off. A build check fails if a system text name differs from its shipped name and has no signed-off entry | Proposed (scope) | S-SCRIPT, S-RISKS |
| TXT-015 | The glossary is published outside the game and also readable in the encyclopedia (TXT-040) | Proposed (scope) | S-SCRIPT |
| TXT-016 | "Show shipped names" setting: when on, every name that has a glossary entry shows its shipped name, in menus, battle and dialogue name tokens. Menu labels other than names stay restored. The change applies immediately | Proposed (scope) | S-SCRIPT, S-FAN-TEXT |
| TXT-017 | "Show shipped names" is offered only when the text language is English, since shipped names exist only there. The scope does not say how this setting works in other languages | Proposed (spec) | S-SCRIPT |
| TXT-018 | Names have no fixed character cap. Every screen that shows a name fits the longest name in each shipped language without truncation or scaling below the minimum text size | Proposed (spec) | S-SCRIPT |

## Presentation

| ID | Requirement | Label | Source |
|---|---|---|---|
| TXT-020 | A dialogue box shows the speaker's name and a live-rendered portrait. No dialogue portrait is an illustrated still | Decided | D-STATIC, D-MENUART |
| TXT-021 | The speaker for each line comes from its portrait code. Every portrait code resolves to a portrait asset; a build check fails on any that does not. This also covers the portrait the Western release lost in one rebuilt town | Proposed (scope) | S-SCRIPT, S-FAN-DEFECTS |
| TXT-022 | Inline line-break and page-pause codes are honored. A page pause waits for the player, or for the auto-advance timer when auto-advance is on | Proposed (spec) | S-SCRIPT |
| TXT-023 | Text speed has four settings: slow, normal, fast, instant. Normal matches the original's reveal speed (TXT-026). Pressing confirm during a reveal completes the page; pressing again advances | Proposed (spec) | S-PILLAR-1 |
| TXT-024 | Auto-advance, off by default: a page advances after 1.0 second plus 0.05 seconds per character, scaled by the delivery note for that line (TXT-025) | Proposed (spec) | S-SCRIPT |
| TXT-025 | Each line can carry delivery data: pauses, timing, and emphasis for panel staging. Delivery data never changes a word | Proposed (scope) | S-SCRIPT |
| TXT-026 | To measure: the original's text reveal speed (characters per second), page pause behavior, and whether any text auto-advances. Method: frame counting on reference build captures over at least 30 pages. Feeds TXT-023 and EVT-061 | To measure | S-ENGINE |

## Text log

| ID | Requirement | Label | Source |
|---|---|---|---|
| TXT-027 | A text log holds the last 500 dialogue pages since the game was loaded, with speaker names. It opens from the field and during events, and opening it pauses the event | Proposed (spec) | S-GOALS |
| TXT-028 | Lines passed over by a skip ([EVT-041](events-and-live-panels.md)) are added to the log, marked as skipped | Proposed (spec) | S-SYS-QOL |
| TXT-029 | The log shows names under the current "Show shipped names" setting. It is not saved | Proposed (spec) | S-SCRIPT |

## Localization

| ID | Requirement | Label | Source |
|---|---|---|---|
| TXT-030 | All player-facing text lives in string tables keyed by stable IDs. No player-facing text is in code, scripts or art | Proposed (spec) | S-SCRIPT |
| TXT-031 | Japanese and English ship at minimum. The full language list is open (S-OPEN 1) | Proposed (scope) | S-SCRIPT, S-OPEN |
| TXT-032 | Each string record holds the shipped English and the Japanese source side by side, so translations can be made from either. Which one they use is open (S-OPEN 2) | Proposed (spec) | S-OPEN |
| TXT-033 | Fonts cover every character of every shipped language, including accented Latin and Japanese | Proposed (spec) | S-SCRIPT |
| TXT-034 | Inline codes are language-neutral tokens. Validation rejects a translation that removes, adds or reorders a portrait code, or breaks a name token | Proposed (spec) | S-SCRIPT |
| TXT-035 | Every screen passes a pseudo-localization run with strings 40% longer than English and with accented characters, with no truncation or overlap | Proposed (spec) | S-SCRIPT |

## Encyclopedia

| ID | Requirement | Label | Source |
|---|---|---|---|
| TXT-040 | An in-game encyclopedia explains terms, places, groups and links to the earlier games in the series that the locked dialogue cannot carry. It includes the name glossary | Proposed (scope) | S-FAN-TEXT |
| TXT-041 | An entry unlocks when its subject first appears in play, set by a flag. An entry never reveals a story beat the player has not reached | Proposed (spec) | S-GOALS |
| TXT-042 | Material known only from developer interviews, such as a villain's cut motivation, goes in the encyclopedia only if the rights holder's own archives confirm it | Proposed (scope) | S-FAN-TEXT |
| TXT-043 | Encyclopedia text is new system text. It goes through the same string tables, glossary checks and localization as other system text | Proposed (spec) | S-SCRIPT |
