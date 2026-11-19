#include "check.h"

using events::ActionType;
using events::Interpreter;
using events::Program;
using events::RecordingHost;
using events::SaveState;
using events::Status;
using events::WaitKind;

namespace {

// A line, a race, an answered choice whose branch gives before its line, a
// nested call, a battle and background presentation: everything a skip has
// to pass through or stop at.
const char* const skippable_scene = R"(event scene skippable
	lock player
	music play theme
	together
		move guide to pier
		sequence
			anim traveler wave
			face traveler guide
		end
	end
	say guide "Hello again."
	choice "Which way?" into scene.way
	option "Coast"
		give rope
		say guide "The coast it is."
		var visits += 1
	option "Hills"
		say guide "Up we go."
		set scene.hills
	end
	call helper
	move traveler to dock nowait
	wait 0.5
	battle reef_crabs
	win
		party add guide
	flee
		say guide "Run!"
		take rope
	end
	panel show p1 camera dock_wide layout full
	say guide "Off we go."
	unlock player
end
event helper unskippable
	wait 0.4
	say guide "A short aside."
	var visits += 2
	race
		camera move gull_cam 3.0
		wait 0.6
	end
end
)";

void configure(RecordingHost& host) {
	host.set_duration(ActionType::move, 0.4);
	host.set_duration(ActionType::animate, 0.7);
	host.set_line_duration(0.3);
	host.set_choice_duration(0.2);
	host.set_battle_duration(0.5);
	host.vars["visits"] = 2;
}

bool contains(const std::vector<std::string>& trace, const std::string& line) {
	for (const std::string& l : trace) {
		if (l == line) {
			return true;
		}
	}
	return false;
}

bool starts_any(const std::vector<std::string>& trace, const std::string& prefix) {
	for (const std::string& l : trace) {
		if (l.rfind(prefix, 0) == 0) {
			return true;
		}
	}
	return false;
}

struct Answers {
	int way = 0;
	events::BattleResult battle = events::BattleResult::win;
};

void answer(RecordingHost& host, const Answers& a) {
	host.queue_choices({a.way});
	host.queue_battle_results({a.battle});
}

}

TEST(skipping_at_any_step_on_every_path_leaves_the_world_of_watching) {
	Program p = test::compile_ok(skippable_scene);
	int skips = 0;
	for (int way = 0; way < 2; ++way) {
		for (events::BattleResult battle : {events::BattleResult::win, events::BattleResult::flee}) {
			const Answers answers{way, battle};
			RecordingHost watched;
			configure(watched);
			answer(watched, answers);
			Interpreter full(p, watched);
			full.start("scene");
			const int total = test::run(full, watched);
			CHECK(full.status() == Status::finished);
			CHECK(total > 15);

			// Skip once at step k, then again at every chance after each stop.
			for (int k = 0; k < total; ++k) {
				RecordingHost host;
				configure(host);
				answer(host, answers);
				Interpreter it(p, host);
				it.start("scene");
				for (int i = 0; i < k; ++i) {
					host.advance(0.1);
					it.step(0.1);
				}
				for (int guard = 0; it.status() == Status::running && guard < 1000; ++guard) {
					if (it.can_skip()) {
						CHECK(it.skip());
						CHECK(!it.can_skip());
						++skips;
					}
					host.advance(0.1);
					it.step(0.1);
				}
				CHECK(it.status() == Status::finished);
				CHECK(host.same_world(watched));
				CHECK(it.presentation() == full.presentation());
				CHECK_EQ(host.live_handles(), 0u);
				CHECK(host.errors.empty());
				// The decisions were still put to the player.
				CHECK(starts_any(host.trace, "choice "));
				CHECK(starts_any(host.trace, "battle "));
			}
		}
	}
	CHECK(skips > 100);
}

