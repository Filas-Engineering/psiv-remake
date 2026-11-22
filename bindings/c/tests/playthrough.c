/*
 * Runs a game_cli command script through the C interface alone and prints the same transcript game_cli
 * prints, so `make test` can compare the two byte for byte. The command language is the one of
 * game/src/runner.cpp; only the calls differ.
 */
#include "psiv_session.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMBATANTS 16
#define NAME_SIZE 128
#define LINE_SIZE 4096
#define MAX_WORDS 16

static psiv_session* S;
static int errors;
static int autoBattle;
/* With autoBattle, only battles the field starts; an event's battle waits for the script (`autobattle random`). */
static int autoEncountersOnly;
/* With autoBattle, the macro those battles play every round (`autobattle on|random MACRO`), "" for none. */
static char autoMacro[NAME_SIZE];
/* With autoBattle, a psiv_macro_trouble in place of the macro's and the rules' (`autobattle on|random [MACRO] MODE`). */
static int32_t autoTrouble = PSIV_MACRO_TROUBLE_DEFAULT;
static int32_t autoPolicy = PSIV_POLICY_DEFAULT;
/* Print a line for each auto round in which macro trouble holds (`trouble on|off`). */
static int printTroubleLines;
static int echo = 1;
static char battleNames[MAX_COMBATANTS][NAME_SIZE];
static int battleNameCount;

static void print(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	putchar('\n');
}

static void error(const char* text) {
	++errors;
	print("! %s", text);
}

static void report(psiv_status status) {
	if (status != PSIV_OK) {
		psiv_error e;
		psiv_last_error(S, &e);
		error(e.text);
	}
}

/* A growing string for lines assembled from pieces. */
typedef struct {
	char text[LINE_SIZE];
	size_t length;
} Buffer;

static void bclear(Buffer* b) {
	b->length = 0;
	b->text[0] = '\0';
}

static void badd(Buffer* b, const char* format, ...) {
	va_list args;
	va_start(args, format);
	const int n = vsnprintf(b->text + b->length, sizeof b->text - b->length, format, args);
	va_end(args);
	if (n > 0) {
		b->length += (size_t)n;
		if (b->length >= sizeof b->text) {
			b->length = sizeof b->text - 1;
		}
	}
}

static int toInt(const char* text, int* out) {
	char* end = NULL;
	if (!text[0]) {
		return 0;
	}
	const long v = strtol(text, &end, 10);
	if (*end != '\0') {
		return 0;
	}
	*out = (int)v;
	return 1;
}

static const char* clockText(long long ms, char* buffer, size_t size) {
	const long long s = ms / 1000;
	snprintf(buffer, size, "%lld:%02lld:%02lld", s / 3600, s / 60 % 60, s % 60);
	return buffer;
}

static int parseFacing(const char* text, int* out) {
	for (int f = PSIV_DIRECTION_NORTH; f <= PSIV_DIRECTION_WEST; ++f) {
		const char* name = psiv_name_of(PSIV_NAME_DIRECTION, f);
		if (strcmp(text, name) == 0 || (strlen(text) == 1 && text[0] == name[0])) {
			*out = f;
			return 1;
		}
	}
	return 0;
}

static const char* modeName(void) {
	return psiv_name_of(PSIV_NAME_MODE, psiv_mode_get(S));
}

static const char* displayName(const char* id) {
	const char* out = id;
	psiv_display_name(S, id, &out);
	return out;
}

static void printNotices(const psiv_notice* notices, int32_t count) {
	for (int32_t i = 0; i < count; ++i) {
		switch (notices[i].kind) {
		case PSIV_NOTICE_ACTION:
			print("  (%s)", notices[i].text);
			break;
		case PSIV_NOTICE_WARNING:
			print("! %s", notices[i].text);
			break;
		case PSIV_NOTICE_AUTOSAVE:
			print("[%s]", notices[i].text);
			break;
		case PSIV_NOTICE_AMBIENT:
			print("~ %s", notices[i].text);
			break;
		default:
			print("%s", notices[i].text);
			break;
		}
	}
}

static void flushOutput(void) {
	const psiv_notice* notices;
	int32_t count;
	if (psiv_notices_drain(S, &notices, &count) == PSIV_OK) {
		printNotices(notices, count);
	}
}

static const char* battleName(int index) {
	return index >= 0 && index < battleNameCount ? battleNames[index] : "?";
}

static void printEvents(const psiv_battle_event* events, int32_t count) {
	for (int32_t i = 0; i < count; ++i) {
		const psiv_battle_event* e = &events[i];
		switch (e->type) {
		case PSIV_EVENT_ROUND_STARTED:
			print("-- round %d --", e->round);
			break;
		case PSIV_EVENT_RETARGETED:
			print("%s turns from %s to %s.", battleName(e->actor), battleName(e->previous_target), battleName(e->target));
			break;
		case PSIV_EVENT_ACTION_STARTED:
			if (e->command == PSIV_COMMAND_DEFEND) {
				print("%s defends.", battleName(e->actor));
			} else if (e->command == PSIV_COMMAND_ESCAPE) {
				print("%s tries to escape.", battleName(e->actor));
			} else if (e->target >= 0) {
				print("%s uses %s on %s.", battleName(e->actor), displayName(e->ability), battleName(e->target));
			} else {
				print("%s uses %s.", battleName(e->actor), displayName(e->ability));
			}
			break;
		case PSIV_EVENT_ACTION_CANCELLED:
			print("%s", e->text);
			break;
		case PSIV_EVENT_COMBO_TRIGGERED:
			print("Combo: %s.", displayName(e->ability));
			break;
		case PSIV_EVENT_COMBO_DISCOVERED:
			print("  A new combination joins the combo log.");
			break;
		case PSIV_EVENT_ITEM_HAD_NO_EFFECT:
			print("  %s has no effect and is kept.", displayName(e->ability));
			break;
		case PSIV_EVENT_MISSED:
			print("  %s evades.", battleName(e->target));
			break;
		case PSIV_EVENT_CRITICAL:
			print("  Critical hit!");
			break;
		case PSIV_EVENT_DAMAGE_DEALT:
			print("  %s takes %d damage.", battleName(e->target), e->amount);
			break;
		case PSIV_EVENT_HEALED:
			print("  %s recovers %d.", battleName(e->target), e->amount);
			break;
		case PSIV_EVENT_REVIVED:
			print("  %s is revived.", battleName(e->target));
			break;
		case PSIV_EVENT_STATUS_APPLIED:
		case PSIV_EVENT_STATUS_RESISTED:
		case PSIV_EVENT_STATUS_REMOVED:
			print("  %s", e->text);
			break;
		case PSIV_EVENT_STAT_MODIFIED:
			print("  %s's %s %s", battleName(e->target), psiv_name_of(PSIV_NAME_STAT, e->stat), e->amount > 0 ? "rises." : "falls.");
			break;
		case PSIV_EVENT_COMBATANT_DEFEATED:
			print("  %s falls.", battleName(e->target));
			break;
		case PSIV_EVENT_ESCAPE_ATTEMPTED:
			print(e->succeeded ? "  The party escapes!" : "  No escape!");
			break;
		case PSIV_EVENT_MESSAGE:
			print("%s", e->text);
			break;
		case PSIV_EVENT_BATTLE_ENDED:
			print("Outcome: %s", psiv_name_of(PSIV_NAME_OUTCOME, e->outcome));
			break;
		case PSIV_EVENT_ENEMY_JOINED:
			if (e->target == battleNameCount && battleNameCount < MAX_COMBATANTS) {
				const psiv_combatant* list;
				int32_t count;
				if (psiv_battle_combatants(S, &list, &count) == PSIV_OK && e->target < count) {
					snprintf(battleNames[battleNameCount++], NAME_SIZE, "%s", list[e->target].label);
				}
			}
			print("  %s joins the battle.", battleName(e->target));
			break;
		case PSIV_EVENT_ENEMY_LEFT:
			if (e->actor >= 0) {
				print("  %s merges into %s.", battleName(e->target), battleName(e->actor));
			}
			break;
		case PSIV_EVENT_ENEMY_TRANSFORMED:
			print("  %s becomes %s.", battleName(e->target), displayName(e->id_count > 0 ? e->ids[0] : ""));
			break;
		default:
			break;
		}
	}
}

