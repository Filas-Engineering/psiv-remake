/* Focused tests of the C interface: errors, string ownership, saves, a battle through the event stream,
 * presentation actions and text. */
#include "psiv_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#else
#define MAKE_DIR(path) mkdir(path, 0755)
#endif

static const char* dataDir = "../game/data";
static const char* scratch = "build/scratch";
static int failures;
static int checks;

#define CHECK(condition) \
	do { \
		++checks; \
		if (!(condition)) { \
			++failures; \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

#define CHECK_OK(call) \
	do { \
		++checks; \
		const psiv_status status_ = (call); \
		if (status_ != PSIV_OK) { \
			psiv_error e_; \
			psiv_last_error(s, &e_); \
			++failures; \
			fprintf(stderr, "%s:%d: %s returned %d: %s\n", __FILE__, __LINE__, #call, status_, e_.text); \
		} \
	} while (0)

static void makeDirectory(const char* path) {
	char partial[512];
	for (size_t i = 0; path[i]; ++i) {
		partial[i] = path[i];
		partial[i + 1] = '\0';
		if (path[i + 1] == '/' || path[i + 1] == '\0') {
			MAKE_DIR(partial);
		}
	}
}

static void removeFiles(const char* dir) {
	static const char* names[] = {"manual_0", "manual_1", "manual_2", "manual_3", "auto_0", "auto_1", "auto_2", "profile.cfg", "clear.sav"};
	char path[512];
	for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
		snprintf(path, sizeof path, "%s/%s%s", dir, names[i], strcmp(names[i], "profile.cfg") == 0 || strcmp(names[i], "clear.sav") == 0 ? "" : ".sav");
		remove(path);
	}
}

static psiv_session* create(const char* saves, int encounterMode, int presentation) {
	psiv_session_options options;
	psiv_session* s = NULL;
	psiv_session_options_init(&options);
	options.data_dir = dataDir;
	options.save_dir = saves;
	options.seed = 285;
	options.encounter_mode = encounterMode;
	options.presentation = presentation;
	if (psiv_session_create(&options, &s) != PSIV_OK) {
		psiv_error e;
		psiv_last_error(NULL, &e);
		fprintf(stderr, "cannot create a session: %s\n", e.text);
		exit(1);
	}
	return s;
}

/* Runs events forward: confirms lines, answers choices with 0, until control is back. */
static void settle(psiv_session* s) {
	for (int guard = 0; guard < 10000; ++guard) {
		const int32_t mode = psiv_mode_get(s);
		if (mode != PSIV_MODE_EVENT) {
			return;
		}
		if (psiv_line_pending(s)) {
			psiv_line_confirm(s);
		} else if (psiv_choice_pending(s)) {
			psiv_choose(s, 0);
		} else {
			psiv_update(s, 100, PSIV_DIRECTION_NONE);
		}
	}
}

static void testVersion(void) {
	CHECK(psiv_abi_version() == PSIV_ABI_VERSION);
	CHECK(strstr(psiv_version(), "psiv-bindings") != NULL);
	CHECK(strstr(psiv_version(), "game-0.2") != NULL);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MODE, PSIV_MODE_FIELD), "field") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_DIRECTION, PSIV_DIRECTION_NONE), "none") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_OUTCOME, PSIV_OUTCOME_VICTORY), "victory") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_CANCEL_REASON, PSIV_CANCEL_NOT_ENOUGH_TP), "not_enough_tp") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ACTION_TYPE, PSIV_ACTION_WARP), "warp") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_EVENT_TYPE, 999), "") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_EVENT_TYPE, PSIV_EVENT_BATTLE_ENDED), "battle_ended") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_REWARD_MULTIPLIER, PSIV_REWARD_X1_5), "x1_5") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MENU_KIND, PSIV_MENU_SAVE_PROMPT), "save_prompt") == 0);
	int32_t value = -1;
	CHECK(psiv_parse_name(PSIV_NAME_REWARD_MULTIPLIER, "1.5", &value) == PSIV_OK && value == PSIV_REWARD_X1_5);
	CHECK(psiv_parse_name(PSIV_NAME_REWARD_MULTIPLIER, "x2", &value) == PSIV_OK && value == PSIV_REWARD_X2);
	CHECK(psiv_parse_name(PSIV_NAME_EVENT_TYPE, "BattleEnded", &value) == PSIV_OK && value == PSIV_EVENT_BATTLE_ENDED);
	CHECK(psiv_parse_name(PSIV_NAME_DIRECTION, "none", &value) == PSIV_OK && value == PSIV_DIRECTION_NONE);
	CHECK(psiv_parse_name(PSIV_NAME_OUTCOME, "scripted_end", &value) == PSIV_OK && value == PSIV_OUTCOME_SCRIPTED_END);
	CHECK(psiv_parse_name(PSIV_NAME_OUTCOME, "won", &value) == PSIV_NOT_FOUND);
	CHECK(strcmp(psiv_name_of(999, 0), "") == 0);
}

