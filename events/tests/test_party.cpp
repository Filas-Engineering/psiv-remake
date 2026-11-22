#include "check.h"

#include "events/registry.h"

#include <string>
#include <vector>

using events::Interpreter;
using events::Op;
using events::Program;
using events::RecordingHost;
using events::Status;
using events::SwapEquipment;

namespace {

std::vector<std::string> party_calls(const std::vector<std::string>& trace) {
	std::vector<std::string> out;
	for (const std::string& line : trace) {
		if (line.rfind("party ", 0) == 0) {
			out.push_back(line);
		}
	}
	return out;
}

const char* const split_scene = R"(event split skippable
	party add guide
	party add warden
	party save
	party save trio
	say guide "We part here."
	party swap 2 drifter
	party swap guide scribe equipment bag
	say drifter "For now."
	party restore trio
	party restore
end
)";

}

TEST(party_snapshot_and_swap_commands_compile) {
	Program p = test::compile_ok(split_scene);
	CHECK(p.code[2].op == Op::party_save && p.code[2].name == "default");
	CHECK(p.code[3].op == Op::party_save && p.code[3].name == "trio");
	const events::PartySwap& by_slot = p.code[5].swap;
	CHECK(p.code[5].op == Op::party_swap && by_slot.slot == 2 && by_slot.replaced.empty() && by_slot.member == "drifter" && by_slot.equipment == SwapEquipment::rule);
	const events::PartySwap& by_name = p.code[6].swap;
	CHECK(by_name.slot == 0 && by_name.replaced == "guide" && by_name.member == "scribe" && by_name.equipment == SwapEquipment::bag);
	CHECK(events::describe(by_name) == "party swap guide scribe equipment bag");
	CHECK(p.code[8].op == Op::party_restore && p.code[8].name == "trio");
	CHECK(p.code[9].op == Op::party_restore && p.code[9].name == "default");
	// Scripts without the new commands keep their fingerprint: only tokens count.
	CHECK_EQ(test::compile_ok("event a unskippable\n\tparty add guide\nend\n").fingerprint,
		test::compile_ok("event a unskippable\n\tparty   add guide # joins\nend\n").fingerprint);
}

TEST(party_command_mistakes_are_reported) {
	auto d = test::compile_errors(R"(event a unskippable
	party swap 0 drifter
	party swap 1
	party swap guide drifter equipment spare
	party save trio extra
	together
		party save
		wait 1
	end
	party restore "trio"
end
)");
	CHECK(test::has_diagnostic(d, 2, "the lineup slot must be at least 1"));
	CHECK(test::has_diagnostic(d, 3, "expected a party member name"));
	CHECK(test::has_diagnostic(d, 4, "expected 'keep' or 'bag'"));
	CHECK(test::has_diagnostic(d, 5, "unexpected name 'extra'"));
	CHECK(test::has_diagnostic(d, 7, "'party' is not allowed inside 'together'"));
	CHECK(test::has_diagnostic(d, 10, "expected a party snapshot name"));
	CHECK_EQ(d.size(), 6u);

	events::Registry r;
	r.add(events::NameKind::member, "guide");
	events::CompileOptions options;
	options.registry = &r;
	auto unknown = events::compile({{"t.ev", "event a unskippable\n\tparty swap guide stranger\n\tparty swap ghost guide\n\tparty save anyone\nend\n"}}, options).diagnostics;
	CHECK(test::has_diagnostic(unknown, 2, "unknown party member 'stranger'"));
	CHECK(test::has_diagnostic(unknown, 3, "unknown party member 'ghost'"));
	CHECK_EQ(unknown.size(), 2u);
}

TEST(party_snapshots_restore_and_apply_while_skipping) {
	Program p = test::compile_ok(split_scene);
	RecordingHost watched;
	Interpreter full(p, watched);
	full.start("split");
	test::run(full, watched);
	CHECK(full.status() == Status::finished);
	const std::vector<std::string> expected = {
		"party add guide",
		"party add warden",
		"party save default",
		"party save trio",
		"party swap 2 drifter",
		"party swap guide scribe equipment bag",
		"party restore trio -> true",
		"party restore default -> true",
	};
	CHECK(party_calls(watched.trace) == expected);
	CHECK(watched.party == std::vector<std::string>({"guide", "warden"}));
	CHECK(watched.snapshot_world().find("snapshot trio guide warden\n") != std::string::npos);

	RecordingHost skipped;
	Interpreter it(p, skipped);
	it.start("split");
	CHECK(it.skip());
	test::run(it, skipped);
	CHECK(it.status() == Status::finished);
	CHECK(party_calls(skipped.trace) == expected);
	CHECK(skipped.same_world(watched));

	// The snapshots travel with the event-start world a save keeps.
	RecordingHost restored;
	CHECK(restored.restore_world(watched.snapshot_world()));
	CHECK(restored.party_snapshots == watched.party_snapshots);
}