static void resolveAndPrint(void) {
	const psiv_battle_event* events;
	int32_t count;
	if (psiv_battle_resolve_round(S, &events, &count) == PSIV_OK) {
		printEvents(events, count);
	}
}

static void names(Buffer* text, const int32_t* indexes, int32_t count) {
	for (int32_t i = 0; i < count; ++i) {
		badd(text, "%s%s", i ? ", " : "", indexes[i] >= 0 && indexes[i] < battleNameCount ? battleNames[indexes[i]] : "?");
	}
	if (count == 0) {
		badd(text, "-");
	}
}

static void printTrouble(const psiv_macro_trouble_report* trouble) {
	if (!printTroubleLines || !trouble->holds) {
		return;
	}
	Buffer line;
	bclear(&line);
	badd(&line, "Trouble (%s): steps ", psiv_name_of(PSIV_NAME_MACRO_TROUBLE, trouble->mode));
	for (int32_t i = 0; i < trouble->troubled_step_count; ++i) {
		badd(&line, "%s%d", i ? " " : "", trouble->troubled_steps[i]);
	}
	badd(&line, "%s (", trouble->troubled_step_count ? "" : "-");
	names(&line, trouble->troubled_members, trouble->troubled_member_count);
	badd(&line, "); fallen ");
	names(&line, trouble->fallen, trouble->fallen_count);
	badd(&line, "; core's choice for ");
	names(&line, trouble->recovered, trouble->recovered_count);
	badd(&line, ".");
	print("%s", line.text);
}

static void finishBattleAuto(const char* macro, int32_t trouble, int32_t policy) {
	psiv_auto_battle played;
	report(psiv_battle_set_auto_play_macro_policy(S, macro, trouble, policy));
	const psiv_status status = psiv_battle_play(S, 60, &played);
	if (status != PSIV_OK) {
		psiv_battle_set_auto_play_macro(S, "");
		return;
	}
	if (played.refusal_key[0]) {
		error(played.refusal_text);
	}
	for (int32_t i = 0; i < played.round_count; ++i) {
		printNotices(played.rounds[i].notices, played.rounds[i].notice_count);
		printTrouble(&played.rounds[i].trouble);
		printEvents(played.rounds[i].events, played.rounds[i].event_count);
	}
	/* Clearing the macro frees what psiv_battle_play returned, so it waits until the rounds are printed. */
	psiv_battle_set_auto_play_macro(S, "");
	if (!played.ended) {
		error("the battle did not end in 60 rounds");
	}
	flushOutput();
}

static void settle(void) {
	for (int guard = 0; guard < 100000; ++guard) {
		flushOutput();
		const int32_t mode = psiv_mode_get(S);
		if (mode == PSIV_MODE_BATTLE) {
			if (battleNameCount == 0) {
				const psiv_combatant* list;
				int32_t count;
				Buffer enemies;
				bclear(&enemies);
				psiv_battle_combatants(S, &list, &count);
				for (int32_t i = 0; i < count && i < MAX_COMBATANTS; ++i) {
					snprintf(battleNames[i], NAME_SIZE, "%s", list[i].label);
					if (list[i].side == PSIV_SIDE_ENEMY) {
						badd(&enemies, "%s%s", enemies.length ? ", " : "", list[i].label);
					}
				}
				battleNameCount = count;
				print("Enemies: %s.", enemies.text);
			}
			psiv_battle_info info;
			if (!autoBattle || (autoEncountersOnly && psiv_battle_info_get(S, &info) == PSIV_OK && info.source == PSIV_BATTLE_EVENT)) {
				return;
			}
			finishBattleAuto(autoMacro, autoTrouble, autoPolicy);
			continue;
		}
		battleNameCount = 0;
		if (mode != PSIV_MODE_EVENT) {
			return;
		}
		if (psiv_line_pending(S)) {
			psiv_line line;
			psiv_line_get(S, &line);
			print("%s", line.display);
			psiv_line_confirm(S);
			continue;
		}
		if (psiv_choice_pending(S)) {
			psiv_choice choice;
			Buffer text;
			bclear(&text);
			psiv_choice_get(S, &choice);
			badd(&text, "%s", choice.prompt[0] ? choice.prompt : "Choose:");
			for (int32_t i = 0; i < choice.option_count; ++i) {
				badd(&text, " [%d] %s", i, choice.options[i]);
			}
			print("%s", text.text);
			return;
		}
		psiv_update(S, 100, PSIV_DIRECTION_NONE);
	}
	error("the game did not settle");
}

/* Plays the battle round by round with the macro; a step that cannot run follows `fallback`. */
static void fight(const char* macro, int32_t fallback) {
	for (int round = 0; round < 60 && psiv_mode_get(S) == PSIV_MODE_BATTLE; ++round) {
		flushOutput();
		if (psiv_battle_apply_macro_fallback(S, macro, fallback, NULL, NULL) != PSIV_OK) {
			report(PSIV_REFUSED);
			return;
		}
		resolveAndPrint();
	}
	if (psiv_mode_get(S) == PSIV_MODE_BATTLE) {
		error("the battle did not end in 60 rounds");
	}
}

static int hasMacro(const char* name) {
	const psiv_macro* macros;
	int32_t count;
	if (psiv_macro_list(S, &macros, &count) != PSIV_OK) {
		return 0;
	}
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(macros[i].name, name) == 0) {
			return 1;
		}
	}
	return 0;
}

/* `party`: the lineup in order, then the reserve in roster order. */
static void partyLists(void) {
	const psiv_member* party;
	int32_t count;
	Buffer lineup;
	Buffer reserve;
	bclear(&lineup);
	bclear(&reserve);
	psiv_party(S, &party, &count);
	for (int32_t i = 0; i < count; ++i) {
		Buffer* list = party[i].in_lineup ? &lineup : &reserve;
		badd(list, "%s%s", list->length ? ", " : "", party[i].name);
	}
	print("  Lineup: %s. Reserve: %s.", lineup.text, reserve.length ? reserve.text : "nobody");
}

static void listMacros(void) {
	const psiv_macro* macros;
	psiv_settings settings;
	int32_t count;
	psiv_settings_get(S, &settings);
	if (psiv_macro_list(S, &macros, &count) != PSIV_OK) {
		return;
	}
	for (int32_t i = 0; i < count; ++i) {
		Buffer options;
		bclear(&options);
		if (macros[i].on_trouble != PSIV_MACRO_TROUBLE_DEFAULT) {
			badd(&options, "on_trouble %s", psiv_name_of(PSIV_NAME_MACRO_TROUBLE, macros[i].on_trouble));
		}
		if (macros[i].recover_who != PSIV_RECOVER_DEFAULT) {
			badd(&options, "%srecover_who %s", options.length ? ", " : "", psiv_name_of(PSIV_NAME_MACRO_RECOVER_WHO, macros[i].recover_who));
		}
		if (macros[i].unnamed != PSIV_UNNAMED_DEFAULT) {
			badd(&options, "%sunnamed %s", options.length ? ", " : "", psiv_name_of(PSIV_NAME_MACRO_UNNAMED, macros[i].unnamed));
		}
		if (macros[i].policy != PSIV_POLICY_DEFAULT) {
			badd(&options, "%spolicy %s", options.length ? ", " : "", psiv_name_of(PSIV_NAME_MACRO_POLICY, macros[i].policy));
		}
		print("  %s%s%s%s%s", macros[i].name, strcmp(macros[i].name, settings.auto_macro) == 0 ? " (auto)" : "", options.length ? " [" : "", options.text,
			options.length ? "]" : "");
		for (int32_t j = 0; j < macros[i].step_count; ++j) {
			const psiv_macro_step* step = &macros[i].steps[j];
			Buffer line;
			bclear(&line);
			badd(&line, "    %d %s %s", j, step->member, step->piece ? "piece" : psiv_name_of(PSIV_NAME_COMMAND_KIND, step->command_kind));
			if (step->id[0]) {
				badd(&line, " %s", step->id);
			}
			if (step->target_kind == PSIV_MACRO_TARGET_SELF || step->target_kind == PSIV_MACRO_TARGET_ALL) {
				badd(&line, " %s", psiv_name_of(PSIV_NAME_MACRO_TARGET_KIND, step->target_kind));
			} else if (step->target_kind == PSIV_MACRO_TARGET_MEMBER) {
				badd(&line, " %s", step->target_member);
			} else if (step->target_kind == PSIV_MACRO_TARGET_ENEMY) {
				badd(&line, " e%d", step->target_enemy);
			}
			if (step->issue != PSIV_ISSUE_NONE) {
				badd(&line, " (%s)", psiv_name_of(PSIV_NAME_MACRO_ISSUE, step->issue));
			}
			print("%s", line.text);
		}
	}
}