static void testCreateErrors(void) {
	psiv_session* s = (psiv_session*)1;
	psiv_session_options options;
	psiv_error e;

	CHECK(psiv_session_create(NULL, &s) == PSIV_INVALID_ARGUMENT);
	CHECK(s == NULL);
	CHECK(psiv_last_error(NULL, &e) == PSIV_OK);
	CHECK(strcmp(e.key, "binding.null_argument") == 0);
	CHECK(e.param_count == 1 && strcmp(e.params[0], "options") == 0);

	psiv_session_options_init(&options);
	CHECK(psiv_session_create(&options, &s) == PSIV_INVALID_ARGUMENT);

	options.data_dir = "no/such/directory";
	CHECK(psiv_session_create(&options, &s) == PSIV_DATA_ERROR);
	CHECK(s == NULL);
	psiv_last_error(NULL, &e);
	CHECK(e.status == PSIV_DATA_ERROR);
	CHECK(strcmp(e.key, "binding.data_error") == 0);
	CHECK(strlen(e.text) > 0);
	const psiv_problem* problems;
	int32_t problemCount;
	CHECK(psiv_data_problems(&problems, &problemCount) == PSIV_OK);
	CHECK(problemCount == 1 && strcmp(problems[0].key, "data.cannot_read") == 0 && strcmp(problems[0].params[0], "no/such/directory/game.json") == 0);

	options.data_dir = dataDir;
	options.core_data_dir = "no/such/core";
	CHECK(psiv_session_create(&options, &s) == PSIV_DATA_ERROR);
	CHECK(psiv_data_problems(&problems, &problemCount) == PSIV_OK);
	CHECK(problemCount == 1 && strcmp(problems[0].key, "load.cannot_open") == 0 && strcmp(problems[0].file, "no/such/core/rules.json") == 0);
	CHECK(strcmp(problems[0].text, "no/such/core/rules.json: cannot open file") == 0);
	options.core_data_dir = "../core/data";
	CHECK(psiv_session_create(&options, &s) == PSIV_OK);
	psiv_session_destroy(s);
	CHECK(psiv_data_problems(&problems, &problemCount) == PSIV_OK && problemCount == 0);
	options.core_data_dir = NULL;

	options.data_dir = dataDir;
	options.struct_size = 4;
	CHECK(psiv_session_create(&options, &s) == PSIV_INVALID_ARGUMENT);
	psiv_last_error(NULL, &e);
	CHECK(strcmp(e.key, "binding.struct_size") == 0);

	psiv_session_options_init(&options);
	options.data_dir = dataDir;
	options.presentation = 9;
	CHECK(psiv_session_create(&options, &s) == PSIV_INVALID_ARGUMENT);

	/* Calls on a NULL session fail cleanly instead of crashing. */
	CHECK(psiv_new_game(NULL) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_mode_get(NULL) == -1);
	CHECK(psiv_line_pending(NULL) == 0);
	CHECK(psiv_flag(NULL, "x") == 0);
	CHECK(strcmp(psiv_name_entry_member(NULL), "") == 0);
	psiv_session_destroy(NULL);
}

