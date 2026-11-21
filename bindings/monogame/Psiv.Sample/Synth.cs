using System.Text;

namespace Psiv.Sample;

public enum SoundKind { Music, Ambient, Cue }

// Every sound the sample plays, generated in code as 16-bit mono PCM, the way Art generates its pictures: a few
// waveforms, envelopes and a step sequencer. An id's sound comes from a hash of the id (tempo, scale, patterns,
// waveforms), so every track, loop and cue a script names sounds different with nothing authored per id; the
// sample's own interface and battle cues are shaped by hand in one table instead.
public static class Synth
{
	public const int Rate = 22050;
	// Buffers are normalized to this much of full scale, so mixing a cue over music stays clear of clipping.
	private const float Peak = 0.7f;

	private enum Wave { Sine, Square, Pulse, Triangle, Saw, Noise }

	// One segment of a cue: a waveform gliding from one frequency to another (for noise, a filter cutoff).
	private readonly record struct Tone(Wave Wave, float From, float To, int Ms, float Gain = 1f);

	// The sample's own cues. Any other cue id gets a few hashed tones.
	private static readonly Dictionary<string, Tone[]> CueTable = new()
	{
		["back"] = [new(Wave.Square, 990, 990, 40, 0.5f), new(Wave.Square, 660, 660, 60, 0.5f)],
		["battle_start"] = [new(Wave.Saw, 220, 880, 260, 0.6f), new(Wave.Noise, 4000, 300, 380, 0.5f)],
		["confirm"] = [new(Wave.Square, 880, 880, 40, 0.5f), new(Wave.Square, 1320, 1320, 60, 0.5f)],
		["critical"] = [new(Wave.Noise, 6000, 400, 70), new(Wave.Square, 1760, 880, 150, 0.6f)],
		["cursor"] = [new(Wave.Pulse, 1320, 1320, 25, 0.4f)],
		["enemy_down"] = [new(Wave.Square, 440, 110, 280, 0.6f)],
		["game_over"] = [new(Wave.Triangle, 392, 196, 420), new(Wave.Triangle, 294, 147, 700)],
		["heal"] = [new(Wave.Sine, 660, 990, 120, 0.7f), new(Wave.Sine, 880, 1320, 180, 0.7f)],
		["hit"] = [new(Wave.Noise, 3000, 250, 100)],
		["level_up"] = [new(Wave.Triangle, 740, 1480, 200), new(Wave.Pulse, 1480, 1480, 90, 0.5f), new(Wave.Pulse, 1975, 1975, 160, 0.5f)],
		["line"] = [new(Wave.Triangle, 700, 700, 30, 0.5f), new(Wave.Triangle, 1050, 1050, 40, 0.5f)],
		["menu_close"] = [new(Wave.Triangle, 1040, 520, 90, 0.7f)],
		["menu_open"] = [new(Wave.Triangle, 520, 1040, 90, 0.7f)],
		["miss"] = [new(Wave.Sine, 900, 380, 130, 0.6f)],
		["refuse"] = [new(Wave.Saw, 180, 160, 90, 0.6f), new(Wave.Saw, 170, 140, 120, 0.6f)],
		["save"] = [new(Wave.Triangle, 587, 587, 80), new(Wave.Triangle, 784, 784, 80), new(Wave.Triangle, 1175, 1175, 170)],
		["status"] = [new(Wave.Pulse, 300, 620, 80, 0.5f), new(Wave.Pulse, 320, 660, 80, 0.5f)],
		["step"] = [new(Wave.Noise, 500, 200, 35, 0.3f)],
		["text"] = [new(Wave.Pulse, 1800, 1800, 12, 0.25f)],
		["victory"] = [new(Wave.Square, 620, 620, 110, 0.5f), new(Wave.Square, 775, 775, 110, 0.5f), new(Wave.Square, 930, 930, 110, 0.5f),
			new(Wave.Triangle, 1240, 1240, 360)],
	};

	public static IEnumerable<string> SampleCues => CueTable.Keys;

	public static short[] Generate(SoundKind kind, string id) => kind switch
	{
		SoundKind.Music => Music(id),
		SoundKind.Ambient => Ambient(id),
		_ => Cue(id),
	};

	// Semitones above the root, an octave of each.
	private static readonly int[][] Scales =
	[
		[0, 2, 4, 7, 9],
		[0, 3, 5, 7, 10],
		[0, 2, 4, 5, 7, 9, 11],
		[0, 2, 3, 5, 7, 8, 10],
		[0, 2, 3, 5, 7, 9, 10],
		[0, 1, 3, 5, 7, 8, 10],
	];

