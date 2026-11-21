"""Checks the WAV files `Psiv.Sample --dump-audio DIR` writes: header, length and peak of each.

Usage: python3 check_wav.py DIR
"""

import array
import pathlib
import sys
import wave

# Each file's shortest and longest length in milliseconds.
EXPECTED = {
	"music_town_tidewater.wav": (4000, 40000),
	"cue_confirm.wav": (20, 1000),
	"cue_hit.wav": (20, 1000),
	"cue_level_up.wav": (20, 1000),
}


def check(path):
	problems = []
	raw = path.read_bytes()
	if raw[:4] != b"RIFF" or raw[8:16] != b"WAVEfmt " or int.from_bytes(raw[4:8], "little") != len(raw) - 8:
		problems.append("not a RIFF WAVE file of its own length")
	with wave.open(str(path), "rb") as w:
		if (w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getcomptype()) != (1, 2, 22050, "NONE"):
			problems.append(f"format {w.getnchannels()} channels, {w.getsampwidth() * 8} bits, {w.getframerate()} Hz, {w.getcomptype()}")
		frames = w.getnframes()
		samples = array.array("h", w.readframes(frames))
	if sys.byteorder != "little":
		samples.byteswap()
	ms = frames * 1000 // 22050
	low, high = EXPECTED[path.name]
	if not low <= ms <= high or len(samples) != frames:
		problems.append(f"{ms} ms, {len(samples)} samples for {frames} frames")
	peak = max(abs(s) for s in samples) if samples else 0
	if peak == 0 or peak >= 32767 or min(samples) == -32768:
		problems.append(f"peak {peak}: silent or clipped")
	return ms, peak, problems


def main():
	folder = pathlib.Path(sys.argv[1])
	failed = 0
	for name in EXPECTED:
		ms, peak, problems = check(folder / name)
		print(f"{name}: {ms} ms, peak {peak} of 32767" + ("" if not problems else ": " + "; ".join(problems)))
		failed += len(problems)
	print(f"wav check: {len(EXPECTED)} files, {failed} problems")
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