static void checkMacro(const char* name) {
	const psiv_macro_step_check* checks;
	int32_t count = 0;
	const psiv_status status = psiv_battle_macro_check(S, name, &checks, &count);
	if (status != PSIV_OK) {
		report(status);
		return;
	}
	for (int32_t i = 0; i < count; ++i) {
		Buffer line;
		bclear(&line);
		badd(&line, "    %d %s: %s", i, checks[i].member, checks[i].ok ? "ok" : psiv_name_of(PSIV_NAME_MACRO_ISSUE, checks[i].issue));
		if (checks[i].status[0]) {
			badd(&line, " (%s)", checks[i].status);
		}
		if (checks[i].refusal_key[0]) {
			badd(&line, ", refused: %s", checks[i].refusal_text);
		}
		print("%s", line.text);
	}
	psiv_macro_trouble_report trouble;
	psiv_battle_macro_trouble(S, name, &trouble);
	print("    trouble: %s", trouble.holds ? "yes" : "no");
}

/* The macro editor's commands: create, rename, delete, move, step, removestep, movestep and combo. */
static void macro(char words[][LINE_SIZE], int wordCount) {
	int a = 0;
	int b = 0;
	if (strcmp(words[1], "create") == 0 && wordCount == 3) {
		report(psiv_macro_create(S, words[2]));
	} else if (strcmp(words[1], "rename") == 0 && wordCount == 4) {
		report(psiv_macro_rename(S, words[2], words[3]));
	} else if (strcmp(words[1], "delete") == 0 && wordCount == 3) {
		report(psiv_macro_delete(S, words[2]));
	} else if (strcmp(words[1], "move") == 0 && wordCount == 4 && toInt(words[3], &a)) {
		report(psiv_macro_move(S, words[2], a));
	} else if (strcmp(words[1], "removestep") == 0 && wordCount == 4 && toInt(words[3], &a)) {
		report(psiv_macro_remove_step(S, words[2], a));
	} else if (strcmp(words[1], "movestep") == 0 && wordCount == 5 && toInt(words[3], &a) && toInt(words[4], &b)) {
		report(psiv_macro_move_step(S, words[2], a, b));
	} else if (strcmp(words[1], "combo") == 0 && wordCount == 4) {
		report(psiv_macro_from_combo(S, words[2], words[3]));
	} else if (strcmp(words[1], "check") == 0 && wordCount == 3) {
		checkMacro(words[2]);
	} else if (strcmp(words[1], "option") == 0 && wordCount == 5 && strcmp(words[3], "policy") == 0) {
		int32_t policy = PSIV_POLICY_DEFAULT;
		if (psiv_parse_name(PSIV_NAME_MACRO_POLICY, words[4], &policy) != PSIV_OK) {
			error("macro option NAME policy power|expected|default");
			return;
		}
		report(psiv_macro_set_policy(S, words[2], policy));
	} else if (strcmp(words[1], "option") == 0 && wordCount == 5) {
		const psiv_macro* macros;
		int32_t count = 0;
		int32_t values[3] = {PSIV_MACRO_TROUBLE_DEFAULT, PSIV_RECOVER_DEFAULT, PSIV_UNNAMED_DEFAULT};
		psiv_macro_list(S, &macros, &count);
		for (int32_t i = 0; i < count; ++i) {
			if (strcmp(macros[i].name, words[2]) == 0) {
				values[0] = macros[i].on_trouble;
				values[1] = macros[i].recover_who;
				values[2] = macros[i].unnamed;
			}
		}
		const int key = strcmp(words[3], "on_trouble") == 0 ? 0 : strcmp(words[3], "recover_who") == 0 ? 1 : strcmp(words[3], "unnamed") == 0 ? 2 : -1;
		const int32_t kinds[3] = {PSIV_NAME_MACRO_TROUBLE, PSIV_NAME_MACRO_RECOVER_WHO, PSIV_NAME_MACRO_UNNAMED};
		if (key < 0 || psiv_parse_name(kinds[key], words[4], &values[key]) != PSIV_OK) {
			error("macro option NAME on_trouble|recover_who|unnamed VALUE");
			return;
		}
		report(psiv_macro_set_options(S, words[2], values[0], values[1], values[2]));
	} else if (wordCount == 3 && psiv_mode_get(S) == PSIV_MODE_BATTLE) {
		int32_t fallback = PSIV_MACRO_FALLBACK_BASIC_ATTACK;
		if (psiv_parse_name(PSIV_NAME_MACRO_FALLBACK, words[2], &fallback) != PSIV_OK || fallback == PSIV_MACRO_FALLBACK_BASIC_ATTACK) {
			error("macro NAME [automatic|attack|lose_turn]");
			return;
		}
		report(psiv_battle_apply_macro_fallback(S, words[1], fallback, NULL, NULL));
	} else if (strcmp(words[1], "step") == 0 && wordCount >= 6 && wordCount <= 8 && toInt(words[3], &a)) {
		const int piece = strcmp(words[5], "piece") == 0;
		int32_t kind = PSIV_COMMAND_ITEM;
		char message[LINE_SIZE + 64];
		if (!piece && psiv_parse_name(PSIV_NAME_COMMAND_KIND, words[5], &kind) != PSIV_OK) {
			snprintf(message, sizeof message, "unknown command '%s'", words[5]);
			error(message);
			return;
		}
		const int takesId = kind == PSIV_COMMAND_ABILITY || kind == PSIV_COMMAND_ITEM;
		int next = 6;
		const char* id = "";
		if (takesId && wordCount > next) {
			id = words[next++];
		}
		if (wordCount > next + 1) {
			error("macro step NAME INDEX MEMBER COMMAND [ID] [TARGET]");
			return;
		}
		const char* target = wordCount > next ? words[next] : "default";
		int32_t targetKind = PSIV_MACRO_TARGET_DEFAULT;
		const char* member = "";
		int slot = -1;
		if (strlen(target) > 1 && target[0] == 'e' && toInt(target + 1, &slot)) {
			targetKind = PSIV_MACRO_TARGET_ENEMY;
		} else if (psiv_parse_name(PSIV_NAME_MACRO_TARGET_KIND, target, &targetKind) != PSIV_OK || targetKind == PSIV_MACRO_TARGET_MEMBER ||
			targetKind == PSIV_MACRO_TARGET_ENEMY) {
			targetKind = PSIV_MACRO_TARGET_MEMBER;
			member = target;
			slot = -1;
		}
		report(piece ? psiv_macro_set_piece_step(S, words[2], a, words[4], id, targetKind, member, slot)
					 : psiv_macro_set_step(S, words[2], a, words[4], kind, id, targetKind, member, slot));
	} else {
		error("unknown macro command");
	}
}

/* The developer commands a script uses to reach a part of the adventure directly; each marks the game. */
static void dev(char words[][LINE_SIZE], int wordCount) {
	int n = 0;
	if (strcmp(words[1], "warp") == 0 && wordCount == 4) {
		report(psiv_dev_warp(S, words[2], words[3]));
	} else if (strcmp(words[1], "flag") == 0 && wordCount == 4 && (strcmp(words[3], "on") == 0 || strcmp(words[3], "off") == 0)) {
		report(psiv_dev_set_flag(S, words[2], strcmp(words[3], "on") == 0));
	} else if (strcmp(words[1], "give") == 0 && wordCount == 4 && toInt(words[3], &n)) {
		report(psiv_dev_give(S, words[2], n));
	} else if (strcmp(words[1], "money") == 0 && wordCount == 3 && toInt(words[2], &n)) {
		report(psiv_dev_money(S, n));
	} else if (strcmp(words[1], "rest") == 0 && wordCount == 2) {
		report(psiv_dev_rest(S));
	} else if (strcmp(words[1], "level") == 0 && wordCount == 4 && toInt(words[3], &n)) {
		report(psiv_dev_set_level(S, words[2], n));
	} else if (strcmp(words[1], "vehicle") == 0 && wordCount == 5) {
		report(psiv_dev_vehicle(S, words[2], words[3], words[4]));
	} else {
		error("unknown dev command");
	}
}

