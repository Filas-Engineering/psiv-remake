#include "support.h"

using namespace events;

ACC_CHECK(acc_evt_02, "ACC-EVT-02", "EVT-001, EVT-009") {
	const std::vector<Source> sources = support::sampleSources(t);
	t.expect(!sources.empty(), "no scripts found under events/samples");
	CompileOptions options;
	options.registry = &support::sampleRegistry(t);
	const CompileResult result = compile(sources, options);
	std::string errors;
	for (const Diagnostic& d : result.diagnostics) {
		if (d.severity == Severity::error) {
			errors += format_diagnostic(d) + " ";
		}
	}
	t.expect(result.ok(), "shipped scripts do not compile against the registry: " + errors);
	int warnings = 0;
	for (const Diagnostic& d : result.diagnostics) {
		warnings += d.severity == Severity::error ? 0 : 1;
	}
	t.expect(warnings == 0, "shipped scripts compile with " + std::to_string(warnings) + " warning(s), which the strict gate rejects");

	int battles = 0;
	for (const Instruction& ins : result.program.code) {
		if (ins.op != Op::battle) {
			continue;
		}
		++battles;
		t.expect(ins.targets.size() == 3 && ins.targets[0] >= 0, "battle " + ins.battle.encounter + " has no Victory branch");
		t.expect(ins.battle.escape_allowed == (ins.targets.size() == 3 && ins.targets[2] >= 0), "battle " + ins.battle.encounter + ": escape is allowed without a flee branch, or refused with one");
		t.expect(ins.battle.defeat_handled == (ins.targets.size() == 3 && ins.targets[1] >= 0), "battle " + ins.battle.encounter + ": defeat handling does not match its lose branch");
	}

	// A result the script has no branch for stops the event instead of continuing.
	{
		Program p = compile({{"probe.ev", "event probe unskippable\n\tbattle lamp_room_moths\n\twin\n\t\tset acc.won\n\tend\nend\n"}}).program;
		RecordingHost host;
		host.queue_battle_results({BattleResult::flee});
		Interpreter interpreter(p, host);
		interpreter.start("probe");
		support::run(interpreter, host);
		t.expect(interpreter.status() == Status::error, "a flee result in a battle with no flee branch did not stop the event");
	}

	t.note(std::to_string(sources.size()) + " scripts under events/samples compile against the registry with no warning (no game scripts exist yet); " + std::to_string(battles) +
		" battle(s) all have a Victory branch, escape only with a flee branch, game-over unless a lose branch exists");
}
