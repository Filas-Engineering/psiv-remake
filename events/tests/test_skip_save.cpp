#include "check.h"

using events::ActionType;
using events::Interpreter;
using events::Program;
using events::RecordingHost;
using events::SaveState;
using events::Status;
using events::WaitKind;

namespace {

const char* const skippable_scene = R"(event scene skippable
	lock player
	music play theme
	together
		move guide to pier
		anim traveler wave
	end
	say guide "Hello again."
	if not got_rope
		give rope
		set got_rope
	end
	move traveler to dock nowait
	wait 0.5
	party add guide
	if not scene.visit_counted
		var visits += 1
		set scene.visit_counted
	end
	say guide "Off we go."
	unlock player
on skip
	if not got_rope
		give rope
		set got_rope
	end
	party add guide
	if not scene.visit_counted
		var visits += 1
		set scene.visit_counted
	end
	unlock player
end
)";

void configure(RecordingHost& host) {
	host.set_duration(ActionType::move, 0.4);
	host.set_duration(ActionType::animate, 0.7);
	host.set_line_duration(0.3);
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

}

TEST(skipping_at_any_step_leaves_the_same_world_as_playing_through) {
	Program p = test::compile_ok(skippable_scene);
	RecordingHost played;
	configure(played);
	Interpreter full(p, played);
	full.start("scene");
	int total = test::run(full, played);
	CHECK(full.status() == Status::finished);
	CHECK_EQ(played.vars["visits"], 3);
	CHECK(played.player_control);
	CHECK(total > 10);

	bool saw_cancel_in_together = false;
	for (int k = 0; k < total; ++k) {
		RecordingHost host;
		configure(host);
		Interpreter it(p, host);
		it.start("scene");
		for (int i = 0; i < k; ++i) {
			host.advance(0.1);
			it.step(0.1);
		}
		CHECK(it.skip());
		CHECK(!it.can_skip());
		test::run(it, host);
		CHECK(it.status() == Status::finished);
		CHECK(host.same_world(played));
		CHECK_EQ(host.live_handles(), 0u);
		CHECK(host.errors.empty());
		saw_cancel_in_together = saw_cancel_in_together || (contains(host.trace, "cancel move guide to pier") && contains(host.trace, "cancel anim traveler wave"));
	}
	CHECK(saw_cancel_in_together);
}

TEST(skip_is_refused_for_unskippable_events) {
	Program p = test::compile_ok(R"(event locked unskippable
	say guide "Listen closely."
	call aside
	say guide "Done."
end
event aside skippable
	say guide "A short aside."
	wait 1
end
)");
	RecordingHost host;
	host.set_line_duration(1);
	Interpreter it(p, host);
	CHECK(!it.can_skip());
	it.start("locked");
	it.step(0.1);
	CHECK(!it.skip());
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
on skip
	set intro.skipped
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
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK(host.flags["intro.skipped"]);
	CHECK(contains(host.trace, "say guide \"After the intro.\""));
	CHECK(!contains(host.trace, "say guide \"Intro tail.\""));
	// The skip cancels what the intro started, not the caller's walk.
	CHECK(contains(host.trace, "cancel music play intro_theme"));
	CHECK(!contains(host.trace, "cancel move guide to far"));
	CHECK_EQ(host.live_handles(), 0u);
}