static void status(void) {
	psiv_field_info field;
	psiv_world_info world;
	const psiv_member* party;
	const psiv_inventory_entry* items;
	int32_t partyCount;
	int32_t itemCount;
	char clock[32];
	const int hasArea = psiv_field_info_get(S, &field) == PSIV_OK;
	psiv_world_info_get(S, &world);
	print("Mode %s, %s (%d,%d) facing %s, play time %s.", modeName(), hasArea ? field.area_name : "-", hasArea ? field.leader_x : 0, hasArea ? field.leader_y : 0,
		psiv_name_of(PSIV_NAME_DIRECTION, hasArea ? field.facing : PSIV_DIRECTION_SOUTH), clockText(world.play_time_ms, clock, sizeof clock));
	psiv_party(S, &party, &partyCount);
	for (int32_t i = 0; i < partyCount; ++i) {
		const psiv_member* m = &party[i];
		Buffer line;
		if (!m->in_lineup) {
			continue;
		}
		bclear(&line);
		badd(&line, "  %s Lv%d HP %d/%d TP %d/%d EXP %d", m->name, m->level, m->hp, m->max_hp, m->tp, m->max_tp, m->experience);
		for (int32_t j = 0; j < m->status_count; ++j) {
			if (m->statuses[j].remaining > 0) {
				badd(&line, " [%s %d]", m->statuses[j].name, m->statuses[j].remaining);
			} else {
				badd(&line, " [%s]", m->statuses[j].name);
			}
		}
		print("%s", line.text);
	}
	Buffer list;
	bclear(&list);
	psiv_inventory(S, &items, &itemCount);
	for (int32_t i = 0; i < itemCount; ++i) {
		badd(&list, "%s%s x%d", list.length ? ", " : "", items[i].id, items[i].count);
	}
	print("  Money %d. Items: %s.", world.money, list.length ? list.text : "none");
	const psiv_vehicle* vehicles;
	int32_t vehicleCount = 0;
	if (hasArea && field.aboard[0] && psiv_field_vehicles(S, &vehicles, &vehicleCount) == PSIV_OK) {
		for (int32_t i = 0; i < vehicleCount; ++i) {
			if (vehicles[i].aboard) {
				print("  Aboard the %s.", vehicles[i].name);
			}
		}
	}
}

static void look(void) {
	psiv_field_info field;
	const char* const* rows;
	int32_t rowCount;
	const psiv_exit* exits;
	const psiv_point* points;
	const psiv_enemy* enemies;
	int32_t exitCount;
	int32_t pointCount;
	int32_t enemyCount;
	if (psiv_field_info_get(S, &field) != PSIV_OK) {
		error("there is no area");
		return;
	}
	psiv_field_rows(S, &rows, &rowCount);
	psiv_field_exits(S, &exits, &exitCount);
	psiv_field_points(S, &points, &pointCount);
	psiv_field_enemies(S, &enemies, &enemyCount);
	char* map = calloc((size_t)(field.width + 1) * (size_t)rowCount, 1);
	for (int32_t y = 0; y < rowCount; ++y) {
		memcpy(map + y * (field.width + 1), rows[y], strlen(rows[y]));
	}
#define PUT(cx, cy, ch) \
	do { \
		if ((cx) >= 0 && (cy) >= 0 && (cx) < field.width && (cy) < rowCount) { \
			map[(cy) * (field.width + 1) + (cx)] = (ch); \
		} \
	} while (0)
	for (int32_t i = 0; i < exitCount; ++i) {
		PUT(exits[i].x, exits[i].y, '>');
	}
	/* The terminal draws talk hooks, then chests, shops and rest points: the order of psiv_field_points. */
	for (int32_t i = 0; i < pointCount; ++i) {
		switch (points[i].kind) {
		case PSIV_POINT_TALK:
			PUT(points[i].x, points[i].y, 'N');
			break;
		case PSIV_POINT_CHEST:
			PUT(points[i].x, points[i].y, points[i].active ? 'C' : 'c');
			break;
		case PSIV_POINT_SHOP:
			PUT(points[i].x, points[i].y, '$');
			break;
		case PSIV_POINT_REST:
			PUT(points[i].x, points[i].y, 'R');
			break;
		case PSIV_POINT_PORT:
			PUT(points[i].x, points[i].y, 'P');
			break;
		default:
			break;
		}
	}
	const psiv_vehicle* vehicles;
	int32_t vehicleCount = 0;
	psiv_field_vehicles(S, &vehicles, &vehicleCount);
	for (int32_t i = 0; i < vehicleCount; ++i) {
		PUT(vehicles[i].x, vehicles[i].y, 'V');
	}
	for (int32_t i = 0; i < enemyCount; ++i) {
		PUT(enemies[i].x, enemies[i].y, enemies[i].state == PSIV_ENEMY_CHASING ? '!' : 'E');
	}
	PUT(field.leader_x, field.leader_y, '@');
#undef PUT
	for (int32_t y = 0; y < rowCount; ++y) {
		print("  %s", map + y * (field.width + 1));
	}
	free(map);
}

static void rows(void) {
	const char* const* list;
	int32_t count = 0;
	if (psiv_field_rows(S, &list, &count) != PSIV_OK) {
		error("there is no area");
		return;
	}
	for (int32_t i = 0; i < count; ++i) {
		print("  %s", list[i]);
	}
}

static void prompt(void) {
	psiv_interact_target t;
	if (psiv_interact_target_get(S, &t) != PSIV_OK) {
		print("  prompt: nothing");
		return;
	}
	print("  prompt: %s %s at %d %d facing %s%s%s%s%s", psiv_name_of(PSIV_NAME_INTERACT_KIND, t.kind), t.id, t.x, t.y, psiv_name_of(PSIV_NAME_DIRECTION, t.facing),
		t.across ? " across" : "", t.keeper[0] ? " kept by " : "", t.keeper, t.ready ? "" : " (refused)");
}

static void background(void) {
	const psiv_event_position* list;
	int32_t count = 0;
	psiv_background_events(S, &list, &count);
	if (count == 0) {
		print("  background: none");
	}
	for (int32_t i = 0; i < count; ++i) {
		print("  background: %s at %s:%d", list[i].event, list[i].file, list[i].line);
	}
}

/* `credits`: the credits playing, how far play time has come and each entry, + once shown. */
static void credits(void) {
	psiv_credits_info info;
	const psiv_credits_entry* entries;
	int32_t count = 0;
	if (psiv_credits_info_get(S, &info) != PSIV_OK || psiv_credits_entries(S, &entries, &count) != PSIV_OK) {
		error("no credits are playing");
		return;
	}
	print("Credits %s%s%s, %d of %d shown, %d of %d ms.", info.id, info.music[0] ? ", music " : "", info.music, info.shown, info.entry_count, (int)info.elapsed_ms,
		(int)info.total_ms);
	for (int32_t i = 0; i < count; ++i) {
		char gap[32] = "";
		if (entries[i].gap > 0) {
			snprintf(gap, sizeof gap, ", gap %d", entries[i].gap);
		}
		print("  %s %s %s (%d ms%s)", i < info.shown ? "+" : "-", entries[i].kind == PSIV_CREDITS_HEADING ? "heading" : "line", entries[i].text, entries[i].pause_ms, gap);
	}
}

static void steps(void) {
	char text[LINE_SIZE] = "";
	for (int32_t d = PSIV_DIRECTION_NORTH; d <= PSIV_DIRECTION_WEST; ++d) {
		char part[64];
		snprintf(part, sizeof part, "%s%s %s", d == PSIV_DIRECTION_NORTH ? "" : ", ", psiv_name_of(PSIV_NAME_DIRECTION, d), psiv_can_step(S, d) ? "yes" : "no");
		strncat(text, part, sizeof text - strlen(text) - 1);
	}
	print("  steps: %s", text);
}

static void stepTime(void) {
	psiv_world_info world;
	psiv_field_info field;
	psiv_world_info_get(S, &world);
	if (psiv_field_info_get(S, &field) != PSIV_OK) {
		field.leader_step_ms = 0;
		field.run_step_ms = 0;
	}
	print("  play time %lld ms; a step takes %d ms, %d ms with the run input", (long long)world.play_time_ms, field.leader_step_ms, field.run_step_ms);
}