	private enum Mood { Bright, Calm, Dark, Driving }

	// A loop of eight bars of sixteen steps: a lead that walks the scale, a bass on the chord roots, chord tones as an
	// arpeggio and noise drums, each part's pattern and sound drawn from the id's hash. The second half repeats the
	// first with some notes changed, so the loop has a shape without being authored.
	public static short[] Music(string id)
	{
		var rng = new Rng(Noise.Hash(id));
		Mood mood = id.StartsWith("battle", StringComparison.Ordinal) ? Mood.Driving
			: id.StartsWith("dungeon_", StringComparison.Ordinal) ? Mood.Dark
			: id.StartsWith("town_", StringComparison.Ordinal) ? Mood.Bright
			: id == "title" ? Mood.Calm
			: (Mood)rng.Range(4);
		int bpm = mood switch
		{
			Mood.Driving => 138 + rng.Range(24),
			Mood.Dark => 70 + rng.Range(18),
			Mood.Bright => 100 + rng.Range(24),
			_ => 82 + rng.Range(20),
		};
		int[] scale = mood switch
		{
			Mood.Bright => Scales[rng.Range(2) == 0 ? 0 : 2],
			Mood.Dark => Scales[rng.Range(2) == 0 ? 1 : 5],
			Mood.Driving => Scales[3 + rng.Range(3)],
			_ => Scales[rng.Range(Scales.Length)],
		};
		int root = 45 + rng.Range(12);
		const int bars = 8, steps = 16;
		int stepSamples = Rate * 60 / bpm / 4;
		var buffer = new float[bars * steps * stepSamples];
		// Scale degree to frequency, degrees past the scale's end climbing octaves.
		float Pitch(int degree, int octave)
		{
			int n = scale.Length;
			int d = ((degree % n) + n) % n;
			int o = (int)Math.Floor(degree / (double)n) + octave;
			return 440f * MathF.Pow(2f, (root + scale[d] + 12 * o - 69) / 12f);
		}

		int[] roots = [3, 4, 5, 1, 2];
		int[] chords = [0, roots[rng.Range(5)], roots[rng.Range(5)], roots[rng.Range(5)]];
		Wave[] leads = [Wave.Square, Wave.Pulse, Wave.Triangle];
		Wave lead = leads[rng.Range(3)];
		int density = mood switch { Mood.Driving => 70, Mood.Bright => 55, Mood.Dark => 30, _ => 40 };
		var melody = new int?[4 * steps];
		int degree = scale.Length + rng.Range(3);
		for (int i = 0; i < melody.Length; i++)
		{
			bool strong = i % 4 == 0;
			if (!(strong ? rng.Chance(density + 25) : rng.Chance(density - 10)))
			{
				continue;
			}
			degree = Math.Clamp(degree + rng.Range(5) - 2, 0, 2 * scale.Length + 2);
			// On the beat the lead lands on a tone of the bar's chord.
			if (strong)
			{
				int chord = chords[i / steps] + scale.Length;
				int[] tones = [chord, chord + 2, chord + 4];
				degree = tones.MinBy(t => Math.Abs(t - degree));
			}
			melody[i] = degree;
		}
		for (int bar = 0; bar < bars; bar++)
		{
			int chord = chords[bar % 4];
			for (int step = 0; step < steps; step++)
			{
				int at = (bar * steps + step) * stepSamples;
				int index = bar % 4 * steps + step;
				int? note = melody[index];
				if (bar >= 4 && note is { } n && rng.Chance(25))
				{
					note = n + rng.Range(3) - 1;
				}
				if (note is { } lead1)
				{
					AddNote(buffer, at, stepSamples * 2, Pitch(lead1, 0), lead, 0.3f, 0.5f);
				}
				bool bassStep = mood == Mood.Driving ? step % 4 == 0 || step % 4 == 3 : step % 8 == 0;
				if (bassStep)
				{
					int bassDegree = step % 8 == 0 ? chord : chord + 4;
					AddNote(buffer, at, stepSamples * 3, Pitch(bassDegree, -2), Wave.Triangle, 0.45f, 0.5f);
				}
				if (mood is Mood.Bright or Mood.Calm && step % 2 == 0)
				{
					AddNote(buffer, at, stepSamples * 2, Pitch(chord + step / 2 % 3 * 2, -1), Wave.Sine, 0.12f, 0.5f);
				}
				if (mood != Mood.Calm)
				{
					bool kick = mood == Mood.Driving ? step % 4 == 0 : step % 8 == 0;
					if (kick)
					{
						AddSweep(buffer, at, Rate * 110 / 1000, Wave.Sine, 120, 40, 0.6f);
					}
					if (mood != Mood.Dark && step % 8 == 4)
					{
						AddSweep(buffer, at, Rate * 90 / 1000, Wave.Noise, 5000, 1500, 0.25f, Noise.Hash(bar, step));
					}
					if (step % 2 == 1 && mood != Mood.Dark)
					{
						AddSweep(buffer, at, Rate * 25 / 1000, Wave.Noise, 9000, 9000, 0.08f, Noise.Hash(step, bar, 7));
					}
				}
			}
		}
		return Normalize(buffer);
	}