static void testRefusals(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	psiv_error e;

	CHECK(psiv_last_error(s, &e) == PSIV_NOT_FOUND);
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE);
	CHECK(psiv_walk(s, PSIV_DIRECTION_EAST) == PSIV_REFUSED);
	CHECK(psiv_last_error(s, &e) == PSIV_OK);
	CHECK(e.status == PSIV_REFUSED);
	CHECK(strcmp(e.key, "game.refuse.walk_field_only") == 0);
	CHECK(strlen(e.text) > 0 && strcmp(e.text, e.key) != 0);

	CHECK(psiv_interact(s) == PSIV_REFUSED);
	CHECK(psiv_choose(s, 0) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.no_choice") == 0);
	CHECK(psiv_battle_set_command(s, 0, PSIV_COMMAND_ATTACK, "", -1) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.no_battle") == 0);
	CHECK(psiv_save_manual(s, "manual_0") == PSIV_REFUSED);
	CHECK(psiv_load(s, "manual_0") == PSIV_REFUSED);
	CHECK(psiv_load(s, NULL) == PSIV_INVALID_ARGUMENT);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "binding.null_argument") == 0 && strcmp(e.params[0], "slot") == 0);

	const psiv_save_summary* saves;
	int32_t count;
	CHECK(psiv_saves(s, &saves, &count) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.saving_disabled") == 0);

	psiv_field_info field;
	CHECK(psiv_field_info_get(s, &field) == PSIV_NOT_FOUND);
	CHECK(psiv_update(s, -5, PSIV_DIRECTION_NONE) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_update(s, 16, 7) == PSIV_INVALID_ARGUMENT);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "binding.out_of_range") == 0 && strcmp(e.params[0], "direction") == 0 && strcmp(e.params[1], "7") == 0);

	CHECK_OK(psiv_new_game(s));
	CHECK(psiv_new_game(s) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.new_game_title") == 0);
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK(psiv_buy(s, "tonic", 1) == PSIV_REFUSED);
	CHECK(psiv_unequip(s, "fighter", 42) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_settings_set_auto_macro(s, "no_such_macro") == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.no_macro") == 0 && e.param_count == 1 && strcmp(e.params[0], "no_such_macro") == 0);
	CHECK(psiv_settings_set(s, "no_such_difficulty", PSIV_REWARD_X1) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.unknown_difficulty") == 0);
	CHECK(e.param_count == 1 && strcmp(e.params[0], "no_such_difficulty") == 0);
	CHECK(psiv_profile_set_value(s, "text_language", "xx") == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.unknown_language") == 0);
	CHECK(psiv_profile_set_value(s, "no_such_key", "1") == PSIV_REFUSED);
	CHECK(psiv_skip(s) == PSIV_REFUSED);
	psiv_session_destroy(s);
}

static void testStringOwnership(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	psiv_field_info field;
	psiv_error e;
	char areaCopy[64];
	char nameCopy[64];

	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_field_info_get(s, &field));
	snprintf(areaCopy, sizeof areaCopy, "%s", field.area_id);
	snprintf(nameCopy, sizeof nameCopy, "%s", field.area_name);
	const char* area = field.area_id;

	/* Reads never invalidate earlier results. */
	const psiv_member* party;
	const psiv_point* points;
	const char* rendered;
	int32_t count;
	for (int i = 0; i < 50; ++i) {
		CHECK_OK(psiv_party(s, &party, &count));
		CHECK_OK(psiv_field_points(s, &points, &count));
		CHECK_OK(psiv_display_name(s, "fighter", &rendered));
	}
	CHECK(strcmp(area, areaCopy) == 0);
	CHECK(strcmp(field.area_name, nameCopy) == 0);
	CHECK(strcmp(party[0].id, "fighter") == 0);

	/* A pointer the library returned can go back in, even to a MUTATES call. */
	const char* fighterId = party[0].id;
	CHECK_OK(psiv_open_menu(s));
	CHECK(psiv_equip(s, fighterId, "no_such_piece") == PSIV_REFUSED);
	CHECK(psiv_last_error(s, &e) == PSIV_OK);
	const char* errorKey = e.key;
	char errorCopy[128];
	snprintf(errorCopy, sizeof errorCopy, "%s", e.key);

	/* The error record survives later successful calls, MUTATES ones included. */
	CHECK_OK(psiv_close_menu(s));
	CHECK_OK(psiv_party(s, &party, &count));
	CHECK(strcmp(errorKey, errorCopy) == 0);

	/* Every string field is non-NULL, even when empty. */
	psiv_settings settings;
	CHECK_OK(psiv_settings_get(s, &settings));
	CHECK(settings.auto_macro != NULL && settings.auto_macro[0] == '\0');
	CHECK(strcmp(psiv_name_entry_member(s), "") == 0);

	/* Rendering and raw templates. */
	const char* params[] = {"fighter", "tonic"};
	const char* raw;
	CHECK_OK(psiv_render(s, "game.equipped", params, 2, &rendered));
	CHECK_OK(psiv_format_text(s, "game.equipped", params, 2, &raw));
	CHECK(strstr(rendered, "Test Fighter") != NULL);
	CHECK(strstr(raw, "fighter") != NULL);
	CHECK(psiv_render(s, "game.equipped", NULL, 2, &rendered) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_string_text(s, "speaker.dockhand", &rendered));
	CHECK(strcmp(rendered, "Dockhand") == 0);
	psiv_session_destroy(s);
}

static void testSaveAndLoad(void) {
	char saves[512];
	char path[600];
	snprintf(saves, sizeof saves, "%s/saves", scratch);
	makeDirectory(saves);
	removeFiles(saves);

	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	psiv_field_info field;
	psiv_world_info world;
	const psiv_save_summary* list;
	int32_t count;
	CHECK(psiv_continue_game(s) == PSIV_REFUSED);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.autosave_count == 1);

	CHECK_OK(psiv_open_menu(s));
	CHECK(psiv_menu_kind_get(s) == PSIV_MENU_FIELD);
	CHECK(psiv_save_manual(s, "manual_99") == PSIV_REFUSED);
	CHECK(psiv_save_manual(s, "auto_0") == PSIV_REFUSED);
	CHECK_OK(psiv_save_manual(s, "manual_3"));
	CHECK_OK(psiv_close_menu(s));
	CHECK_OK(psiv_field_info_get(s, &field));
	const int savedX = field.leader_x;
	const int savedMoney = world.money;

	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_SOUTH));
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_SOUTH));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_y != 4);

	CHECK_OK(psiv_saves(s, &list, &count));
	int found = 0;
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(list[i].slot, "manual_3") == 0) {
			found = 1;
			CHECK(!list[i].autosave && !list[i].damaged);
			CHECK(strcmp(list[i].area, "tidewater") == 0);
			CHECK(list[i].party_count == 1 && strcmp(list[i].party[0], "fighter:3") == 0);
		}
	}
	CHECK(found);

	CHECK_OK(psiv_quit_to_title(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE);
	CHECK_OK(psiv_load(s, "manual_3"));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_x == savedX && field.leader_y == 4);
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.money == savedMoney);
	psiv_session_destroy(s);

	/* A second session on the same directory continues from the newest save. */
	s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK_OK(psiv_continue_game(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK(psiv_flag(s, "tidewater.arrived"));
	CHECK(!psiv_flag(s, "no.such.flag"));
	CHECK_OK(psiv_quit_to_title(s));

	/* A damaged file is listed as damaged and refused. */
	snprintf(path, sizeof path, "%s/manual_3.sav", saves);
	FILE* f = fopen(path, "r+b");
	CHECK(f != NULL);
	if (f) {
		fseek(f, 40, SEEK_SET);
		fputc('#', f);
		fclose(f);
	}
	CHECK_OK(psiv_saves(s, &list, &count));
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(list[i].slot, "manual_3") == 0) {
			CHECK(list[i].damaged);
			CHECK(strlen(list[i].error) > 0);
		}
	}
	CHECK(psiv_load(s, list[0].slot) == PSIV_REFUSED);
	psiv_session_destroy(s);
}

