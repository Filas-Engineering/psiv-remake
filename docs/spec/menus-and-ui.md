# Menus and UI

Menu structure, portraits and interface art, macro and combo screens, the quest log, settings, and accessibility. Text rules are in [dialogue-and-text.md](dialogue-and-text.md). Save and load screen contents are in [save-and-progression.md](save-and-progression.md).

## Art

| ID | Requirement | Label | Source |
|---|---|---|---|
| UI-001 | Character portraits in menus are rendered live from the character models, with authored expression sets. Dialogue portraits use the same system | Decided | D-MENUART |
| UI-002 | Icons, frames and fonts are ordinary 2D interface art | Decided | D-MENUART |

## Structure

The scope does not describe the menu tree. UI-003 proposes one; UI-004 is the scope's constraint on it.

| ID | Requirement | Label | Source |
|---|---|---|---|
| UI-003 | The field menu root lists: Items, Techniques, Skills, Equipment, Status, Macros, Combo Log, Quest Log, Encyclopedia, Settings, and Save where saving is allowed ([SAV-003](save-and-progression.md)) | Proposed (spec) | S-SYS-QOL |
| UI-004 | Macros are one input from the field menu root and one input from the battle command menu root | Proposed (scope) | S-PILLAR-1 |
| UI-005 | The macro editor creates, renames, deletes and reorders macros; sets each entry's command and target rule ([BAT-054](battle.md)); and marks entries that are currently invalid (absent member, unaffordable cost) with the reason | Proposed (spec) | S-PILLAR-1 |
| UI-006 | The battle screen shows: the command menu for the active party member, a target cursor, each party member's HP, TP and statuses, enemy names, and status messages ([BAT-083](battle.md)). Macro and Auto sit at the top level of the command menu | Proposed (spec) | S-PILLAR-1 |
| UI-007 | Item and equipment screens show every effect of an item or weapon, including battle effects of weapons used as items and any special effect, in plain words | Proposed (scope) | S-FAN-BALANCE |
| UI-008 | Equipping shows stat changes before confirming, and shows which party members can equip the item | Proposed (spec) | S-FAN-BALANCE |
| UI-009 | Shop screens support buying and selling with quantities, show the same effect and comparison information as UI-007 and UI-008, and show how many the party already holds | Proposed (spec) | S-FAN-BALANCE |
| UI-010 | Opening any menu takes at most 100 ms. Menu navigation responds within one frame of input | Proposed (spec) | S-PILLAR-1 |
| UI-011 | The combo log screen shows discovered combinations with their participants and required abilities, and offers "make macro" ([BAT-065](battle.md)). Undiscovered combinations show as locked entries with a total count | Proposed (spec) | S-CONTENT, S-SYS-BATTLE |
| UI-012 | The quest log shows the current main objective and the guild jobs. The main objective is a short line chosen by story flag range. Guild jobs show as available, accepted or completed. No objective line reveals a story beat the player has not reached | Proposed (scope) | S-SYS-QOL, S-CONTENT |
| UI-016 | To measure: the original's menu tree, labels and depth for every field and battle menu. Method: a reference build session that records every screen; compared with UI-003 so nothing the original offers is missing. Feeds UI-003 | To measure | S-GOALS |

## Settings

| ID | Requirement | Label | Source |
|---|---|---|---|
| UI-013 | Settings are the table below. Each setting has a one-line description of what it changes | Proposed (spec) | S-SYS-QOL, S-SYS-DIFF, S-SYS-ACCESS |
| UI-014 | Each difficulty preset and the reward multiplier say plainly, in the menu, what they change, including that Hard is built on enemy behavior and that the multiplier is off by default | Proposed (scope) | S-FAN-BALANCE |

| Setting | Values | Default | Owner |
|---|---|---|---|
| Text speed | Slow, Normal, Fast, Instant | Normal | [TXT-023](dialogue-and-text.md) |
| Auto-advance text | On, Off | Off | [TXT-024](dialogue-and-text.md) |
| Show shipped names | On, Off | Off | [TXT-016](dialogue-and-text.md) |
| Text language | Shipped languages | System language | [TXT-031](dialogue-and-text.md) |
| Text size | 100% to 150% | 100% | UI-019 |
| Battle speed | 1x, 1.5x, 2x, 3x | 1x | [BAT-100](battle.md) |
| Auto-battle macro | Any saved macro | None | [BAT-103](battle.md) |
| Difficulty | Original, Hard | Original | [BAT-120](battle.md) |
| Reward multiplier | 1x, 1.5x, 2x | 1x | [BAT-125](battle.md) |
| Ambient party talk | On, Off | On | [FLD-073](field.md) |
| Reduced flash | On, Off | Off | UI-017 |
| Music, ambient, effects volume | 0 to 100 | 80 | This file |
| Controls | Remap all actions; holds can be toggles | Platform default | UI-018 |

## Accessibility

| ID | Requirement | Label | Source |
|---|---|---|---|
| UI-015 | Nothing in the game flashes the full screen, in any mode and at any setting. This covers the critical-hit white flash of the original | Proposed (scope) | S-FAN-ACCESS, S-SYS-ACCESS |
| UI-017 | Reduced flash setting: lowers the peak brightness change of effect flashes (critical hits, techniques, combinations, lightning and similar) by at least 50% and removes any strobing | Proposed (scope) | S-SYS-ACCESS |
| UI-018 | Every input can be remapped, and every hold action, including skip ([EVT-043](events-and-live-panels.md)), can be set to a toggle or a press-and-confirm | Proposed (spec) | S-GOALS |
| UI-019 | Text size can be raised to 150% in dialogue boxes, menus and the text log without truncation | Proposed (spec) | S-GOALS |
| UI-020 | Status icons differ by shape as well as color | Proposed (spec) | S-GOALS |
| UI-021 | At every setting, no effect flashes more than three times in any one second, and no flash covers more than 25% of the screen. This applies the common photosensitivity guidance behind UI-015 as a testable limit | Proposed (spec) | S-FAN-ACCESS |