/* `pictures`: the story pictures up, bottom first. */
/* `platforms`: each moving platform of the area, where it is and whether it carries the party. */
static void platforms(void) {
	const psiv_platform* list = NULL;
	int32_t count = 0;
	psiv_platforms(S, &list, &count);
	if (count == 0) {
		print("  platforms: none");
	}
	for (int32_t i = 0; i < count; ++i) {
		const psiv_platform* p = &list[i];
		char rest[64] = "";
		if (p->end >= 0 && p->pause_ms > 0) {
			snprintf(rest, sizeof rest, ", resting %d ms", (int)p->pause_ms);
		}
		print("  platform %s at %d %d (%d, %d px) %s%s%s", p->id, (int)p->x, (int)p->y, (int)p->pixel_x, (int)p->pixel_y,
			p->end == 0 ? "docked at from" : p->end == 1 ? "docked at to" : "moving", rest, p->carrying ? ", carrying the party" : "");
	}
}

static void pictures(void) {
	const char* const* list = NULL;
	int32_t count = 0;
	char text[1024] = "";
	psiv_pictures(S, &list, &count);
	for (int32_t i = 0; i < count; ++i) {
		snprintf(text + strlen(text), sizeof text - strlen(text), "%s%s", i ? ", " : "", list[i]);
	}
	print("  pictures: %s", count ? text : "none");
}

/* `camera`: the scene camera, held on a cell or on the leader, with the move's time left. */
static void sceneCamera(void) {
	psiv_scene_camera c;
	char moving[64] = "";
	psiv_scene_camera_get(S, &c);
	if (c.ms > 0) {
		snprintf(moving, sizeof moving, ", %d/%d ms", c.ms, c.total_ms);
	}
	if (c.held) {
		print("  camera: at %d %d%s", c.x, c.y, moving);
	} else if (c.ms > 0) {
		print("  camera: leader (returning%s)", moving);
	} else {
		print("  camera: leader");
	}
}

/* `motion`: when the next tick and the step's landing come, and the last exit's sound. */
static void motion(void) {
	psiv_field_info field;
	if (psiv_field_info_get(S, &field) != PSIV_OK) {
		error("there is no area");
		return;
	}
	print("  motion: next tick in %lld us, a step lands in %lld us, %lld us running; last exit sound %s", (long long)field.next_tick_us,
		(long long)field.step_lands_in_us, (long long)field.run_step_lands_in_us, field.exit_sound[0] ? field.exit_sound : "none");
}

static void actors(void) {
	const psiv_actor* list;
	int32_t count = 0;
	if (psiv_field_actors(S, &list, &count) != PSIV_OK) {
		error("there is no area");
		return;
	}
	if (count == 0) {
		print("  (no actors)");
	}
	for (int32_t i = 0; i < count; ++i) {
		const psiv_actor* a = &list[i];
		char text[LINE_SIZE];
		int n = snprintf(text, sizeof text, "  %s at %d %d facing %s %s", a->id, a->x, a->y, psiv_name_of(PSIV_NAME_DIRECTION, a->facing),
			a->visible ? "shown" : "hidden");
		if (a->solid) {
			n += snprintf(text + n, sizeof text - (size_t)n, " solid");
		}
		if (a->vanish != PSIV_VANISH_NONE) {
			n += snprintf(text + n, sizeof text - (size_t)n, " %s", psiv_name_of(PSIV_NAME_ACTOR_VANISH, a->vanish));
		}
		if (a->removal != PSIV_REMOVAL_NONE) {
			n += snprintf(text + n, sizeof text - (size_t)n, " removed %s", psiv_name_of(PSIV_NAME_ACTOR_REMOVAL, a->removal));
		}
		if (a->animation[0]) {
			n += snprintf(text + n, sizeof text - (size_t)n, " anim %s %d/%dms", a->animation, a->animation_ms, a->animation_total_ms);
		}
		if (a->sprite_enemy[0]) {
			n += snprintf(text + n, sizeof text - (size_t)n, " enemy %s", a->sprite_enemy);
		}
		if (a->look[0]) {
			n += snprintf(text + n, sizeof text - (size_t)n, " look %s", a->look);
		}
		if (a->wanders) {
			n += snprintf(text + n, sizeof text - (size_t)n, " wanders");
		}
		if (a->patrols) {
			n += snprintf(text + n, sizeof text - (size_t)n, " patrols");
		}
		if (a->on_path) {
			snprintf(text + n, sizeof text - (size_t)n, " path %d %d from %d %d", (int)a->path_x, (int)a->path_y, (int)a->home_x, (int)a->home_y);
		}
		print("%s", text);
	}
}

static void walkTo(int goalX, int goalY) {
	int32_t status = PSIV_WALK_WALKING;
	if (psiv_walk_to(S, goalX, goalY) != PSIV_OK) {
		return;
	}
	while (psiv_walk_step(S, &status) == PSIV_OK && status == PSIV_WALK_WALKING) {
		settle();
	}
	if (status == PSIV_WALK_NO_PATH) {
		char text[64];
		snprintf(text, sizeof text, "no path to (%d,%d)", goalX, goalY);
		error(text);
	} else if (status == PSIV_WALK_TOO_LONG) {
		error("walking took too long");
	}
}

/* game/src/runner.cpp's Runner::approach, through the C API: walk up to an actor where it stands now, choosing again after
 * every step, then face it and, for talk, interact. */
static void approach(const char* actor, int talk) {
	static const int dx[4] = {0, 1, 0, -1};
	static const int dy[4] = {-1, 0, 1, 0};
	psiv_field_info start;
	if (psiv_field_info_get(S, &start) != PSIV_OK) {
		return;
	}
	char area[128];
	snprintf(area, sizeof area, "%s", start.area_id);
	char text[192];
	for (int step = 0; step < 400; ++step) {
		if (psiv_mode_get(S) != PSIV_MODE_FIELD) {
			return;
		}
		psiv_field_info field;
		psiv_field_info_get(S, &field);
		if (strcmp(field.area_id, area) != 0) {
			snprintf(text, sizeof text, "left the area on the way to %s", actor);
			error(text);
			return;
		}
		const psiv_actor* actors;
		int32_t actorCount = 0;
		psiv_field_actors(S, &actors, &actorCount);
		int found = 0;
		int targetX[16];
		int targetY[16];
		int targets = 0;
		for (int32_t i = 0; i < actorCount; ++i) {
			if (strcmp(actors[i].id, actor) == 0 && actors[i].visible && actors[i].removal == PSIV_REMOVAL_NONE) {
				targetX[0] = actors[i].x;
				targetY[0] = actors[i].y;
				found = 1;
			}
		}
		if (!found || targetX[0] < 0 || targetY[0] < 0 || targetX[0] >= field.width || targetY[0] >= field.height) {
			snprintf(text, sizeof text, "%s is not here", actor);
			error(text);
			return;
		}
		targets = 1;
		const psiv_point* points;
		int32_t pointCount = 0;
		psiv_field_points(S, &points, &pointCount);
		for (int32_t i = 0; i < pointCount && targets < 16; ++i) {
			if (points[i].kind == PSIV_POINT_TALK && strcmp(points[i].id, actor) == 0 && (points[i].x != targetX[0] || points[i].y != targetY[0])) {
				targetX[targets] = points[i].x;
				targetY[targets] = points[i].y;
				++targets;
			}
		}
		int facing = PSIV_DIRECTION_NONE;
		for (int t = 0; t < targets; ++t) {
			psiv_interact_target target;
			if (facing == PSIV_DIRECTION_NONE && psiv_interact_at(S, targetX[t], targetY[t], &target) == PSIV_OK && strcmp(target.id, actor) == 0) {
				facing = target.facing;
			}
		}
		for (int d = 0; d < 4; ++d) {
			if (facing == PSIV_DIRECTION_NONE && field.leader_x + dx[d] == targetX[0] && field.leader_y + dy[d] == targetY[0]) {
				facing = d;
			}
		}
		if (facing != PSIV_DIRECTION_NONE) {
			report(psiv_face(S, facing));
			if (talk) {
				report(psiv_interact(S));
			}
			return;
		}
		const char* const* rows;
		int32_t rowCount = 0;
		psiv_field_rows(S, &rows, &rowCount);
		int candidateX[128];
		int candidateY[128];
		int candidates = 0;
		for (int t = 0; t < targets; ++t) {
			for (int d = 0; d < 4; ++d) {
				const int x = targetX[t] + dx[d];
				const int y = targetY[t] + dy[d];
				candidateX[candidates] = x;
				candidateY[candidates] = y;
				++candidates;
				if (x >= 0 && y >= 0 && x < field.width && y < rowCount && rows[y][x] != '.') {
					candidateX[candidates] = x + dx[d];
					candidateY[candidates] = y + dy[d];
					++candidates;
				}
			}
		}
		/* A stable insertion sort by steps along x plus along y from the leader. */
		for (int i = 1; i < candidates; ++i) {
			const int x = candidateX[i];
			const int y = candidateY[i];
			const int key = abs(x - field.leader_x) + abs(y - field.leader_y);
			int j = i - 1;
			while (j >= 0 && abs(candidateX[j] - field.leader_x) + abs(candidateY[j] - field.leader_y) > key) {
				candidateX[j + 1] = candidateX[j];
				candidateY[j + 1] = candidateY[j];
				--j;
			}
			candidateX[j + 1] = x;
			candidateY[j + 1] = y;
		}
		const psiv_exit* exits;
		int32_t exitCount = 0;
		psiv_field_exits(S, &exits, &exitCount);
		int moved = 0;
		for (int c = 0; c < candidates && !moved; ++c) {
			const int x = candidateX[c];
			const int y = candidateY[c];
			int skip = (x == field.leader_x && y == field.leader_y) || x < 0 || y < 0 || x >= field.width || y >= field.height;
			for (int32_t e = 0; e < exitCount; ++e) {
				skip = skip || (exits[e].x == x && exits[e].y == y);
			}
			int32_t status = PSIV_WALK_NO_PATH;
			if (skip || psiv_walk_to(S, x, y) != PSIV_OK || psiv_walk_step(S, &status) != PSIV_OK || status == PSIV_WALK_NO_PATH || status == PSIV_WALK_ARRIVED) {
				continue;
			}
			if (status != PSIV_WALK_WALKING) {
				return;
			}
			settle();
			moved = 1;
		}
		if (!moved) {
			snprintf(text, sizeof text, "no way to %s", actor);
			error(text);
			return;
		}
	}
	snprintf(text, sizeof text, "could not reach %s", actor);
	error(text);
}