static void testBattleStream(void) {
	psiv_session* s = create(NULL, PSIV_ENCOUNTERS_RANDOM, PSIV_PRESENTATION_INSTANT);
	psiv_field_info field;
	CHECK_OK(psiv_new_game(s));
	settle(s);
	for (int i = 0; i < 10; ++i) {
		CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
		settle(s);
	}
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "reed_path") == 0);
	CHECK(field.encounter_mode == PSIV_ENCOUNTERS_RANDOM);

	/* Pace inside the west reeds until the random counter starts a battle. */
	int direction = PSIV_DIRECTION_EAST;
	for (int step = 0; step < 200 && psiv_mode_get(s) == PSIV_MODE_FIELD; ++step) {
		CHECK_OK(psiv_field_info_get(s, &field));
		if (field.leader_x >= 8) {
			direction = PSIV_DIRECTION_WEST;
		} else if (field.leader_x <= 2) {
			direction = PSIV_DIRECTION_EAST;
		}
		psiv_walk(s, direction);
		settle(s);
	}
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);

	psiv_battle_info info;
	const psiv_combatant* combatants;
	int32_t combatantCount;
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(info.live && info.round == 0);
	CHECK(info.source == PSIV_BATTLE_RANDOM);
	CHECK(strcmp(info.encounter, "training") == 0);
	CHECK_OK(psiv_battle_combatants(s, &combatants, &combatantCount));
	CHECK(combatantCount == info.combatant_count);
	CHECK(info.party_count == 1);
	CHECK(combatants[0].side == PSIV_SIDE_PARTY && strcmp(combatants[0].id, "fighter") == 0);
	CHECK(combatants[1].side == PSIV_SIDE_ENEMY);

	int hp[16];
	for (int32_t i = 0; i < combatantCount && i < 16; ++i) {
		hp[i] = combatants[i].hp;
		CHECK(combatants[i].max_hp >= combatants[i].hp && combatants[i].hp > 0);
	}

	const psiv_battle_option* options;
	int32_t optionCount;
	CHECK_OK(psiv_battle_options(s, 0, &options, &optionCount));
	CHECK(optionCount > 0);
	CHECK(psiv_battle_options(s, combatantCount, &options, &optionCount) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_battle_set_command(s, 0, PSIV_COMMAND_ABILITY, "no_such_ability", -1) == PSIV_REFUSED);
	CHECK(psiv_battle_set_command(s, 0, PSIV_COMMAND_ATTACK, "", 99) == PSIV_INVALID_ARGUMENT);

	int sawStart = 0;
	int sawLocked = 0;
	int sawOrder = 0;
	int sawEnd = 0;
	int rounds = 0;
	int lastRound = 0;
	int32_t outcome = PSIV_OUTCOME_ONGOING;
	while (psiv_mode_get(s) == PSIV_MODE_BATTLE && rounds < 40) {
		CHECK_OK(psiv_battle_combatants(s, &combatants, &combatantCount));
		int target = -1;
		for (int32_t i = 0; i < combatantCount; ++i) {
			if (combatants[i].side == PSIV_SIDE_ENEMY && combatants[i].alive && target < 0) {
				target = i;
			}
		}
		CHECK_OK(psiv_battle_set_command(s, 0, PSIV_COMMAND_ATTACK, "", target));
		CHECK_OK(psiv_battle_combatants(s, &combatants, &combatantCount));
		CHECK(combatants[0].command_kind == PSIV_COMMAND_ATTACK && combatants[0].command_target == target);

		const psiv_battle_event* events;
		int32_t count;
		CHECK_OK(psiv_battle_resolve_round(s, &events, &count));
		++rounds;
		CHECK(count > 0);
		for (int32_t i = 0; i < count; ++i) {
			const psiv_battle_event* e = &events[i];
			switch (e->type) {
			case PSIV_EVENT_BATTLE_STARTED:
				sawStart = 1;
				CHECK(rounds == 1 && i == 0);
				CHECK(e->id_count == combatantCount && e->party_count == 1);
				CHECK(strcmp(e->ids[0], "fighter") == 0 && e->groups[0] != NULL);
				break;
			case PSIV_EVENT_ROUND_STARTED:
				CHECK(e->round == lastRound + 1);
				lastRound = e->round;
				break;
			case PSIV_EVENT_COMMANDS_LOCKED:
				sawLocked = 1;
				CHECK(e->command_count >= 1);
				CHECK(e->commands[0].actor == 0 && e->commands[0].kind == PSIV_COMMAND_ATTACK);
				break;
			case PSIV_EVENT_ORDER_RESOLVED:
				sawOrder = 1;
				CHECK(e->order_count >= 1);
				break;
			case PSIV_EVENT_DAMAGE_DEALT:
				CHECK(e->target >= 0 && e->target < combatantCount);
				CHECK(e->hp == (hp[e->target] - e->amount > 0 ? hp[e->target] - e->amount : 0));
				hp[e->target] = e->hp;
				break;
			case PSIV_EVENT_HEALED:
				hp[e->target] = e->hp;
				break;
			case PSIV_EVENT_STATUS_APPLIED:
			case PSIV_EVENT_STATUS_RESISTED:
			case PSIV_EVENT_STATUS_REMOVED:
			case PSIV_EVENT_MESSAGE:
				CHECK(strlen(e->text) > 0);
				break;
			case PSIV_EVENT_BATTLE_ENDED:
				sawEnd = 1;
				outcome = e->outcome;
				CHECK(i == count - 1);
				break;
			default:
				break;
			}
		}
	}
	CHECK(sawStart && sawLocked && sawOrder && sawEnd);
	CHECK(outcome == PSIV_OUTCOME_VICTORY);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);

	/* The ended battle stays readable until the next one, with the last round applied. */
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(!info.live && info.outcome == PSIV_OUTCOME_VICTORY && info.round == rounds);
	CHECK_OK(psiv_battle_combatants(s, &combatants, &combatantCount));
	for (int32_t i = 0; i < combatantCount; ++i) {
		CHECK(combatants[i].hp == hp[i]);
		if (combatants[i].side == PSIV_SIDE_ENEMY) {
			CHECK(!combatants[i].alive);
		}
	}
	const psiv_battle_event* again;
	int32_t againCount;
	CHECK_OK(psiv_battle_last_events(s, &again, &againCount));
	CHECK(againCount > 0 && again[againCount - 1].type == PSIV_EVENT_BATTLE_ENDED);

	/* Rewards arrive as notices. */
	const psiv_notice* notices;
	int32_t noticeCount;
	int sawVictory = 0;
	CHECK_OK(psiv_notices_drain(s, &notices, &noticeCount));
	for (int32_t i = 0; i < noticeCount; ++i) {
		sawVictory = sawVictory || strcmp(notices[i].key, "game.victory") == 0;
	}
	CHECK(sawVictory);
	CHECK(psiv_battle_resolve_round(s, &again, &againCount) == PSIV_REFUSED);
	psiv_session_destroy(s);
}

