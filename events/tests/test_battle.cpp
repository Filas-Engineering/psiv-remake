#include "check.h"

using events::BattleRequest;
using events::BattleResult;
using events::Interpreter;
using events::Program;
using events::RecordingHost;
using events::Status;

namespace {

// Records each request so the test can see the options the script implied.
class BattleHost : public RecordingHost {
public:
	std::vector<BattleRequest> requests;

	events::Handle start_battle(const BattleRequest& request) override {
		requests.push_back(request);
		return RecordingHost::start_battle(request);
	}
};

const char* const battles = R"(event full unskippable
	battle reef_crabs
	win
		set crabs.beaten
	lose
		warp shore landing
	flee
		var crabs.fled += 1
	end
end
event strict unskippable
	battle tide_wyrm opening enemy_first
	win
	end
	say guide "after"
end
)";

}

TEST(battle_options_follow_the_sections_the_script_wrote) {
	Program p = test::compile_ok(battles);
	BattleHost host;
	Interpreter it(p, host);
	it.start("full");
	test::run(it, host);
	it.start("strict");
	test::run(it, host);
	CHECK_EQ(host.requests.size(), 2u);
	CHECK_EQ(host.requests[0].encounter, std::string("reef_crabs"));
	CHECK(host.requests[0].escape_allowed);
	CHECK(host.requests[0].defeat_handled);
	CHECK_EQ(host.requests[1].encounter, std::string("tide_wyrm"));
	CHECK(!host.requests[1].escape_allowed);
	CHECK(!host.requests[1].defeat_handled);
	CHECK(host.requests[0].opening == events::BattleOpening::normal);
	CHECK(host.requests[1].opening == events::BattleOpening::enemy_first);
	CHECK_EQ(events::describe(host.requests[1]), std::string("battle tide_wyrm no-escape defeat-game-over opening enemy_first"));
	CHECK_EQ(host.trace.back(), std::string("say guide \"after\""));
}

TEST(battle_results_the_script_did_not_allow_are_runtime_errors) {
	Program p = test::compile_ok(battles);
	const BattleResult refused[] = {BattleResult::lose, BattleResult::flee};
	for (BattleResult r : refused) {
		RecordingHost host;
		host.queue_battle_results({r});
		Interpreter it(p, host);
		it.start("strict");
		test::run(it, host);
		CHECK(it.status() == Status::error);
		const char* expected = r == BattleResult::lose ? "game-over flow" : "escape was not allowed";
		CHECK(it.error().find(expected) != std::string::npos);
		CHECK_EQ(host.live_handles(), 0u);
		CHECK(host.errors.empty());
	}
}

TEST(battle_needs_a_win_section) {
	auto d = test::compile_errors(R"(event a unskippable
	battle reef_crabs
	lose
		warp shore landing
	end
end
)");
	CHECK(test::has_diagnostic(d, 2, "battle: needs a 'win' section"));
	CHECK_EQ(d.size(), 1u);
}

TEST(a_save_during_a_battle_starts_the_battle_again) {
	Program p = test::compile_ok(battles);
	BattleHost host;
	host.set_battle_duration(5);
	Interpreter it(p, host);
	it.start("full");
	it.step(0.1);
	events::SaveState saved = it.save();
	BattleHost resumed_host;
	static_cast<RecordingHost&>(resumed_host) = host.reloaded();
	resumed_host.queue_battle_results({BattleResult::flee});
	Interpreter resumed(p, resumed_host);
	std::string error;
	CHECK(resumed.restore(saved, error));
	test::run(resumed, resumed_host);
	CHECK(resumed.status() == Status::finished);
	CHECK_EQ(resumed_host.requests.size(), 1u);
	CHECK_EQ(resumed_host.vars["crabs.fled"], 1);
}

namespace {

// Writes its autosave from inside start_battle, as a game would (EVT-050).
class SavingHost : public RecordingHost {
public:
	Interpreter* interpreter = nullptr;
	std::vector<std::string> saves;

	events::Handle start_battle(const BattleRequest& request) override {
		saves.push_back(events::save_state_to_text(interpreter->save()));
		return RecordingHost::start_battle(request);
	}
};

}

TEST(a_save_written_inside_start_battle_restores_to_the_same_battle) {
	Program p = test::compile_ok(R"(event cove skippable
	give rope
	music play alarm
	say guide "Something stirs."
	battle reef_crabs
	win
		set crabs.beaten
	flee
		var crabs.fled += 1
	end
	say guide "Onward."
end
)");
	SavingHost host;
	host.set_line_duration(0.3);
	Interpreter it(p, host);
	host.interpreter = &it;
	it.start("cove");
	// Skipping into the battle: the save sees the stopped skip, not a running one.
	it.step(0.1);
	CHECK(it.skip());
	it.step(0.1);
	CHECK_EQ(host.saves.size(), 1u);

	events::SaveState loaded;
	CHECK(events::save_state_from_text(host.saves[0], loaded));
	CHECK_EQ(loaded.skip_depth, -1);
	CHECK_EQ(loaded.presentation.music, std::string("alarm"));
	RecordingHost resumed_host = host.reloaded();
	resumed_host.queue_battle_results({BattleResult::flee});
	Interpreter resumed(p, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	test::run(resumed, resumed_host);
	CHECK(resumed.status() == Status::finished);
	std::vector<std::string> expected = {"action music play alarm", "battle reef_crabs escape defeat-game-over -> flee", "get_var crabs.fled -> 0",
		"set_var crabs.fled 1", "say guide \"Onward.\""};
	CHECK(resumed_host.trace == expected);
	CHECK_EQ(resumed_host.items["rope"], 1);
	CHECK(resumed_host.errors.empty());
}

TEST(battle_until_sets_a_scripted_end_in_the_request) {
	Program p = test::compile_ok(R"(event rounds unskippable
	battle reef_crabs opening party_first until survive 3
	win
	end
end
event boss unskippable
	battle tide_wyrm until defeat wyrm_head
	win
	end
end
)");
	BattleHost host;
	Interpreter it(p, host);
	it.start("rounds");
	test::run(it, host);
	it.start("boss");
	test::run(it, host);
	CHECK_EQ(host.requests.size(), 2u);
	CHECK(host.requests[0].end == events::BattleEndKind::survive_rounds && host.requests[0].end_rounds == 3);
	CHECK(host.requests[1].end == events::BattleEndKind::enemy_defeated && host.requests[1].end_enemy == "wyrm_head");
	CHECK_EQ(events::describe(host.requests[0]), std::string("battle reef_crabs no-escape defeat-game-over opening party_first until survive 3"));
	CHECK_EQ(events::describe(host.requests[1]), std::string("battle tide_wyrm no-escape defeat-game-over until defeat wyrm_head"));

	std::vector<events::Diagnostic> d = test::compile_errors("event a unskippable\n\tbattle x until survive 0\n\twin\n\tend\n\tbattle x until flee\n\twin\n\tend\nend\n");
	CHECK(test::has_diagnostic(d, 2, "the rounds to survive must be 1 to 999, found 0"));
	CHECK(test::has_diagnostic(d, 5, "expected 'survive <rounds>' or 'defeat <enemy>' after 'until'"));
}