static int actorIndex(const char* text, int* out) {
	if (toInt(text, out)) {
		return 1;
	}
	const psiv_member* party;
	int32_t count;
	psiv_party(S, &party, &count);
	for (int32_t i = 0; i < count; ++i) {
		if (party[i].in_lineup && strcmp(party[i].id, text) == 0) {
			*out = i;
			return 1;
		}
	}
	/* In a battle aboard, the vehicle acts for the party under its own id. */
	const psiv_combatant* combatants;
	if (psiv_battle_combatants(S, &combatants, &count) == PSIV_OK) {
		for (int32_t i = 0; i < count; ++i) {
			if (combatants[i].side == PSIV_SIDE_PARTY && strcmp(combatants[i].id, text) == 0) {
				*out = combatants[i].index;
				return 1;
			}
		}
	}
	return 0;
}

static int targetIndex(const char* text, int* out) {
	if (strlen(text) > 1 && (text[0] == 'e' || text[0] == 'p') && toInt(text + 1, out)) {
		if (text[0] == 'e') {
			psiv_battle_info info;
			psiv_battle_info_get(S, &info);
			*out += info.party_count;
		}
		return 1;
	}
	return toInt(text, out);
}

static void joinWords(char words[][LINE_SIZE], int from, int count, char* out, size_t size) {
	out[0] = '\0';
	for (int i = from; i < count; ++i) {
		if (out[0]) {
			strncat(out, " ", size - strlen(out) - 1);
		}
		strncat(out, words[i], size - strlen(out) - 1);
	}
}