static int callbackCount;
static uint64_t callbackHandle;

static void onAction(void* user, const psiv_action* action) {
	++*(int*)user;
	if (action->phase == PSIV_ACTION_BEGIN && action->type == PSIV_ACTION_FADE_IN) {
		callbackHandle = action->handle;
	}
}

static void testPresentation(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_WAIT);
	const psiv_action* actions;
	int32_t count;
	CHECK_OK(psiv_actions_set_callback(s, onAction, &callbackCount));
	CHECK_OK(psiv_new_game(s));
	for (int i = 0; i < 20; ++i) {
		CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	}
	/* The arrival event fades in first and waits for the engine to finish the fade. */
	CHECK(psiv_mode_get(s) == PSIV_MODE_EVENT);
	CHECK(!psiv_line_pending(s));
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	CHECK(count == 1);
	CHECK(actions[0].phase == PSIV_ACTION_BEGIN && actions[0].type == PSIV_ACTION_FADE_IN);
	CHECK(actions[0].seconds == 1.0 && !actions[0].instant);
	CHECK(strcmp(actions[0].description, "fade in 1") == 0);
	CHECK(callbackCount == 1 && callbackHandle == actions[0].handle);
	const uint64_t handle = actions[0].handle;
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	CHECK(count == 0);
	CHECK_OK(psiv_action_finish(s, handle));
	CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	CHECK(psiv_line_pending(s));

	psiv_line line;
	CHECK_OK(psiv_line_get(s, &line));
	CHECK(strcmp(line.speaker_id, "") == 0);
	CHECK(strcmp(line.text, line.display) == 0);
	CHECK_OK(psiv_line_confirm(s));
	/* The dockhand walks over and turns before speaking; each step waits for the engine, while psiv_field_actors
	 * already reports where it ends. */
	const char* expected[] = {"move dockhand to dock_front", "move dockhand to greet_spot", "face dockhand fighter"};
	const int32_t cells[][3] = {{10, 3, PSIV_DIRECTION_SOUTH}, {5, 4, PSIV_DIRECTION_SOUTH}, {5, 4, PSIV_DIRECTION_WEST}};
	for (int step = 0; step < 3; ++step) {
		CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
		CHECK(!psiv_line_pending(s));
		CHECK_OK(psiv_actions_poll(s, &actions, &count));
		CHECK(count == 1 && strcmp(actions[0].description, expected[step]) == 0);
		const psiv_actor* actors;
		int32_t actorCount;
		CHECK_OK(psiv_field_actors(s, &actors, &actorCount));
		CHECK(actorCount == 2 && strcmp(actors[0].id, "dockhand") == 0 && actors[0].scripted);
		CHECK(actorCount >= 1 && actors[0].x == cells[step][0] && actors[0].y == cells[step][1] && actors[0].facing == cells[step][2]);
		if (count == 1) {
			CHECK_OK(psiv_action_finish(s, actions[0].handle));
		}
	}
	CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	CHECK_OK(psiv_line_get(s, &line));
	CHECK(strcmp(line.speaker_id, "dockhand") == 0);
	CHECK(strcmp(line.speaker_name, "Dockhand") == 0);
	CHECK(strncmp(line.display, "Dockhand: ", 10) == 0 && strcmp(line.display + 10, line.text) == 0);
	CHECK(psiv_line_confirm(s) == PSIV_OK);
	for (int guard = 0; guard < 20 && psiv_mode_get(s) == PSIV_MODE_EVENT; ++guard) {
		CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
		CHECK_OK(psiv_actions_poll(s, &actions, &count));
		for (int32_t i = 0; i < count; ++i) {
			CHECK_OK(psiv_action_finish(s, actions[i].handle));
		}
	}
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	psiv_session_destroy(s);

	/* Skipping a skippable scene logs the lines it passes over. */
	s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	CHECK_OK(psiv_new_game(s));
	CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	CHECK(psiv_can_skip(s));
	CHECK_OK(psiv_skip(s));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	const char* const* pages;
	int32_t pageCount;
	CHECK_OK(psiv_text_log(s, &pages, &pageCount));
	CHECK(pageCount >= 1);
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	CHECK(count >= 1 && actions[0].type == PSIV_ACTION_FADE_IN);
	psiv_session_destroy(s);
}