TEST(restoring_an_unknown_party_snapshot_is_a_runtime_error) {
	Program p = test::compile_ok("event a skippable\n\tparty add guide\n\tparty restore before_split\n\tgive lamp\nend\n");
	for (const bool skipping : {false, true}) {
		RecordingHost host;
		Interpreter it(p, host);
		it.start("a");
		if (skipping) {
			CHECK(it.skip());
		}
		test::run(it, host);
		CHECK(it.status() == Status::error);
		CHECK_EQ(it.error(), std::string("test.ev:3: there is no party snapshot 'before_split'; 'party save before_split' makes one"));
		CHECK(host.items.empty());
	}
}

TEST(party_order_and_silent_changes_compile_and_reach_the_host) {
	Program p = test::compile_ok(R"(event regroup skippable
	party add guide silent
	party add warden
	party add scribe
	party order scribe guide
	party swap 3 drifter equipment keep silent
	party remove guide silent
	say drifter "Onward."
end
)");
	CHECK(p.code[0].op == Op::party_add && p.code[0].number == 1);
	CHECK(p.code[1].op == Op::party_add && p.code[1].number == 0);
	CHECK(p.code[3].op == Op::party_order && p.code[3].names == std::vector<std::string>({"scribe", "guide"}));
	CHECK(p.code[4].swap.silent && p.code[4].swap.equipment == SwapEquipment::keep);
	CHECK(events::describe(p.code[4].swap) == "party swap 3 drifter equipment keep silent");
	CHECK(p.code[5].op == Op::party_remove && p.code[5].number == 1);
	const std::vector<std::string> expected = {
		"party add guide silent",
		"party add warden",
		"party add scribe",
		"party order scribe guide -> true",
		"party swap 3 drifter equipment keep silent",
		"party remove guide silent",
	};
	RecordingHost watched;
	Interpreter full(p, watched);
	full.start("regroup");
	test::run(full, watched);
	CHECK(party_calls(watched.trace) == expected);
	// The others keep their order after the named ones: scribe, guide, then warden; drifter takes slot 3.
	CHECK(watched.party == std::vector<std::string>({"scribe", "drifter"}));

	RecordingHost skipped;
	Interpreter it(p, skipped);
	it.start("regroup");
	CHECK(it.skip());
	test::run(it, skipped);
	CHECK(party_calls(skipped.trace) == expected);
	CHECK(skipped.same_world(watched));
	// Scripts without the new words keep their fingerprint.
	CHECK_EQ(test::compile_ok("event a unskippable\n\tparty add guide\n\tparty swap 1 scribe\nend\n").fingerprint,
		test::compile_ok("event a unskippable\n\tparty  add guide\n\tparty swap 1 scribe # here\nend\n").fingerprint);
}

TEST(party_order_mistakes_are_reported_and_an_absent_member_stops_the_script) {
	auto d = test::compile_errors("event a unskippable\n\tparty order\n\tparty order guide guide\n\tparty add guide loudly\n\tparty order guide 2\nend\n");
	CHECK(test::has_diagnostic(d, 2, "expected a party member name"));
	CHECK(test::has_diagnostic(d, 3, "'guide' is named twice"));
	CHECK(test::has_diagnostic(d, 4, "unexpected name 'loudly'"));
	CHECK(test::has_diagnostic(d, 5, "expected a party member name"));
	CHECK_EQ(d.size(), 4u);

	events::Registry r;
	r.add(events::NameKind::member, "guide");
	events::CompileOptions options;
	options.registry = &r;
	auto unknown = events::compile({{"t.ev", "event a unskippable\n\tparty order guide stranger\nend\n"}}, options).diagnostics;
	CHECK(test::has_diagnostic(unknown, 2, "unknown party member 'stranger'"));
	CHECK_EQ(unknown.size(), 1u);

	Program p = test::compile_ok("event a skippable\n\tparty add guide\n\tparty order warden guide\n\tgive lamp\nend\n");
	for (const bool skipping : {false, true}) {
		RecordingHost host;
		Interpreter it(p, host);
		it.start("a");
		if (skipping) {
			CHECK(it.skip());
		}
		test::run(it, host);
		CHECK(it.status() == Status::error);
		CHECK_EQ(it.error(), std::string("test.ev:3: 'party order warden guide' names a member who is not in the lineup"));
		CHECK(host.items.empty());
		CHECK(host.party == std::vector<std::string>({"guide"}));
	}
}