/* Returns 0 after `quit`. */
static int execute(const char* rawLine) {
	char line[LINE_SIZE];
	char words[MAX_WORDS][LINE_SIZE];
	int wordCount = 0;
	/* Room for a whole line plus the text around it, so no message is cut short. */
	char message[LINE_SIZE + 64];
	snprintf(line, sizeof line, "%s", rawLine);
	char* hash = strchr(line, '#');
	if (hash) {
		*hash = '\0';
	}
	for (const char* p = line; *p && wordCount < MAX_WORDS;) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '\v' || *p == '\f') {
			++p;
		}
		if (!*p) {
			break;
		}
		size_t n = 0;
		while (*p && !(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '\v' || *p == '\f')) {
			if (n + 1 < LINE_SIZE) {
				words[wordCount][n++] = *p;
			}
			++p;
		}
		words[wordCount][n] = '\0';
		++wordCount;
	}
	if (wordCount == 0) {
		return 1;
	}
	if (echo) {
		print("%s> %s", modeName(), line + strspn(line, " \t"));
	}
	/* `refused COMMAND` runs the command and passes only when the game refuses it, so a script can show a refusal. */
	if (strcmp(words[0], "refused") == 0 && wordCount > 1) {
		const int before = errors;
		const char* inner = strstr(line, "refused") + 7;
		echo = 0;
		const int more = execute(inner);
		echo = 1;
		if (errors == before) {
			snprintf(message, sizeof message, "'%s' was not refused", inner + 1);
			error(message);
		} else {
			errors = before;
		}
		return more;
	}
	const char* cmd = words[0];
	const int32_t mode = psiv_mode_get(S);
	int facing;
	int count = 1;

	if (strcmp(cmd, "quit") == 0) {
		return 0;
	} else if (strcmp(cmd, "new") == 0) {
		report(psiv_new_game(S));
	} else if (strcmp(cmd, "continue") == 0) {
		report(psiv_continue_game(S));
	} else if (strcmp(cmd, "load") == 0 && wordCount == 2) {
		report(psiv_load(S, words[1]));
	} else if (strcmp(cmd, "erase") == 0 && wordCount == 2) {
		report(psiv_erase_save(S, words[1]));
	} else if (strcmp(cmd, "retry") == 0) {
		report(psiv_retry_battle(S));
	} else if (strcmp(cmd, "title") == 0) {
		psiv_quit_to_title(S);
	} else if (parseFacing(cmd, &facing) && (wordCount == 1 || (wordCount == 2 && toInt(words[1], &count)))) {
		for (int i = 0; i < count; ++i) {
			const psiv_status problem = psiv_walk(S, facing);
			if (problem != PSIV_OK) {
				report(problem);
				break;
			}
			settle();
			if (psiv_mode_get(S) != PSIV_MODE_FIELD) {
				break;
			}
		}
	} else if (strcmp(cmd, "face") == 0 && wordCount == 2 && parseFacing(words[1], &facing)) {
		report(psiv_face(S, facing));
	} else if (strcmp(cmd, "walk") == 0 && wordCount == 3 && strcmp(words[1], "to") == 0) {
		approach(words[2], 0);
	} else if (strcmp(cmd, "talk") == 0 && wordCount == 2) {
		approach(words[1], 1);
	} else if (strcmp(cmd, "walk") == 0 && (wordCount == 2 || wordCount == 3)) {
		int x = 0;
		int y = 0;
		int found = 0;
		if (wordCount == 3 && toInt(words[1], &x) && toInt(words[2], &y)) {
			found = 1;
		} else if (wordCount == 2) {
			const psiv_marker* markers;
			int32_t markerCount;
			if (psiv_field_markers(S, &markers, &markerCount) == PSIV_OK) {
				for (int32_t i = 0; i < markerCount; ++i) {
					if (strcmp(markers[i].name, words[1]) == 0) {
						x = markers[i].x;
						y = markers[i].y;
						found = 1;
					}
				}
			}
		}
		if (!found) {
			error("walk takes a marker of this area or x y");
			return 1;
		}
		walkTo(x, y);
	} else if ((strcmp(cmd, "talk") == 0 || strcmp(cmd, "x") == 0 || strcmp(cmd, "interact") == 0) && wordCount == 1) {
		report(psiv_interact(S));
	} else if (strcmp(cmd, "board") == 0 && wordCount == 1) {
		report(psiv_board(S));
	} else if (strcmp(cmd, "disembark") == 0 && wordCount == 1) {
		report(psiv_leave_vehicle(S));
	} else if (strcmp(cmd, "destinations") == 0 && wordCount == 2) {
		int32_t kind = 0;
		const psiv_travel_destination* places;
		int32_t placeCount = 0;
		if (psiv_parse_name(PSIV_NAME_TRAVEL_KIND, words[1], &kind) != PSIV_OK) {
			error("destinations port|town|dungeon_exit");
		} else if (psiv_travel_destinations(S, kind, &places, &placeCount) == PSIV_OK) {
			for (int32_t i = 0; i < placeCount; ++i) {
				print("  %s: %s (%d,%d)", places[i].id, places[i].area_name, places[i].x, places[i].y);
			}
		}
	} else if (strcmp(cmd, "travel") == 0 && wordCount >= 3 && wordCount <= 5) {
		int32_t kind = 0;
		const char* usage = "travel port DEST | travel town DEST THING [USER] | travel dungeon_exit THING [USER]";
		if (psiv_parse_name(PSIV_NAME_TRAVEL_KIND, words[1], &kind) != PSIV_OK) {
			error(usage);
		} else if (kind == PSIV_TRAVEL_PORT) {
			report(psiv_travel(S, kind, words[2], NULL, NULL));
		} else if (kind == PSIV_TRAVEL_TOWN && wordCount >= 4) {
			report(psiv_travel(S, kind, words[2], wordCount == 5 ? words[4] : NULL, words[3]));
		} else if (kind == PSIV_TRAVEL_DUNGEON_EXIT && wordCount <= 4) {
			report(psiv_travel(S, kind, NULL, wordCount == 4 ? words[3] : NULL, words[2]));
		} else {
			error(usage);
		}
	} else if (strcmp(cmd, "choose") == 0 && wordCount == 2 && toInt(words[1], &count)) {
		report(psiv_choose(S, count));
	} else if (strcmp(cmd, "skip") == 0) {
		if (psiv_skip(S) != PSIV_OK) {
			error("this scene cannot be skipped");
		}
	} else if (strcmp(cmd, "wait") == 0 && wordCount == 2 && toInt(words[1], &count)) {
		psiv_update(S, count, PSIV_DIRECTION_NONE);
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "attack") == 0 && wordCount >= 2) {
		int actor = 0;
		int target = -1;
		if (!actorIndex(words[1], &actor) || (wordCount == 3 && !targetIndex(words[2], &target))) {
			error("attack <member> [eN|pN]");
		} else {
			report(psiv_battle_set_command(S, actor, PSIV_COMMAND_ATTACK, "", target));
		}
	} else if (mode == PSIV_MODE_BATTLE && (strcmp(cmd, "use") == 0 || strcmp(cmd, "item") == 0) && wordCount >= 3) {
		int actor = 0;
		int target = -1;
		if (!actorIndex(words[1], &actor) || (wordCount == 4 && !targetIndex(words[3], &target))) {
			snprintf(message, sizeof message, "%s <member> <id> [eN|pN]", cmd);
			error(message);
		} else {
			report(psiv_battle_set_command(S, actor, strcmp(cmd, "use") == 0 ? PSIV_COMMAND_ABILITY : PSIV_COMMAND_ITEM, words[2], target));
		}
	} else if (mode == PSIV_MODE_BATTLE && (strcmp(cmd, "defend") == 0 || strcmp(cmd, "escape") == 0) && wordCount == 2) {
		int actor = 0;
		if (!actorIndex(words[1], &actor)) {
			snprintf(message, sizeof message, "%s <member>", cmd);
			error(message);
		} else {
			report(psiv_battle_set_command(S, actor, strcmp(cmd, "defend") == 0 ? PSIV_COMMAND_DEFEND : PSIV_COMMAND_ESCAPE, "", -1));
		}
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "fight") == 0 && (wordCount == 2 || wordCount == 3)) {
		int32_t fallback = PSIV_MACRO_FALLBACK_AUTOMATIC;
		if (wordCount == 3 && (psiv_parse_name(PSIV_NAME_MACRO_FALLBACK, words[2], &fallback) != PSIV_OK || fallback == PSIV_MACRO_FALLBACK_BASIC_ATTACK)) {
			error("fight NAME [automatic|attack|lose_turn]");
		} else {
			fight(words[1], fallback);
		}
	} else if (strcmp(cmd, "macros") == 0 && wordCount == 1) {
		listMacros();
	} else if (strcmp(cmd, "macro") == 0 && wordCount >= 3) {
		macro(words, wordCount);
	} else if (strcmp(cmd, "autobattle") == 0 && (wordCount == 2 || (wordCount <= 5 && strcmp(words[1], "off") != 0)) &&
		(strcmp(words[1], "on") == 0 || strcmp(words[1], "off") == 0 || strcmp(words[1], "random") == 0)) {
		autoBattle = strcmp(words[1], "off") != 0;
		autoEncountersOnly = strcmp(words[1], "random") == 0;
		int rest = wordCount - 2;
		autoPolicy = PSIV_POLICY_DEFAULT;
		if (rest > 0 && psiv_parse_name(PSIV_NAME_MACRO_POLICY, words[2 + rest - 1], &autoPolicy) == PSIV_OK && autoPolicy != PSIV_POLICY_DEFAULT) {
			--rest;
		} else {
			autoPolicy = PSIV_POLICY_DEFAULT;
		}
		autoTrouble = PSIV_MACRO_TROUBLE_DEFAULT;
		if (rest > 0 && psiv_parse_name(PSIV_NAME_MACRO_TROUBLE, words[2 + rest - 1], &autoTrouble) == PSIV_OK && autoTrouble != PSIV_MACRO_TROUBLE_DEFAULT) {
			--rest;
		} else {
			autoTrouble = PSIV_MACRO_TROUBLE_DEFAULT;
		}
		if (rest > 1) {
			error("autobattle on|random [MACRO] [continue|recover|stop] [power|expected]");
			rest = 0;
		}
		snprintf(autoMacro, sizeof autoMacro, "%.*s", NAME_SIZE - 1, rest == 1 ? words[2] : "");
		if (autoMacro[0] && !hasMacro(autoMacro)) {
			char message[LINE_SIZE];
			snprintf(message, sizeof message, "no macro named '%s'", autoMacro);
			error(message);
			autoMacro[0] = '\0';
		}
	} else if (strcmp(cmd, "dev") == 0 && wordCount >= 2) {
		dev(words, wordCount);
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "macro") == 0 && wordCount == 2) {
		report(psiv_battle_apply_macro(S, words[1], NULL, NULL));
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "round") == 0) {
		resolveAndPrint();
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "auto") == 0 && wordCount <= 3) {
		int32_t trouble = PSIV_MACRO_TROUBLE_DEFAULT;
		int32_t policy = PSIV_POLICY_DEFAULT;
		int ok = 1;
		for (int i = 1; i < wordCount; ++i) {
			if (psiv_parse_name(PSIV_NAME_MACRO_POLICY, words[i], &policy) == PSIV_OK && policy != PSIV_POLICY_DEFAULT && i + 1 == wordCount) {
				continue;
			}
			ok = ok && i == 1 && psiv_parse_name(PSIV_NAME_MACRO_TROUBLE, words[i], &trouble) == PSIV_OK && trouble != PSIV_MACRO_TROUBLE_DEFAULT;
		}
		if (!ok) {
			error("auto [continue|recover|stop] [power|expected]");
		} else {
			finishBattleAuto("", trouble, policy);
		}
	} else if (strcmp(cmd, "trouble") == 0 && wordCount == 2 && (strcmp(words[1], "on") == 0 || strcmp(words[1], "off") == 0)) {
		printTroubleLines = strcmp(words[1], "on") == 0;
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "cancel") == 0) {
		psiv_battle_cancel_auto(S);
	} else if (strcmp(cmd, "menu") == 0) {
		report(psiv_open_menu(S));
	} else if (strcmp(cmd, "close") == 0 || strcmp(cmd, "leave") == 0) {
		report(psiv_close_menu(S));
	} else if (strcmp(cmd, "item") == 0 && (wordCount == 2 || wordCount == 3)) {
		report(psiv_use_item(S, words[1], wordCount == 3 ? words[2] : ""));
	} else if (strcmp(cmd, "tech") == 0 && (wordCount == 3 || wordCount == 4)) {
		report(psiv_use_ability(S, words[1], words[2], wordCount == 4 ? words[3] : ""));
	} else if (strcmp(cmd, "name") == 0 && wordCount >= 2) {
		joinWords(words, 1, wordCount, message, sizeof message);
		report(psiv_enter_name(S, message));
	} else if (strcmp(cmd, "chat") == 0) {
		report(psiv_party_talk(S));
	} else if (strcmp(cmd, "quests") == 0) {
		psiv_quest_log log;
		psiv_quest_log_get(S, &log);
		print("  Objective: %s", log.objective[0] ? log.objective : "-");
		for (int32_t i = 0; i < log.job_count; ++i) {
			print("  Job: %s (%s)", log.jobs[i].title, psiv_name_of(PSIV_NAME_JOB_STATE, log.jobs[i].state));
		}
	} else if (strcmp(cmd, "codex") == 0) {
		const psiv_codex_entry* entries;
		const psiv_glossary_entry* glossary;
		int32_t entryCount;
		int32_t glossaryCount;
		psiv_encyclopedia(S, &entries, &entryCount);
		for (int32_t i = 0; i < entryCount; ++i) {
			print("  %s: %s", entries[i].title, entries[i].text);
		}
		psiv_glossary(S, &glossary, &glossaryCount);
		for (int32_t i = 0; i < glossaryCount; ++i) {
			print("  Glossary: %s (shipped as %s)", glossary[i].restored, glossary[i].shipped);
		}
	} else if (strcmp(cmd, "log") == 0) {
		const char* const* pages;
		int32_t pageCount;
		psiv_text_log(S, &pages, &pageCount);
		for (int32_t i = 0; i < pageCount; ++i) {
			print("  %s", pages[i]);
		}
	} else if (strcmp(cmd, "equip") == 0 && wordCount == 3) {
		report(psiv_equip(S, words[1], words[2]));
	} else if (strcmp(cmd, "unequip") == 0 && wordCount == 3) {
		int slot = -1;
		for (int i = 0; i < PSIV_SLOT_COUNT; ++i) {
			if (strcmp(psiv_name_of(PSIV_NAME_EQUIP_SLOT, i), words[2]) == 0) {
				slot = i;
			}
		}
		if (slot < 0) {
			snprintf(message, sizeof message, "unknown slot '%s'", words[2]);
			error(message);
		} else {
			report(psiv_unequip(S, words[1], slot));
		}
	} else if ((strcmp(cmd, "buy") == 0 || strcmp(cmd, "sell") == 0) && (wordCount == 2 || (wordCount == 3 && toInt(words[2], &count)))) {
		report(strcmp(cmd, "buy") == 0 ? psiv_buy(S, words[1], count) : psiv_sell(S, words[1], count));
	} else if (strcmp(cmd, "rest") == 0) {
		report(psiv_rest(S));
	} else if (strcmp(cmd, "save") == 0 && wordCount == 2) {
		report(psiv_save_manual(S, words[1]));
	} else if (strcmp(cmd, "saves") == 0) {
		const psiv_save_summary* saves;
		int32_t saveCount;
		if (psiv_saves(S, &saves, &saveCount) != PSIV_OK) {
			error("saving is disabled");
		} else {
			for (int32_t i = 0; i < saveCount; ++i) {
				const psiv_save_summary* s = &saves[i];
				char clock[32];
				Buffer party;
				bclear(&party);
				for (int32_t j = 0; j < s->party_count; ++j) {
					badd(&party, "%s%s", j ? " " : "", s->party[j]);
				}
				if (s->damaged) {
					print("  %s DAMAGED: %s", s->slot, s->error);
				} else {
					print("  %s %s %s [%s] %s%s", s->slot, s->area, clockText(s->play_time_ms, clock, sizeof clock), party.text, s->difficulty, s->autosave ? " (auto)" : "");
				}
			}
		}
	} else if (strcmp(cmd, "set") == 0 && wordCount == 3) {
		psiv_settings settings;
		psiv_settings_get(S, &settings);
		if (strcmp(words[1], "difficulty") == 0 || strcmp(words[1], "reward") == 0) {
			char difficulty[NAME_SIZE];
			int32_t reward = settings.reward_multiplier;
			snprintf(difficulty, sizeof difficulty, "%s", settings.difficulty);
			if (strcmp(words[1], "difficulty") == 0) {
				snprintf(difficulty, sizeof difficulty, "%.*s", NAME_SIZE - 1, words[2]);
			} else if (psiv_parse_name(PSIV_NAME_REWARD_MULTIPLIER, words[2], &reward) != PSIV_OK) {
				snprintf(message, sizeof message, "bad reward multiplier '%s'", words[2]);
				error(message);
				return 1;
			}
			report(psiv_settings_set(S, difficulty, reward));
		} else if (strcmp(words[1], "auto") == 0) {
			report(psiv_settings_set_auto_macro(S, strcmp(words[2], "-") == 0 ? "" : words[2]));
		} else {
			const char* key = strcmp(words[1], "text") == 0 ? "text_speed"
				: strcmp(words[1], "battle") == 0 ? "battle_speed"
				: strcmp(words[1], "shipped") == 0 ? "show_shipped_names"
				: words[1];
			if (psiv_profile_set_value(S, key, words[2]) != PSIV_OK) {
				psiv_error e;
				psiv_last_error(S, &e);
				error(e.text);
			}
		}
	} else if (strcmp(cmd, "party") == 0 && wordCount == 1) {
		partyLists();
	} else if (strcmp(cmd, "status") == 0) {
		status();
	} else if (strcmp(cmd, "look") == 0) {
		look();
	} else if (strcmp(cmd, "actors") == 0 && wordCount == 1) {
		actors();
	} else if (strcmp(cmd, "credits") == 0 && wordCount == 1) {
		credits();
	} else if (strcmp(cmd, "background") == 0 && wordCount == 1) {
		background();
	} else if (strcmp(cmd, "platforms") == 0 && wordCount == 1) {
		platforms();
	} else if (strcmp(cmd, "pictures") == 0 && wordCount == 1) {
		pictures();
	} else if (strcmp(cmd, "camera") == 0 && wordCount == 1) {
		sceneCamera();
	} else if (strcmp(cmd, "motion") == 0 && wordCount == 1) {
		motion();
	} else if (strcmp(cmd, "rows") == 0 && wordCount == 1) {
		rows();
	} else if (strcmp(cmd, "prompt") == 0 && wordCount == 1) {
		prompt();
	} else if (strcmp(cmd, "steps") == 0 && wordCount == 1) {
		steps();
	} else if (strcmp(cmd, "time") == 0 && wordCount == 1) {
		stepTime();
	} else if (strcmp(cmd, "pacing") == 0) {
		const char* table = "";
		psiv_pacing_table(S, &table);
		fputs(table, stdout);
	} else {
		snprintf(message, sizeof message, "unknown command '%s' in %s mode", line, psiv_name_of(PSIV_NAME_MODE, mode));
		error(message);
	}
	settle();
	return 1;
}