TEST(skip_inside_nested_skippable_events_runs_the_outermost_on_skip) {
	Program p = test::compile_ok(R"(event root skippable
	call child
	set root.played
on skip
	set root.skipped
end
event child skippable
	wait 3
on skip
	set child.skipped
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("root");
	it.step(0.1);
	CHECK(it.skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK(host.flags.count("root.skipped") == 1);
	CHECK(host.flags.count("child.skipped") == 0);
	CHECK(host.flags.count("root.played") == 0);
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

namespace {

// The `on skip` gives and adds without any guard. The played part gives the
// shells before it sets anything a guard could test.
const char* const unguarded_scene = R"(event haul skippable
	lock player
	give shell 2
	say guide "Take these."
	call helper
	var tides += 3
	say guide "Mind the current."
	take coin
	give shell
	unlock player
on skip
	give shell 3
	give net
	var tides += 3
	take coin
	unlock player
end
event helper unskippable
	give net
	wait 0.4
end
)";

void configure_haul(RecordingHost& host) {
	host.items["coin"] = 4;
	host.items["shell"] = 1;
	host.vars["tides"] = 10;
	host.set_line_duration(0.3);
}

}

TEST(on_skip_never_reapplies_what_the_played_part_already_gave_took_or_added) {
	Program p = test::compile_ok(unguarded_scene);
	RecordingHost played;
	configure_haul(played);
	Interpreter full(p, played);
	full.start("haul");
	int total = test::run(full, played);
	CHECK(full.status() == Status::finished);
	CHECK_EQ(played.items["shell"], 4);
	CHECK_EQ(played.items["net"], 1);
	CHECK_EQ(played.items["coin"], 3);
	CHECK_EQ(played.vars["tides"], 13);

	for (int k = 0; k < total; ++k) {
		RecordingHost host;
		configure_haul(host);
		Interpreter it(p, host);
		it.start("haul");
		for (int i = 0; i < k; ++i) {
			host.advance(0.1);
			it.step(0.1);
		}
		if (!it.skip()) {
			continue;
		}
		test::run(it, host);
		CHECK(it.status() == Status::finished);
		CHECK(host.same_world(played));
		CHECK(host.errors.empty());
	}
}

TEST(a_save_inside_on_skip_keeps_what_is_left_to_apply) {
	Program p = test::compile_ok(R"(event haul skippable
	give shell 2
	say guide "Take these."
	give shell
on skip
	wait 0.5
	give shell 3
end
)");
	RecordingHost played;
	played.set_line_duration(0.3);
	Interpreter full(p, played);
	full.start("haul");
	test::run(full, played);

	RecordingHost host;
	host.set_line_duration(0.3);
	Interpreter it(p, host);
	it.start("haul");
	host.advance(0.1);
	it.step(0.1);
	CHECK(it.skip());
	host.advance(0.1);
	it.step(0.1);
	SaveState loaded;
	CHECK(events::save_state_from_text(events::save_state_to_text(it.save()), loaded));
	CHECK_EQ(loaded.skip_credit.size(), 1u);

	RecordingHost resumed_host = host.reloaded();
	Interpreter resumed(p, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	CHECK(!resumed.can_skip());
	test::run(resumed, resumed_host);
	CHECK(resumed.status() == Status::finished);
	CHECK_EQ(resumed_host.items["shell"], 3);
	CHECK(resumed_host.same_world(played));
}

TEST(a_nested_skip_counts_toward_an_outer_skippable_event) {
	// outer (skippable) calls middle (unskippable) calls inner (skippable).
	// Skipping inner applies its rest; skipping outer later must count both
	// the played gives and what inner's skip applied.
	Program p = test::compile_ok(R"(event outer skippable
	call middle
	say guide "Back outside."
	give shell
on skip
	give shell 4
end
event middle unskippable
	call inner
	say guide "In the middle."
end
event inner skippable
	give shell
	say guide "Inside."
	give shell 2
on skip
	give shell 3
end
)");
	RecordingHost host;
	host.set_line_duration(0.3);
	Interpreter it(p, host);
	it.start("outer");
	host.advance(0.1);
	it.step(0.1);
	CHECK_EQ(host.items["shell"], 1);
	CHECK(it.skip());
	host.advance(0.1);
	it.step(0.1);
	CHECK_EQ(host.items["shell"], 3);
	// Now in middle's line, which is unskippable.
	CHECK(!it.can_skip());
	for (int i = 0; i < 5 && !it.can_skip(); ++i) {
		host.advance(0.1);
		it.step(0.1);
	}
	CHECK(it.can_skip());
	CHECK(it.skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK_EQ(host.items["shell"], 4);
}
