"""Runs the spec consistency checker and every acceptance check, prints one line per check, writes results.tsv."""

import argparse
import importlib.util
import subprocess
import sys
import traceback
from pathlib import Path

import check_spec
import spec as specmod

PASS, FAIL, NOT_IMPLEMENTED, NOT_AUTOMATABLE = "PASS", "FAIL", "NOT IMPLEMENTED", "NOT AUTOMATABLE"
TIMEOUT = 900


def native_checks(binary):
	result = subprocess.run([str(binary), "--list"], capture_output=True, text=True, check=True)
	checks = {}
	for line in result.stdout.splitlines():
		identifier, verifies = line.split("\t")
		checks[identifier] = (verifies, lambda root, i=identifier: run_native(binary, i, root))
	return checks


def run_native(binary, identifier, root):
	try:
		result = subprocess.run([str(binary), "--run", identifier, "--root", str(root)], capture_output=True, text=True, timeout=TIMEOUT)
	except subprocess.TimeoutExpired:
		return FAIL, f"timed out after {TIMEOUT} s"
	lines = [l for l in result.stdout.splitlines() if "\t" in l]
	if result.returncode != 0 or not lines:
		return FAIL, f"the check crashed or printed no result (exit {result.returncode}) {result.stderr.strip()[:200]}"
	status, _, reason = lines[-1].partition("\t")
	return status, reason


def tool_checks(folder):
	sys.path.insert(0, str(folder))
	checks = {}
	for path in sorted(folder.glob("acc_*.py")):
		spec = importlib.util.spec_from_file_location(path.stem, path)
		module = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(module)
		checks[module.ID] = (module.VERIFIES, lambda root, m=module: run_tool(m, root))
	return checks


def run_tool(module, root):
	import common

	ctx = common.Context(root)
	try:
		module.run(ctx)
	except Exception:
		ctx.failures.append("the check raised: " + traceback.format_exc().strip().splitlines()[-1])
	finally:
		ctx.close()
	if ctx.failures:
		reason = "; ".join(ctx.failures)
		if ctx.gaps:
			reason += "; also not implemented: " + "; ".join(ctx.gaps)
		return FAIL, reason
	if ctx.gaps:
		reason = "; ".join(ctx.gaps)
		return NOT_IMPLEMENTED, reason + ("; verified: " + "; ".join(ctx.notes) if ctx.notes else "")
	return PASS, "; ".join(ctx.notes)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--root", default="..")
	parser.add_argument("--build", default="build")
	parser.add_argument("--width", type=int, default=160, help="console reason width; results.tsv keeps the whole reason")
	args = parser.parse_args()
	root = Path(args.root).resolve()
	build = Path(args.build).resolve()

	spec = specmod.load(root)
	errors, warnings = check_spec.check(spec)
	print(f"Spec consistency: {len(spec.requirements)} requirements, {len(spec.checks)} checks, {len(errors)} error(s), {len(warnings)} warning(s)")
	for e in errors:
		print(f"  error: {e}")
	for w in warnings:
		print(f"  warning: {w}")
	(build / "spec_check.txt").write_text("".join(f"error: {e}\n" for e in errors) + "".join(f"warning: {w}\n" for w in warnings), encoding="utf-8")
	print()

	registered = {}
	registered.update(native_checks(build / "core_acceptance"))
	registered.update(native_checks(build / "events_acceptance"))
	registered.update(native_checks(build / "game_acceptance"))
	registered.update(tool_checks(Path(__file__).resolve().parent.parent / "tools"))

	results = []
	for check in spec.checks.values():
		if check.runs_on not in specmod.AUTOMATED:
			status, reason = NOT_AUTOMATABLE, f"runs on {check.runs_on}" + (" (needs the engine build)" if check.runs_on == "engine" else " (measured by people against the reference build)")
		elif check.id not in registered:
			status, reason = FAIL, "no acceptance test is registered for this automatable check"
		else:
			verifies, execute = registered[check.id]
			cited = [v.strip() for v in verifies.split(",")]
			if cited != check.verifies:
				status, reason = FAIL, f"the test cites {', '.join(cited)} but the spec says the check verifies {', '.join(check.verifies)}"
			else:
				status, reason = execute(root)
				# A check whose baseline is still a To measure row cannot pass (acceptance.md).
				pending = [b for b in check.baselines if spec.requirements.get(b) and spec.requirements[b].label == "To measure"]
				if status == PASS and pending:
					status, reason = NOT_IMPLEMENTED, f"baseline {', '.join(pending)} not measured yet, so the check cannot pass; against the placeholder data: {reason}"
		results.append((check, status, reason))
		short = reason if len(reason) <= args.width else reason[: args.width - 3] + "..."
		print(f"{check.id:<12} {status:<16} {short}")

	for identifier in sorted(set(registered) - set(spec.checks)):
		results.append((None, FAIL, f"{identifier} is registered but acceptance.md has no such check"))
		print(f"{identifier:<12} {FAIL:<16} registered but acceptance.md has no such check")

	with open(build / "results.tsv", "w", encoding="utf-8") as out:
		out.write("id\tstatus\truns_on\treason\n")
		for check, status, reason in results:
			if check:
				out.write(f"{check.id}\t{status}\t{check.runs_on}\t{reason}\n")

	counts = {s: sum(1 for _, st, _ in results if st == s) for s in (PASS, FAIL, NOT_IMPLEMENTED, NOT_AUTOMATABLE)}
	print()
	print("Totals: " + ", ".join(f"{s} {n}" for s, n in counts.items()))
	if errors:
		print(f"Spec consistency errors: {len(errors)}")
	return 1 if counts[FAIL] or errors else 0


if __name__ == "__main__":
	sys.exit(main())