TEST(skip_stops_at_the_next_choice_and_battle_and_is_offered_again_after_each) {
	Program p = test::compile_ok(skippable_scene);
	RecordingHost host;
	configure(host);
	host.set_choice_duration(1);
	host.set_battle_duration(1);
	answer(host, {0, events::BattleResult::win});
	Interpreter it(p, host);
	it.start("scene");
	it.step(0.1);
	CHECK(it.skip());
	host.advance(0.1);
	it.step(0.1);
	// The skip stopped at the choice: it is on screen, and nothing after it ran.
	CHECK(!it.skipping());
	CHECK(starts_any(host.trace, "choice "));
	CHECK(!contains(host.trace, "give rope 1"));
	CHECK(!it.can_skip());
	for (int i = 0; i < 12 && !it.can_skip(); ++i) {
		host.advance(0.1);
		it.step(0.1);
	}
	CHECK(it.can_skip());
	CHECK(it.skip());
	host.advance(0.1);
	it.step(0.1);
	// Through the branch and the unskippable helper, up to the battle.
	CHECK(contains(host.trace, "give rope 1"));
	CHECK(contains(host.trace, "skipped say guide \"A short aside.\""));
	CHECK(starts_any(host.trace, "battle "));
	CHECK(!it.can_skip());
	CHECK_EQ(host.vars["visits"], 5);
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK(host.errors.empty());
}

TEST(a_choice_writes_its_answer_before_its_branch_runs) {
	Program p = test::compile_ok(R"(event a skippable
	choice "Pick." into pick.way
	option "One"
		say guide "One."
	option "Two"
		say guide "Two."
	end
	ask "Sure?" into pick.sure
	yes
		say guide "Good."
	end
end
)");
	RecordingHost host;
	host.set_line_duration(1);
	host.queue_choices({1, 1});
	Interpreter it(p, host);
	it.start("a");
	it.step(0.1);
	// The answer lands in the step that takes the branch, before its first line.
	CHECK_EQ(host.vars["pick.way"], 2);
	CHECK_EQ(host.trace.back(), std::string("say guide \"Two.\""));
	test::run(it, host);
	CHECK(host.flags.count("pick.sure") == 1 && !host.flags["pick.sure"]);
}