	// A four-second loop: wind (noise through a sweeping filter), surf (low noise in swells), drips (sparse plinks
	// over a hiss) or a hum, picked by the id's hash. The end is blended into the start, so the loop has no seam.
	public static short[] Ambient(string id)
	{
		uint hash = Noise.Hash(id);
		var rng = new Rng(hash);
		int length = Rate * 4, tail = Rate / 4;
		var buffer = new float[length + tail];
		int style = (int)(hash % 4);
		float low = 0f;
		for (int i = 0; i < buffer.Length; i++)
		{
			float t = i / (float)length;
			float white = rng.Next() / (float)uint.MaxValue * 2f - 1f;
			switch (style)
			{
			case 0:
				float cutoff = 0.02f + 0.05f * (0.5f + 0.5f * MathF.Sin(2 * MathF.PI * t * 2 + hash % 7));
				low += cutoff * (white - low);
				buffer[i] = low * (0.6f + 0.4f * MathF.Sin(2 * MathF.PI * t));
				break;
			case 1:
				low += 0.03f * (white - low);
				float swell = MathF.Sin(MathF.PI * t * 2);
				buffer[i] = low * swell * swell;
				break;
			case 2:
				low += 0.2f * (white - low);
				buffer[i] = low * 0.08f;
				break;
			default:
				low += 0.01f * (white - low);
				float f = 55f * (1 + hash % 3);
				buffer[i] = 0.4f * MathF.Sin(2 * MathF.PI * f * i / Rate) + 0.15f * MathF.Sin(2 * MathF.PI * f * 2.01f * i / Rate) + low;
				break;
			}
		}
		if (style == 2)
		{
			for (int drip = 0; drip < 6; drip++)
			{
				AddSweep(buffer, rng.Range(length), Rate * 90 / 1000, Wave.Sine, 900 + rng.Range(900), 700, 0.6f);
			}
		}
		for (int i = 0; i < tail; i++)
		{
			float w = i / (float)tail;
			buffer[i] = buffer[i] * w + buffer[length + i] * (1 - w);
		}
		return Normalize(buffer.AsSpan(0, length));
	}

	public static short[] Cue(string id)
	{
		Tone[] tones = CueTable.TryGetValue(id, out Tone[]? shaped) ? shaped : HashedCue(id);
		int total = tones.Sum(t => Rate * t.Ms / 1000);
		var buffer = new float[total];
		int at = 0;
		uint seed = Noise.Hash(id);
		foreach (Tone t in tones)
		{
			int length = Rate * t.Ms / 1000;
			AddSweep(buffer, at, length, t.Wave, t.From, t.To, t.Gain, seed++);
			at += length;
		}
		return Normalize(buffer);
	}

	private static Tone[] HashedCue(string id)
	{
		var rng = new Rng(Noise.Hash(id));
		var tones = new Tone[2 + rng.Range(3)];
		float f = 200 + rng.Range(1000);
		for (int i = 0; i < tones.Length; i++)
		{
			Wave wave = (Wave)rng.Range(6);
			float to = Math.Clamp(f * (0.5f + rng.Range(100) / 66f), 80, 4000);
			tones[i] = new Tone(wave, f, to, 40 + rng.Range(160), 0.6f);
			f = to;
		}
		return tones;
	}