static void testFieldAndMenus(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	psiv_field_info field;
	const uint8_t* grid;
	int32_t cells;
	const psiv_exit* exits;
	const psiv_point* points;
	const psiv_marker* markers;
	int32_t count;
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "tidewater") == 0 && field.area_kind == PSIV_AREA_TOWN);
	CHECK(field.width == 14 && field.height == 9);
	CHECK(field.leader_x == 3 && field.leader_y == 4 && field.facing == PSIV_DIRECTION_EAST);
	CHECK_OK(psiv_field_grid(s, &grid, &cells));
	CHECK(cells == 14 * 9);
	CHECK((grid[0] & PSIV_CELL_TERRAIN_MASK) == PSIV_CELL_WALL && !(grid[0] & PSIV_CELL_WALKABLE));
	CHECK(grid[4 * 14 + 3] & PSIV_CELL_WALKABLE);
	/* The guide stands on a floor cell, which is therefore not walkable. */
	CHECK((grid[2 * 14 + 5] & PSIV_CELL_TERRAIN_MASK) == PSIV_CELL_FLOOR && !(grid[2 * 14 + 5] & PSIV_CELL_WALKABLE));
	CHECK_OK(psiv_field_exits(s, &exits, &count));
	CHECK(count == 1 && exits[0].x == 13 && strcmp(exits[0].to_area, "reed_path") == 0);
	CHECK_OK(psiv_field_markers(s, &markers, &count));
	CHECK(count == 16);
	CHECK_OK(psiv_field_points(s, &points, &count));
	int talk = 0;
	int shop = 0;
	int rest = 0;
	int chest = 0;
	for (int32_t i = 0; i < count; ++i) {
		talk += points[i].kind == PSIV_POINT_TALK;
		shop += points[i].kind == PSIV_POINT_SHOP;
		rest += points[i].kind == PSIV_POINT_REST;
		chest += points[i].kind == PSIV_POINT_CHEST;
	}
	CHECK(talk == 5 && shop == 1 && rest == 1 && chest == 1);

	/* Shop at the stall (8,2) from stall_front (8,3). */
	for (int i = 0; i < 5; ++i) {
		CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
	}
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_SHOP);
	psiv_shop_info shopInfo;
	CHECK_OK(psiv_shop_get(s, &shopInfo));
	CHECK(shopInfo.stock_count > 0 && shopInfo.stock[0].price > 0);
	CHECK(psiv_buy(s, "rivet_plate", 99) == PSIV_REFUSED);
	CHECK_OK(psiv_buy(s, "tonic", 1));
	CHECK(psiv_sell(s, "tonic", 50) == PSIV_REFUSED);
	CHECK_OK(psiv_close_menu(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);

	const psiv_inventory_entry* items;
	CHECK_OK(psiv_inventory(s, &items, &count));
	CHECK(count >= 1 && strcmp(items[0].id, "tonic") == 0 && items[0].count == 2 && !items[0].equipment);

	CHECK_OK(psiv_open_menu(s));
	CHECK(psiv_use_item(s, "tonic", "fighter") == PSIV_REFUSED);
	const psiv_ability_info* abilities;
	CHECK_OK(psiv_member_abilities(s, "fighter", &abilities, &count));
	CHECK(count >= 1);
	CHECK(psiv_member_abilities(s, "nobody", &abilities, &count) == PSIV_NOT_FOUND);
	const psiv_member* party;
	CHECK_OK(psiv_party(s, &party, &count));
	CHECK(count == 1 && party[0].in_lineup && party[0].level == 3 && party[0].max_hp >= party[0].hp);
	CHECK(party[0].equipment[PSIV_SLOT_WEAPON][0] != '\0');
	char weapon[64];
	snprintf(weapon, sizeof weapon, "%s", party[0].equipment[PSIV_SLOT_WEAPON]);
	CHECK_OK(psiv_unequip(s, "fighter", PSIV_SLOT_WEAPON));
	CHECK_OK(psiv_equip(s, "fighter", weapon));
	CHECK_OK(psiv_close_menu(s));

	psiv_quest_log log;
	CHECK_OK(psiv_quest_log_get(s, &log));
	CHECK(strlen(log.objective) > 0 && log.job_count == 1 && log.jobs[0].state == PSIV_JOB_AVAILABLE);
	const psiv_codex_entry* codex;
	CHECK_OK(psiv_encyclopedia(s, &codex, &count));
	CHECK(count == 1);
	const psiv_status talkStatus = psiv_party_talk(s);
	CHECK(talkStatus == PSIV_OK || talkStatus == PSIV_REFUSED);

	/* Profile changes show at once: the placeholder Japanese tables render differently. */
	CHECK_OK(psiv_quest_log_get(s, &log));
	char english[512];
	snprintf(english, sizeof english, "%s", log.objective);
	CHECK_OK(psiv_profile_set_value(s, "text_language", "ja"));
	psiv_profile profile;
	CHECK_OK(psiv_profile_get(s, &profile));
	CHECK(strcmp(profile.text_language, "ja") == 0);
	CHECK_OK(psiv_quest_log_get(s, &log));
	CHECK(strcmp(english, log.objective) != 0);
	profile.text_language = "en";
	CHECK_OK(psiv_profile_set(s, &profile));
	CHECK_OK(psiv_quest_log_get(s, &log));
	CHECK(strcmp(english, log.objective) == 0);

	psiv_game_info game;
	CHECK_OK(psiv_game_info_get(s, &game));
	CHECK(strcmp(game.title, "The Bell at Tidewater") == 0 && game.tick_ms == 50 && game.language_count == 2);
	psiv_session_destroy(s);
}