TEST(skip_is_refused_for_unskippable_events_and_while_a_decision_is_open) {
	Program p = test::compile_ok(R"(event locked unskippable
	say guide "Listen closely."
	call aside
	say guide "Done."
end
event aside skippable
	ask "Ready?"
	end
	wait 1
end
)");
	RecordingHost host;
	host.set_line_duration(1);
	host.set_choice_duration(1);
	Interpreter it(p, host);
	CHECK(!it.can_skip());
	it.start("locked");
	it.step(0.1);
	CHECK(!it.skip());
	for (int i = 0; i < 15 && !starts_any(host.trace, "ask "); ++i) {
		host.advance(0.1);
		it.step(0.1);
	}
	CHECK(starts_any(host.trace, "ask "));
	CHECK(!it.can_skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK(!contains(host.trace, "cancel say guide \"Listen closely.\""));
}

TEST(skipping_a_skippable_callee_resumes_the_unskippable_caller) {
	Program p = test::compile_ok(R"(event outer unskippable
	move guide to far nowait
	call intro
	say guide "After the intro."
end
event intro skippable
	say guide "Intro line."
	music play intro_theme nowait
	wait 5
	say guide "Intro tail."
	set intro.done
end
)");
	RecordingHost host;
	host.set_duration(ActionType::move, 2);
	host.set_duration(ActionType::music_play, 10);
	Interpreter it(p, host);
	it.start("outer");
	it.step(0.1);
	CHECK(it.can_skip());
	CHECK(it.skip());
	host.advance(0.1);
	it.step(0.1);
	CHECK(!it.skipping());
	CHECK(host.flags["intro.done"]);
	CHECK(contains(host.trace, "skipped say guide \"Intro tail.\""));
	CHECK(!contains(host.trace, "say guide \"Intro tail.\""));
	CHECK(contains(host.trace, "say guide \"After the intro.\""));
	// The skip ends the intro's music at once, not the caller's walk.
	CHECK(!contains(host.trace, "cancel move guide to far"));
	CHECK(host.instant_calls == std::vector<std::string>{"music play intro_theme"});
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK_EQ(host.live_handles(), 0u);
}

TEST(a_skip_inside_nested_skippable_events_fast_forwards_the_whole_chain) {
	Program p = test::compile_ok(R"(event root skippable
	call child
	say guide "Root tail."
	set root.played
end
event child skippable
	wait 3
	set child.played
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("root");
	it.step(0.1);
	CHECK(it.skip());
	it.step(0.1);
	CHECK(it.status() == Status::finished);
	CHECK(host.flags["child.played"]);
	CHECK(host.flags["root.played"]);
}

TEST(skip_completes_running_branches_with_their_end_state) {
	Program p = test::compile_ok(R"(event a skippable
	music play theme
	together
		move guide to pier
		sequence
			anim traveler wave
			face traveler guide
			anim traveler sit loop
		end
	end
	say guide "Done."
	shake 1.0
	sound thud
	fade out 1.0
end
)");
	RecordingHost host;
	host.set_duration(ActionType::move, 1);
	host.set_duration(ActionType::animate, 1);
	Interpreter it(p, host);
	it.start("a");
	it.step(0.1);
	CHECK(it.skip());
	it.step(0.1);
	CHECK(it.status() == Status::finished);
	CHECK(contains(host.trace, "cancel move guide to pier"));
	CHECK(contains(host.trace, "cancel anim traveler wave"));
	// What is left on screen is rebuilt at once; effects with no end state are dropped.
	std::vector<std::string> expected = {"move guide to pier", "face traveler guide", "anim traveler sit loop", "fade out 0"};
	CHECK(host.instant_calls == expected);
	CHECK(!starts_any(host.trace, "action shake"));
	CHECK(!starts_any(host.trace, "action sound"));
	CHECK_EQ(host.live_handles(), 0u);
}

TEST(a_warp_still_happens_while_skipping_and_a_save_during_it_keeps_skipping) {
	Program p = test::compile_ok(R"(event a skippable
	move guide to pier
	say guide "Let us go."
	warp lighthouse door
	show keeper
	say keeper "Welcome."
	set arrived
end
)");
	RecordingHost played;
	played.set_duration(ActionType::warp, 1);
	Interpreter full(p, played);
	full.start("a");
	test::run(full, played);

	RecordingHost host;
	host.set_duration(ActionType::warp, 1);
	host.set_duration(ActionType::move, 1);
	Interpreter it(p, host);
	it.start("a");
	it.step(0.1);
	CHECK(it.skip());
	host.advance(0.1);
	it.step(0.1);
	// Waiting on the map load, still skipping.
	CHECK(it.skipping());
	CHECK(contains(host.trace, "action warp lighthouse door"));
	SaveState loaded;
	CHECK(events::save_state_from_text(events::save_state_to_text(it.save()), loaded));
	CHECK_EQ(loaded.skip_depth, 0);
	// The guide belonged to the old map, so nothing rebuilds it.
	CHECK(loaded.presentation.actors.empty());

	RecordingHost resumed_host = host.reloaded();
	Interpreter resumed(p, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	CHECK(!resumed.can_skip());
	test::run(resumed, resumed_host);
	CHECK(resumed.status() == Status::finished);
	CHECK(resumed_host.same_world(played));
	CHECK(!contains(resumed_host.trace, "say keeper \"Welcome.\""));
	CHECK(resumed_host.instant_calls == (std::vector<std::string>{"warp lighthouse door", "show keeper"}));
}

namespace {

const char* const save_scene = R"(event scene unskippable
	say guide "One."
	call helper
	together
		move guide to pier
		sequence
			wait 0.4
			sound bell
		end
	end
	move traveler to dock nowait
	choice "Pick a path."
	option "Left"
		say guide "Left it is."
	option "Right"
		say guide "Right it is."
	end
	say guide "The end."
end
event helper unskippable
	wait 0.35
	say guide "Helper."
	var count += 1
end
)";

void configure_save(RecordingHost& host) {
	host.set_duration(ActionType::move, 0.3);
	host.set_line_duration(0.2);
	host.queue_choices({1});
}

struct Run {
	std::vector<std::string> trace;
	std::vector<std::size_t> trace_size_after_step;
	RecordingHost host;
};

Run play_through(const Program& p) {
	Run r;
	configure_save(r.host);
	Interpreter it(p, r.host);
	it.start("scene");
	r.trace_size_after_step.push_back(0);
	while (it.status() == Status::running) {
		r.host.advance(0.1);
		it.step(0.1);
		r.trace_size_after_step.push_back(r.host.trace.size());
	}
	r.trace = r.host.trace;
	return r;
}

// Plays k steps, saves through the text form, then finishes the event in a
// new interpreter with a reloaded host. Returns the trace after the restore.
std::vector<std::string> resume_after(const Program& p, int k, SaveState& saved, RecordingHost& resumed_host) {
	RecordingHost host;
	configure_save(host);
	Interpreter it(p, host);
	it.start("scene");
	for (int i = 0; i < k; ++i) {
		host.advance(0.1);
		it.step(0.1);
	}
	saved = it.save();
	std::string text = events::save_state_to_text(saved);
	SaveState loaded;
	CHECK(events::save_state_from_text(text, loaded));
	CHECK_EQ(events::save_state_to_text(loaded), text);

	resumed_host = host.reloaded();
	Interpreter resumed(p, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	test::run(resumed, resumed_host);
	CHECK(resumed.status() == Status::finished);
	return resumed_host.trace;
}

std::vector<std::string> suffix(const std::vector<std::string>& trace, std::size_t from) {
	return std::vector<std::string>(trace.begin() + static_cast<long>(from), trace.end());
}

}

TEST(restore_during_a_timed_wait_in_a_call_replays_the_identical_remaining_trace) {
	Program p = test::compile_ok(save_scene);
	Run original = play_through(p);
	// "One." shows in step 1 and is confirmed in step 3, which also enters
	// helper's 0.35 s wait; step 4 is in the middle of it.
	const int k = 4;
	SaveState saved;
	RecordingHost host;
	std::vector<std::string> rest = resume_after(p, k, saved, host);
	CHECK(saved.main.wait == WaitKind::timer);
	CHECK_EQ(saved.frames.size(), 2u);
	CHECK(rest == suffix(original.trace, original.trace_size_after_step[k]));
	CHECK(host.same_world(original.host));
	CHECK(host.errors.empty());
}

TEST(restore_during_dialogue_shows_the_line_again_then_matches) {
	Program p = test::compile_ok(save_scene);
	Run original = play_through(p);
	const int k = 1;
	SaveState saved;
	RecordingHost host;
	std::vector<std::string> rest = resume_after(p, k, saved, host);
	CHECK(saved.main.wait == WaitKind::handle);
	std::vector<std::string> expected = {"say guide \"One.\""};
	std::vector<std::string> tail = suffix(original.trace, original.trace_size_after_step[k]);
	expected.insert(expected.end(), tail.begin(), tail.end());
	CHECK(rest == expected);
}

TEST(restore_at_every_step_finishes_with_the_same_world) {
	Program p = test::compile_ok(save_scene);
	Run original = play_through(p);
	int total = static_cast<int>(original.trace_size_after_step.size()) - 1;
	bool saw_children = false;
	bool saw_background = false;
	for (int k = 0; k <= total; ++k) {
		SaveState saved;
		RecordingHost host;
		std::vector<std::string> rest = resume_after(p, k, saved, host);
		saw_children = saw_children || !saved.children.empty();
		saw_background = saw_background || !saved.background.empty();
		CHECK(host.same_world(original.host));
		CHECK_EQ(host.live_handles(), 0u);
		CHECK(host.errors.empty());
		// Whatever restarts after loading, the story beats that remain match.
		std::vector<std::string> tail = suffix(original.trace, original.trace_size_after_step[k]);
		CHECK(rest.size() >= tail.size());
		CHECK(std::vector<std::string>(rest.end() - static_cast<long>(tail.size()), rest.end()) == tail);
	}
	CHECK(saw_children);
	CHECK(saw_background);
}

TEST(restore_rejects_corrupt_saves) {
	Program p = test::compile_ok(save_scene);
	RecordingHost host;
	Interpreter it(p, host);
	it.start("scene");
	it.step(0.1);
	SaveState saved = it.save();

	RecordingHost other_host;
	std::string error;
	SaveState broken = saved;
	broken.main.pc = 100000;
	Interpreter same(p, other_host);
	CHECK(!same.restore(broken, error));
	CHECK(error.find("outside the program") != std::string::npos);

	SaveState parsed;
	CHECK(!events::save_state_from_text("events-save 1 12 1", parsed));
	CHECK(!events::save_state_from_text(events::save_state_to_text(saved) + " extra", parsed));
	CHECK(!events::save_state_from_text("other-format 1", parsed));
}

TEST(fingerprint_ignores_comments_blank_lines_and_indentation) {
	const std::string base = "event a unskippable\n\tsay guide \"Hi.\"\n\twait 1\nend\n";
	const std::string cosmetic = "# A new header comment.\n\nevent a unskippable   # trailing note\n\n    say guide \"Hi.\"\n wait 1\n\n\nend\n";
	const std::string changed = "event a unskippable\n\tsay guide \"Hello.\"\n\twait 1\nend\n";
	const std::string hash_in_text = "event a unskippable\n\tsay guide \"Hi. # not a comment\"\n\twait 1\nend\n";
	Program p = test::compile_ok(base);
	CHECK_EQ(test::compile_ok(cosmetic).fingerprint, p.fingerprint);
	CHECK(test::compile_ok(changed).fingerprint != p.fingerprint);
	CHECK(test::compile_ok(hash_in_text).fingerprint != p.fingerprint);
	events::CompileResult renamed = events::compile({{"other.ev", base}});
	CHECK(renamed.program.fingerprint != p.fingerprint);
}

TEST(restore_after_a_comment_only_edit_resumes_in_place) {
	Program p = test::compile_ok(save_scene);
	Run original = play_through(p);
	const int k = 4;
	RecordingHost host;
	configure_save(host);
	Interpreter it(p, host);
	it.start("scene");
	for (int i = 0; i < k; ++i) {
		host.advance(0.1);
		it.step(0.1);
	}
	SaveState saved = it.save();

	Program edited = test::compile_ok("# Reworded notes only.\n\n" + std::string(save_scene) + "\n# edited\n");
	RecordingHost resumed_host = host.reloaded();
	Interpreter resumed(edited, resumed_host);
	std::string error;
	CHECK(resumed.restore(saved, error));
	CHECK(!resumed.restarted());
	test::run(resumed, resumed_host);
	CHECK(resumed_host.trace == suffix(original.trace, original.trace_size_after_step[k]));
}

namespace {

const char* const reward_scene = R"(event reward unskippable
	give shell 2
	say guide "Two shells for you."
	var tides += 1
	say guide "And a third."
	give shell
end
)";

const char* const reward_scene_v2 = R"(event reward unskippable
	give shell 2
	say guide "Two shells for you, traveler."
	var tides += 1
	say guide "And a third."
	give shell
	set reward.reworded
end
)";

}

TEST(restore_after_a_script_change_restarts_the_event_from_its_start_state) {
	Program p = test::compile_ok(reward_scene);
	RecordingHost host;
	host.items["shell"] = 1;
	host.vars["tides"] = 5;
	host.set_line_duration(0.3);
	Interpreter it(p, host);
	it.start("reward");
	// Into the second line: the first give and the += have been applied.
	for (int i = 0; i < 5; ++i) {
		host.advance(0.1);
		it.step(0.1);
	}
	CHECK_EQ(host.items["shell"], 3);
	CHECK_EQ(host.vars["tides"], 6);
	SaveState loaded;
	CHECK(events::save_state_from_text(events::save_state_to_text(it.save()), loaded));
	CHECK_EQ(loaded.event, std::string("reward"));

	Program changed = test::compile_ok(reward_scene_v2);
	CHECK(changed.fingerprint != p.fingerprint);
	RecordingHost resumed_host = host.reloaded();
	Interpreter resumed(changed, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	CHECK(resumed.restarted());
	// The world is back to how it was when the event started.
	CHECK_EQ(resumed_host.items["shell"], 1);
	CHECK_EQ(resumed_host.vars["tides"], 5);
	test::run(resumed, resumed_host);
	CHECK(resumed.status() == Status::finished);
	CHECK_EQ(resumed_host.items["shell"], 4);
	CHECK_EQ(resumed_host.vars["tides"], 6);
	CHECK(resumed_host.flags["reward.reworded"]);
	CHECK(contains(resumed_host.trace, "say guide \"Two shells for you, traveler.\""));

	// A restarted event saves its own start state again.
	SaveState again = resumed.save();
	CHECK_EQ(again.start_world, loaded.start_world);
}

TEST(restore_after_a_script_change_fails_when_the_event_is_gone) {
	Program p = test::compile_ok(reward_scene);
	RecordingHost host;
	host.set_line_duration(1);
	Interpreter it(p, host);
	it.start("reward");
	it.step(0.1);
	SaveState saved = it.save();
	CHECK(saved.status == Status::running);
	Program other = test::compile_ok("event something_else unskippable\n\twait 1\nend\n");
	RecordingHost other_host = host.reloaded();
	Interpreter restored(other, other_host);
	std::string error;
	CHECK(!restored.restore(saved, error));
	CHECK(error.find("'reward' no longer exists") != std::string::npos);
}

TEST(a_restore_rebuilds_what_the_event_put_on_screen_before_anything_else) {
	Program p = test::compile_ok(R"(event a unskippable
	fade out 0
	music play theme
	ambient play surf
	camera cut wide
	show guide
	move guide to pier
	face guide traveler
	anim guide idle_sway loop
	hide vendor
	move traveler to 1.5 0 -2
	panel show p1 camera wide layout left
	panel show p2 camera close layout right
	panel show p1 camera close_up layout left
	sound bell
	say guide "Hold still."
	set done
end
)");
	RecordingHost host;
	host.set_line_duration(1);
	Interpreter it(p, host);
	it.start("a");
	it.step(0.1);
	const std::string text = events::save_state_to_text(it.save());
	SaveState loaded;
	CHECK(events::save_state_from_text(text, loaded));
	CHECK(loaded.presentation == it.presentation());
	CHECK_EQ(loaded.presentation.panels.size(), 2u);

	RecordingHost resumed_host = host.reloaded();
	Interpreter resumed(p, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	CHECK(resumed_host.trace.empty());
	resumed.step(0.1);
	std::vector<std::string> expected = {
		"action music play theme",
		"action ambient play surf",
		"action camera cut wide",
		"action show guide",
		"action move guide to pier",
		"action face guide traveler",
		"action anim guide idle_sway loop",
		"action move traveler to 1.5 0 -2",
		"action hide vendor",
		"action panel show p1 camera close_up layout left",
		"action panel show p2 camera close layout right",
		"action fade out 0",
		"say guide \"Hold still.\"",
	};
	CHECK(resumed_host.trace == expected);
	if (resumed_host.trace != expected) {
		std::printf("%s", test::join(resumed_host.trace).c_str());
	}
	CHECK_EQ(resumed_host.instant_calls.size(), expected.size() - 1);
	test::run(resumed, resumed_host);
	CHECK(resumed.status() == Status::finished);
	CHECK_EQ(resumed_host.live_handles(), 0u);
}

TEST(a_damaged_save_text_is_rejected) {
	Program p = test::compile_ok(save_scene);
	RecordingHost host;
	configure_save(host);
	Interpreter it(p, host);
	it.start("scene");
	for (int i = 0; i < 6; ++i) {
		host.advance(0.1);
		it.step(0.1);
	}
	const std::string text = events::save_state_to_text(it.save());
	SaveState parsed;
	CHECK(events::save_state_from_text(text, parsed));
	int accepted = 0;
	for (std::size_t at = 0; at < text.size(); ++at) {
		std::string flipped = text;
		flipped[at] = static_cast<char>(flipped[at] ^ 0x04);
		accepted += events::save_state_from_text(flipped, parsed) ? 1 : 0;
	}
	for (std::size_t length = 0; length < text.size(); ++length) {
		accepted += events::save_state_from_text(text.substr(0, length), parsed) ? 1 : 0;
	}
	CHECK_EQ(accepted, 0);
	// Another version is rejected even with a checksum that matches.
	CHECK(text.rfind("events-save 3 ", 0) == 0);
	auto checksum = [](const std::string& body) {
		std::uint64_t hash = 14695981039346656037ull;
		for (unsigned char c : body) {
			hash ^= c;
			hash *= 1099511628211ull;
		}
		char sum[20];
		std::snprintf(sum, sizeof sum, "%016llx", static_cast<unsigned long long>(hash));
		return std::string(sum);
	};
	const std::size_t mark = text.rfind(" sum ");
	CHECK_EQ(checksum(text.substr(0, mark)), text.substr(mark + 5));
	const std::string older = "events-save 2" + text.substr(13, mark - 13);
	CHECK(!events::save_state_from_text(older + " sum " + checksum(older), parsed));
}