	// A note with a quick attack and a decay to `sustain` of its gain, released over its last quarter.
	private static void AddNote(float[] buffer, int at, int length, float freq, Wave wave, float gain, float sustain)
	{
		float phase = 0f;
		int attack = Rate / 200, decay = Rate / 20, release = length / 4;
		for (int i = 0; i < length && at + i < buffer.Length; i++)
		{
			float env = i < attack ? i / (float)attack
				: i < attack + decay ? 1f - (1f - sustain) * (i - attack) / decay
				: sustain;
			if (i > length - release)
			{
				env *= (length - i) / (float)release;
			}
			buffer[at + i] += gain * env * Sample(wave, phase);
			phase = (phase + freq / Rate) % 1f;
		}
	}

	// A tone gliding from `from` to `to` Hz, faded in and out at its ends so it starts and stops without a click. For
	// noise the frequencies set a low-pass filter's cutoff instead.
	private static void AddSweep(float[] buffer, int at, int length, Wave wave, float from, float to, float gain, uint seed = 1)
	{
		var rng = new Rng(seed);
		float phase = 0f, low = 0f;
		int edge = Math.Max(1, Math.Min(length / 4, Rate / 250));
		int fall = Math.Max(1, length / 3);
		for (int i = 0; i < length && at + i < buffer.Length; i++)
		{
			float t = i / (float)length;
			float freq = from + (to - from) * t;
			float env = Math.Min(1f, i / (float)edge) * Math.Min(1f, (length - i) / (float)fall);
			float v;
			if (wave == Wave.Noise)
			{
				float white = rng.Next() / (float)uint.MaxValue * 2f - 1f;
				low += Math.Min(1f, freq / Rate * 2f * MathF.PI) * (white - low);
				v = low;
			}
			else
			{
				v = Sample(wave, phase);
				phase = (phase + freq / Rate) % 1f;
			}
			buffer[at + i] += gain * env * v;
		}
	}

	// One sample of a periodic wave at phase `p`, 0 to 1.
	private static float Sample(Wave wave, float p) => wave switch
	{
		Wave.Sine => MathF.Sin(2 * MathF.PI * p),
		Wave.Square => p < 0.5f ? 0.6f : -0.6f,
		Wave.Pulse => p < 0.25f ? 0.6f : -0.6f,
		Wave.Triangle => 4f * MathF.Abs(p - 0.5f) - 1f,
		Wave.Saw => 0.7f * (2f * p - 1f),
		_ => 0f,
	};

	private static short[] Normalize(ReadOnlySpan<float> buffer)
	{
		float peak = 0f;
		foreach (float v in buffer)
		{
			peak = Math.Max(peak, MathF.Abs(v));
		}
		float scale = peak > 0f ? Peak * short.MaxValue / peak : 0f;
		var pcm = new short[buffer.Length];
		for (int i = 0; i < buffer.Length; i++)
		{
			pcm[i] = (short)MathF.Round(buffer[i] * scale);
		}
		return pcm;
	}

	public static void WriteWav(string path, short[] pcm)
	{
		using var w = new BinaryWriter(File.Create(path), Encoding.ASCII);
		int bytes = pcm.Length * 2;
		w.Write("RIFF"u8);
		w.Write(36 + bytes);
		w.Write("WAVEfmt "u8);
		w.Write(16);
		w.Write((short)1);
		w.Write((short)1);
		w.Write(Rate);
		w.Write(Rate * 2);
		w.Write((short)2);
		w.Write((short)16);
		w.Write("data"u8);
		w.Write(bytes);
		foreach (short s in pcm)
		{
			w.Write(s);
		}
	}

	// What the self-check finds wrong with each sound: generated twice it must come out the same, last long enough,
	// peak where Normalize puts it, never touch full scale, and differ from the sound before it.
	public static List<string> Check(IEnumerable<(SoundKind Kind, string Id)> sounds)
	{
		var problems = new List<string>();
		short[]? previous = null;
		foreach ((SoundKind kind, string id) in sounds)
		{
			short[] a = Generate(kind, id), b = Generate(kind, id);
			int peak = a.Length == 0 ? 0 : a.Max(v => Math.Abs((int)v));
			string name = $"{kind} {id}";
			if (!a.AsSpan().SequenceEqual(b))
			{
				problems.Add($"{name}: two generations differ");
			}
			if (a.Length < Rate / 100)
			{
				problems.Add($"{name}: only {a.Length} samples");
			}
			if (peak < short.MaxValue / 4 || peak >= short.MaxValue * 0.95)
			{
				problems.Add($"{name}: peak {peak} of {short.MaxValue}");
			}
			if (previous is not null && previous.AsSpan().SequenceEqual(a))
			{
				problems.Add($"{name}: the same samples as the sound before it");
			}
			previous = a;
		}
		return problems;
	}
}
