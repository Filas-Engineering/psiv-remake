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
	battle tide_wyrm
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