/* Walks to a marker of the current area with psiv_walk_to, running events between the steps. */
static int32_t walkToMarker(psiv_session* s, const char* name) {
	const psiv_marker* markers;
	int32_t count;
	int32_t status = -1;
	if (psiv_field_markers(s, &markers, &count) != PSIV_OK) {
		return status;
	}
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(markers[i].name, name) == 0 && psiv_walk_to(s, markers[i].x, markers[i].y) == PSIV_OK) {
			while (psiv_walk_step(s, &status) == PSIV_OK && status == PSIV_WALK_WALKING) {
				settle(s);
			}
			return status;
		}
	}
	return status;
}

/* The paths the placeholder adventure added for hosts: line data with portraits, localized yes and no, a
 * name entry, a lost event battle kept readable and retried, its record replayed, and the pacing numbers. */
static void testAdventurePaths(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/paths", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	psiv_line line;
	psiv_choice choice;
	psiv_error e;
	int32_t status = -1;
	CHECK_OK(psiv_new_game(s));
	settle(s);

	CHECK(walkToMarker(s, "hold_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	const psiv_combatant* combatants;
	int32_t count;
	CHECK_OK(psiv_battle_combatants(s, &combatants, &count));
	CHECK(count == 4 && strcmp(combatants[0].name, "Test Fighter") == 0 && strcmp(combatants[1].label, "Gel Mite A") == 0);
	psiv_auto_battle played;
	CHECK_OK(psiv_battle_play(s, 0, &played));
	CHECK(played.ended && played.round_count >= 1 && strcmp(played.refusal_key, "") == 0);
	CHECK(played.rounds[0].event_count > 0 && played.rounds[0].events[0].type == PSIV_EVENT_BATTLE_STARTED);
	/* The fighter alone falls to the enemy-first ambush: a game over, with the battle still readable. */
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE);
	psiv_battle_info info;
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(!info.live && info.outcome == PSIV_OUTCOME_DEFEAT && info.game_over && info.source == PSIV_BATTLE_EVENT);
	CHECK_OK(psiv_battle_combatants(s, &combatants, &count));
	CHECK(count == 4 && !combatants[0].alive);
	psiv_battle_result result;
	CHECK_OK(psiv_battle_result_get(s, &result));
	CHECK(strcmp(result.encounter, "training") == 0 && result.outcome == PSIV_OUTCOME_DEFEAT && result.game_over);
	CHECK(result.experience == 0 && result.drop_count == 0 && result.level_up_count == 0);

	const char* record;
	CHECK_OK(psiv_battle_record(s, &record));
	char* recordCopy = malloc(strlen(record) + 1);
	strcpy(recordCopy, record);
	const psiv_battle_event* replayed;
	int32_t replayedCount;
	const char* eventText;
	CHECK_OK(psiv_battle_replay(s, recordCopy, &replayed, &replayedCount, &eventText));
	CHECK(replayedCount > 0 && replayed[replayedCount - 1].type == PSIV_EVENT_BATTLE_ENDED && replayed[replayedCount - 1].outcome == PSIV_OUTCOME_DEFEAT);
	CHECK(strncmp(eventText, "0 battle_started", 16) == 0);
	recordCopy[strlen(recordCopy) / 2] ^= 1;
	CHECK(psiv_battle_replay(s, recordCopy, &replayed, &replayedCount, NULL) == PSIV_DATA_ERROR);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "binding.bad_record") == 0);
	free(recordCopy);

	psiv_world_info world;
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.game_over && world.can_retry);
	CHECK_OK(psiv_retry_battle(s));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	CHECK(psiv_battle_result_get(s, &result) == PSIV_NOT_FOUND);
	CHECK_OK(psiv_battle_play(s, 0, &played));
	CHECK(played.ended);
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE || psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK_OK(psiv_battle_dismiss(s));
	CHECK(psiv_battle_info_get(s, &info) == PSIV_NOT_FOUND);
	CHECK(psiv_walk_step(s, &status) == PSIV_NOT_FOUND);
	psiv_session_destroy(s);

	/* The guide: a portrait on the first line, the string table's yes and no, and a name entry. */
	removeFiles(saves);
	s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK(walkToMarker(s, "guide_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	CHECK_OK(psiv_line_get(s, &line));
	CHECK(strcmp(line.speaker_id, "guide") == 0 && strcmp(line.speaker_name, "Harbor Guide") == 0);
	CHECK(strcmp(line.text_id, "tidewater.guide.bell") == 0 && strcmp(line.portrait, "guide_smile") == 0);
	CHECK_OK(psiv_line_confirm(s));
	CHECK_OK(psiv_choice_get(s, &choice));
	CHECK(choice.yes_no && choice.option_count == 2 && strcmp(choice.options[0], "Yes") == 0 && strcmp(choice.options[1], "No") == 0);
	CHECK_OK(psiv_profile_set_value(s, "text_language", "ja"));
	CHECK_OK(psiv_choice_get(s, &choice));
	CHECK(strcmp(choice.options[0], "\xE3\x81\xAF\xE3\x81\x84") == 0);
	CHECK_OK(psiv_profile_set_value(s, "text_language", "en"));
	CHECK_OK(psiv_choose(s, 0));
	CHECK_OK(psiv_line_confirm(s));
	CHECK(psiv_menu_kind_get(s) == PSIV_MENU_NAME_ENTRY);
	CHECK(strcmp(psiv_name_entry_member(s), "fighter") == 0);
	CHECK(psiv_enter_name(s, "") == PSIV_REFUSED);
	CHECK_OK(psiv_enter_name(s, "Bryn"));
	CHECK_OK(psiv_line_get(s, &line));
	CHECK(strcmp(line.speaker_id, "fighter") == 0 && strcmp(line.speaker_name, "Bryn") == 0 && strcmp(line.portrait, "") == 0);
	CHECK(strcmp(line.display, "Bryn: Tell the ferry I am staying a while.") == 0);
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK(walkToMarker(s, "no_such_marker") == -1);
	CHECK_OK(psiv_walk_to(s, 0, 0));
	CHECK_OK(psiv_walk_step(s, &status));
	CHECK(status == PSIV_WALK_NO_PATH);

	const psiv_pacing_area* areas;
	const psiv_pacing_boss* bosses;
	CHECK_OK(psiv_pacing_areas(s, &areas, &count));
	CHECK(count == 1 && strcmp(areas[0].area, "tidewater") == 0 && areas[0].visits == 1 && areas[0].steps > 0 && areas[0].battles == 0);
	CHECK_OK(psiv_pacing_bosses(s, &bosses, &count));
	CHECK(count == 0);
	psiv_session_destroy(s);
}

static const psiv_actor* findActor(const psiv_actor* actors, int32_t count, const char* id) {
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(actors[i].id, id) == 0) {
			return &actors[i];
		}
	}
	return NULL;
}

