#include "check.h"

#include "events/registry.h"

#include <string>
#include <vector>

using events::Interpreter;
using events::Op;
using events::Program;
using events::RecordingHost;
using events::Status;

namespace {

// A host that has no credits to play, as one built before the command.
class NoCreditsHost : public RecordingHost {
public:
	events::Handle play_credits(const std::string& credits) override { return events::Host::play_credits(credits); }
};

const char* const finale = R"(event finale skippable
	say guide "That is the end of the road."
	credits wren_end
	if guide_stayed
		end game
	end
	say guide "Never heard."
end
)";

}

TEST(credits_and_end_game_compile_and_end_the_event) {
	Program p = test::compile_ok(finale);
	CHECK(p.code[1].op == Op::credits && p.code[1].name == "wren_end");
	CHECK(p.code[3].op == Op::end_game);
	CHECK_EQ(std::string(events::op_name(Op::end_game)), std::string("end game"));

	RecordingHost host;
	host.flags["guide_stayed"] = true;
	Interpreter it(p, host);
	it.start("finale");
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	const std::vector<std::string> expected = {"say guide \"That is the end of the road.\"", "credits wren_end", "get_flag guide_stayed -> true", "end game"};
	CHECK_EQ(test::join(host.trace), test::join(expected));

	// A skip stops at the credits, which always play; the end comes after them.
	RecordingHost skipped;
	skipped.flags["guide_stayed"] = true;
	Interpreter fast(p, skipped);
	fast.start("finale");
	CHECK(fast.skip());
	test::run(fast, skipped);
	CHECK(fast.status() == Status::finished);
	const std::vector<std::string> passed = {"skipped say guide \"That is the end of the road.\"", "credits wren_end", "get_flag guide_stayed -> true", "end game"};
	CHECK_EQ(test::join(skipped.trace), test::join(passed));

	// Without the flag the `if` block closes on its own `end` and the event goes on.
	RecordingHost on;
	Interpreter more(p, on);
	more.start("finale");
	test::run(more, on);
	CHECK(more.status() == Status::finished);
	CHECK(on.trace.back() == "say guide \"Never heard.\"");

	NoCreditsHost none;
	Interpreter refused(p, none);
	refused.start("finale");
	test::run(refused, none);
	CHECK(refused.status() == Status::error);
	CHECK_EQ(refused.error(), std::string("test.ev:3: the host does not support 'credits': Host::play_credits returned no handle"));

	// Scripts without the new commands keep their fingerprint.
	CHECK_EQ(test::compile_ok("event a unskippable\n\tsay guide \"Hi.\"\nend\n").fingerprint, test::compile_ok("event a unskippable\n\tsay  guide \"Hi.\" # hi\nend\n").fingerprint);
}

TEST(credits_and_end_game_mistakes_are_reported) {
	auto d = test::compile_errors("event a unskippable\n\tcredits\n\tend game now\n\tend games\nend\nevent b background\n\tcredits wren_end\n\tend game\nend\n");
	CHECK(test::has_diagnostic(d, 2, "expected a credits name"));
	CHECK(test::has_diagnostic(d, 3, "unexpected name 'now'"));
	CHECK(test::has_diagnostic(d, 7, "'credits'"));
	CHECK(test::has_diagnostic(d, 8, "'end'"));

	events::Registry r;
	r.add(events::NameKind::credits, "wren_end");
	events::CompileOptions options;
	options.registry = &r;
	auto unknown = events::compile({{"t.ev", "event a unskippable\n\tcredits wren_end\n\tcredits epilogue\nend\n"}}, options).diagnostics;
	CHECK(test::has_diagnostic(unknown, 3, "unknown credits 'epilogue'"));
	CHECK_EQ(unknown.size(), 1u);
}