static int usage(void) {
	fprintf(stderr, "c_playthrough --data DIR --script FILE [--seed N] [--saves DIR] [--auto-battle]\n");
	return 2;
}

int main(int argc, char** argv) {
	psiv_session_options options;
	const char* script = NULL;
	psiv_session_options_init(&options);
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
			options.data_dir = argv[++i];
		} else if (strcmp(argv[i], "--saves") == 0 && i + 1 < argc) {
			options.save_dir = argv[++i];
		} else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
			script = argv[++i];
		} else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
			options.seed = strtoull(argv[++i], NULL, 10);
		} else if (strcmp(argv[i], "--auto-battle") == 0) {
			autoBattle = 1;
		} else {
			return usage();
		}
	}
	if (!options.data_dir || !script) {
		return usage();
	}
	if (psiv_session_create(&options, &S) != PSIV_OK) {
		psiv_error e;
		psiv_last_error(NULL, &e);
		fprintf(stderr, "%s\n", e.text);
		return 1;
	}
	FILE* file = fopen(script, "r");
	if (!file) {
		fprintf(stderr, "cannot read %s\n", script);
		psiv_session_destroy(S);
		return 1;
	}
	psiv_game_info info;
	psiv_game_info_get(S, &info);
	print("%s (seed %llu)", info.title, (unsigned long long)options.seed);
	settle();
	char line[LINE_SIZE];
	while (fgets(line, sizeof line, file)) {
		line[strcspn(line, "\n")] = '\0';
		if (!execute(line)) {
			break;
		}
	}
	fclose(file);
	print("End of script.");
	psiv_session_destroy(S);
	return errors == 0 ? 0 : 3;
}
