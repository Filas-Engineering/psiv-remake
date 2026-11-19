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
static char battleNames[MAX_COMBATANTS][NAME_SIZE];
static int battleNameCount;
static int tickMs = 50;

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

static void flushOutput(void) {
	const psiv_notice* notices;
	int32_t count;
	if (psiv_notices_drain(S, &notices, &count) != PSIV_OK) {
		return;
	}
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
		default:
			print("%s", notices[i].text);
			break;
		}
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
		case PSIV_EVENT_STATUS_TICKED:
			print("  %s suffers %d from %s.", battleName(e->target), e->amount, displayName(e->status));
			break;
		case PSIV_EVENT_STAT_MODIFIED:
			print("  %s's %s %s", battleName(e->target), psiv_name_of(PSIV_NAME_STAT, e->stat), e->amount > 0 ? "rises." : "falls.");
			break;
		case PSIV_EVENT_STAT_MODIFIER_EXPIRED:
			print("  %s's %s returns to normal.", battleName(e->target), psiv_name_of(PSIV_NAME_STAT, e->stat));
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

static void finishBattleAuto(void) {
	psiv_settings settings;
	psiv_battle_info info;
	psiv_settings_get(S, &settings);
	if (settings.auto_macro[0] && psiv_battle_info_get(S, &info) == PSIV_OK && info.round == 0 && !info.auto_active) {
		report(psiv_battle_start_auto(S));
	}
	for (int round = 0; round < 60 && psiv_mode_get(S) == PSIV_MODE_BATTLE; ++round) {
		int32_t filled = 0;
		psiv_battle_fill_from_auto(S, &filled);
		if (!filled) {
			psiv_battle_fill_from_policy(S);
		}
		flushOutput();
		resolveAndPrint();
	}
	if (psiv_mode_get(S) == PSIV_MODE_BATTLE) {
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
			if (!autoBattle) {
				return;
			}
			finishBattleAuto();
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
			if (choice.yes_no) {
				badd(&text, " [0] yes [1] no");
			} else {
				for (int32_t i = 0; i < choice.option_count; ++i) {
					badd(&text, " [%d] %s", i, choice.options[i]);
				}
			}
			print("%s", text.text);
			return;
		}
		psiv_update(S, 100, PSIV_DIRECTION_NONE);
	}
	error("the game did not settle");
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
		default:
			break;
		}
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

static int walkTo(int goalX, int goalY) {
	static const int dx[] = {0, 1, 0, -1};
	static const int dy[] = {-1, 0, 1, 0};
	for (int guard = 0; guard < 4000; ++guard) {
		psiv_field_info field;
		if (psiv_mode_get(S) != PSIV_MODE_FIELD || psiv_field_info_get(S, &field) != PSIV_OK) {
			return 0;
		}
		if (field.leader_x == goalX && field.leader_y == goalY) {
			return 1;
		}
		const uint8_t* grid;
		const psiv_exit* exits;
		int32_t cells;
		int32_t exitCount;
		psiv_field_grid(S, &grid, &cells);
		psiv_field_exits(S, &exits, &exitCount);
		const int w = field.width;
		const int h = field.height;
		int* from = malloc(sizeof(int) * (size_t)cells);
		int* queue = malloc(sizeof(int) * (size_t)cells);
		for (int i = 0; i < cells; ++i) {
			from[i] = -1;
		}
		const int start = field.leader_y * w + field.leader_x;
		const int goal = goalY * w + goalX;
		const int goalInside = goalX >= 0 && goalY >= 0 && goalX < w && goalY < h;
		int head = 0;
		int tail = 0;
		queue[tail++] = start;
		from[start] = start;
		/* Breadth-first search over floor; enemies are ignored, so bumping into one starts its battle. */
		while (head < tail && !(goalInside && from[goal] >= 0)) {
			const int c = queue[head++];
			for (int f = 0; f < 4; ++f) {
				const int nx = c % w + dx[f];
				const int ny = c / w + dy[f];
				if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
					continue;
				}
				const int n = ny * w + nx;
				int isExit = 0;
				for (int32_t e = 0; e < exitCount; ++e) {
					isExit = isExit || (exits[e].x == nx && exits[e].y == ny);
				}
				if (from[n] >= 0 || !(grid[n] & PSIV_CELL_WALKABLE) || (isExit && n != goal)) {
					continue;
				}
				from[n] = c;
				queue[tail++] = n;
			}
		}
		if (!goalInside || from[goal] < 0) {
			char text[64];
			snprintf(text, sizeof text, "no path to (%d,%d)", goalX, goalY);
			error(text);
			free(from);
			free(queue);
			return 0;
		}
		int next = goal;
		while (from[next] != start) {
			next = from[next];
		}
		free(from);
		free(queue);
		int dir = PSIV_DIRECTION_NORTH;
		for (int f = 0; f < 4; ++f) {
			if (field.leader_x + dx[f] == next % w && field.leader_y + dy[f] == next / w) {
				dir = f;
			}
		}
		char startArea[NAME_SIZE];
		snprintf(startArea, sizeof startArea, "%s", field.area_id);
		if (psiv_walk(S, dir) != PSIV_OK) {
			/* A visible enemy in the way during the grace period: let the moment pass. */
			psiv_update(S, tickMs, PSIV_DIRECTION_NONE);
		}
		settle();
		if (psiv_mode_get(S) != PSIV_MODE_FIELD || psiv_field_info_get(S, &field) != PSIV_OK) {
			return 0;
		}
		if (strcmp(field.area_id, startArea) != 0) {
			return 1;
		}
	}
	error("walking took too long");
	return 0;
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
	char message[LINE_SIZE];
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
	print("%s> %s", modeName(), line + strspn(line, " \t"));
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
		psiv_face(S, facing);
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
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "macro") == 0 && wordCount == 2) {
		report(psiv_battle_apply_macro(S, words[1], NULL, NULL));
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "round") == 0) {
		resolveAndPrint();
	} else if (mode == PSIV_MODE_BATTLE && strcmp(cmd, "auto") == 0) {
		finishBattleAuto();
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
			int reward = settings.reward_multiplier;
			snprintf(difficulty, sizeof difficulty, "%s", settings.difficulty);
			if (strcmp(words[1], "difficulty") == 0) {
				snprintf(difficulty, sizeof difficulty, "%s", words[2]);
			} else {
				reward = -1;
				for (int i = PSIV_REWARD_X1; i <= PSIV_REWARD_X2; ++i) {
					if (strcmp(psiv_name_of(PSIV_NAME_REWARD_MULTIPLIER, i), words[2]) == 0) {
						reward = i;
					}
				}
				if (reward < 0) {
					snprintf(message, sizeof message, "bad reward multiplier '%s'", words[2]);
					error(message);
					return 1;
				}
			}
			report(psiv_settings_set(S, difficulty, reward));
		} else if (strcmp(words[1], "auto") == 0) {
			const char* name = strcmp(words[2], "-") == 0 ? "" : words[2];
			if (psiv_settings_set_auto_macro(S, name) != PSIV_OK) {
				snprintf(message, sizeof message, "no macro named '%s'", words[2]);
				error(message);
			}
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
	} else if (strcmp(cmd, "status") == 0) {
		status();
	} else if (strcmp(cmd, "look") == 0) {
		look();
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
	tickMs = info.tick_ms;
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
