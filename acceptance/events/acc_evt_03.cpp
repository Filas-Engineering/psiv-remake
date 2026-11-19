#include "support.h"

using namespace events;

namespace {

std::vector<std::string> traceOf(const Program& program, const support::Path& path, double step) {
	support::DecisionHost host;
	support::configure(host);
	support::seed(host, path.world);
	host.picks = path.picks;
	Interpreter interpreter(program, host);
	interpreter.start(path.event);
	while (interpreter.status() == Status::running && host.trace.size() < 100000) {
		host.advance(step);
		interpreter.step(step);
	}
	return host.trace;
}

}

ACC_CHECK(acc_evt_03, "ACC-EVT-03", "EVT-021") {
	const Program program = support::compileSamples(t);
	const std::vector<support::Path> paths = support::allPaths(program, {{}, {"harbor.met_guide"}});
	for (const support::Path& path : paths) {
		const std::vector<std::string> first = traceOf(program, path, support::kStep);
		t.expect(!first.empty(), support::describe(path) + ": no host calls");
		t.expect(first == traceOf(program, path, support::kStep), support::describe(path) + ": a second run issued a different host call log");
		// Polling calls are not logged, so the log is also independent of the step size.
		t.expect(first == traceOf(program, path, 0.05), support::describe(path) + ": the host call log depends on the step size");
	}
	t.note(std::to_string(paths.size()) + " script paths (every choice option and allowed battle result, two starting worlds) give identical host call logs on repeat runs and at two step sizes");
}