/* Script actors as state: untouched ones at their talk points, the arrival's walk, saves and loads. */
static void testActors(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/actors", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	const psiv_actor* actors;
	int32_t count;
	CHECK(psiv_field_actors(s, &actors, &count) == PSIV_NOT_FOUND);
	CHECK_OK(psiv_new_game(s));
	CHECK(psiv_field_actors(s, NULL, &count) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_field_actors(s, &actors, NULL) == PSIV_INVALID_ARGUMENT);

	/* At the first line nothing has moved: the dockhand and the guide stand at their talk points. */
	CHECK(psiv_line_pending(s));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	CHECK(count == 2);
	const psiv_actor* dockhand = findActor(actors, count, "dockhand");
	const psiv_actor* guide = findActor(actors, count, "guide");
	CHECK(dockhand && dockhand->x == 10 && dockhand->y == 2 && dockhand->facing == PSIV_DIRECTION_SOUTH && dockhand->visible && !dockhand->scripted);
	CHECK(dockhand && strcmp(dockhand->name, "Dockhand") == 0);
	CHECK(guide && guide->x == 5 && guide->y == 2 && !guide->scripted);

	CHECK_OK(psiv_line_confirm(s));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	dockhand = findActor(actors, count, "dockhand");
	CHECK(dockhand && dockhand->x == 5 && dockhand->y == 4 && dockhand->facing == PSIV_DIRECTION_WEST && dockhand->scripted);
	settle(s);
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	dockhand = findActor(actors, count, "dockhand");
	CHECK(dockhand && dockhand->x == 10 && dockhand->y == 2 && dockhand->facing == PSIV_DIRECTION_SOUTH && dockhand->scripted);

	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_save_manual(s, "manual_0"));
	CHECK_OK(psiv_close_menu(s));

	/* Talking to the guide turns it toward the leader below it. */
	CHECK(walkToMarker(s, "guide_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	CHECK(psiv_line_pending(s));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	guide = findActor(actors, count, "guide");
	CHECK(guide && guide->x == 5 && guide->y == 2 && guide->facing == PSIV_DIRECTION_SOUTH && guide->scripted);

	/* The save has the dockhand's place and not the guide's turn. */
	CHECK_OK(psiv_quit_to_title(s));
	CHECK_OK(psiv_load(s, "manual_0"));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	dockhand = findActor(actors, count, "dockhand");
	guide = findActor(actors, count, "guide");
	CHECK(dockhand && dockhand->scripted && dockhand->x == 10 && dockhand->y == 2);
	CHECK(guide && !guide->scripted);

	/* No actor stands on the reed path until the peddler's talk point is reached. */
	CHECK(walkToMarker(s, "east_gate") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
	settle(s);
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "reed_path") == 0);
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	CHECK(count == 1 && strcmp(actors[0].id, "peddler") == 0 && actors[0].x == 10 && actors[0].y == 7 && !actors[0].scripted);
	psiv_session_destroy(s);
}

int main(int argc, char** argv) {
	for (int i = 1; i + 1 < argc; i += 2) {
		if (strcmp(argv[i], "--data") == 0) {
			dataDir = argv[i + 1];
		} else if (strcmp(argv[i], "--scratch") == 0) {
			scratch = argv[i + 1];
		}
	}
	makeDirectory(scratch);
	testVersion();
	testCreateErrors();
	testRefusals();
	testStringOwnership();
	testSaveAndLoad();
	testBattleStream();
	testPresentation();
	testFieldAndMenus();
	testAdventurePaths();
	testActors();
	if (failures) {
		fprintf(stderr, "%d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("c_tests: %d checks passed\n", checks);
	return 0;
}
