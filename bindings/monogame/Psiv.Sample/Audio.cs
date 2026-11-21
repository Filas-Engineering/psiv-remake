using System.Runtime.InteropServices;

using Microsoft.Xna.Framework.Audio;

namespace Psiv.Sample;

// Everything the sample plays goes through this: one music track at a time, one ambient loop, one-shot cues, and a
// volume per group. Sounds are named by id; the implementation turns ids into sound (Synth generates them here).
// Replace it to put real audio behind the same screens.
public interface IAudioOut
{
	// The track or loop playing (or fading in), "" for none.
	string Music { get; }
	string Ambient { get; }
	// A new track fades in over `fadeMs` while the one before fades out; 0 switches at once.
	void PlayMusic(string track, int fadeMs);
	void StopMusic(int fadeMs);
	void PlayAmbient(string sound, int fadeMs);
	void StopAmbient(int fadeMs);
	void PlayCue(string cue);
	// Percentages, from the profile's music, ambient and effects volumes.
	void SetVolumes(int music, int ambient, int effects);
	void Update(int dtMs);
}

// Silent: keeps what is playing and a log of every call, for headless runs, --mute and machines without an audio
// device, so scripts can assert on what would have played.
public class RecordingAudio : IAudioOut
{
	private const int MostCalls = 4096;

	private readonly List<string> _calls = [];
	// Calls dropped from the front of the log once it is full, so marks stay valid.
	private int _dropped;

	public string Music { get; private set; } = "";
	public string Ambient { get; private set; } = "";
	public (int Music, int Ambient, int Effects) Volumes { get; private set; } = (-1, -1, -1);

	// Every call so far, counting those no longer kept.
	public int CallCount => _dropped + _calls.Count;

	public IEnumerable<string> CallsSince(int mark) => _calls.Skip(Math.Max(0, mark - _dropped));

	private void Record(string call)
	{
		_calls.Add(call);
		if (_calls.Count > MostCalls)
		{
			_calls.RemoveAt(0);
			_dropped++;
		}
	}

	public virtual void PlayMusic(string track, int fadeMs)
	{
		if (track != Music)
		{
			Music = track;
			Record($"music {track} fade {fadeMs}");
		}
	}

	public virtual void StopMusic(int fadeMs)
	{
		if (Music.Length > 0)
		{
			Music = "";
			Record($"music stop fade {fadeMs}");
		}
	}

	public virtual void PlayAmbient(string sound, int fadeMs)
	{
		if (sound != Ambient)
		{
			Ambient = sound;
			Record($"ambient {sound} fade {fadeMs}");
		}
	}

	public virtual void StopAmbient(int fadeMs)
	{
		if (Ambient.Length > 0)
		{
			Ambient = "";
			Record($"ambient stop fade {fadeMs}");
		}
	}

	public virtual void PlayCue(string cue) => Record("cue " + cue);

	public virtual void SetVolumes(int music, int ambient, int effects)
	{
		if (Volumes != (music, ambient, effects))
		{
			Volumes = (music, ambient, effects);
			Record($"volumes {music} {ambient} {effects}");
		}
	}

	public virtual void Update(int dtMs)
	{
	}
}

// Plays the generated sounds through MonoGame, keeping the same state and log as the silent one. Cues are generated
// when it starts, tracks and loops the first time each plays.
public sealed class MonoGameAudio : RecordingAudio, IDisposable
{
	// A looping instance fading toward a gain; one that has faded out is stopped and dropped.
	private sealed class Loop(SoundEffectInstance instance, float gain, float target, int fadeMs)
	{
		public SoundEffectInstance Instance { get; } = instance;
		public float Gain { get; set; } = gain;
		public float Target { get; set; } = target;
		public float PerMs { get; set; } = fadeMs <= 0 ? 1f : 1f / fadeMs;
	}

	private readonly Action<string> _log;
	private readonly Dictionary<(SoundKind, string), SoundEffect> _sounds = [];
	private readonly List<Loop> _music = [];
	private readonly List<Loop> _ambient = [];
	private bool _failed;

	private MonoGameAudio(Action<string> log)
	{
		_log = log;
		foreach (string cue in Synth.SampleCues)
		{
			Sound(SoundKind.Cue, cue);
		}
	}

	// MonoGame audio, or the silent recorder with one log line when there is no device to play on (CI, Xvfb,
	// containers) or `mute` asks for none.
	public static RecordingAudio Create(bool mute, Action<string> log)
	{
		if (mute)
		{
			return new RecordingAudio();
		}
		try
		{
			return new MonoGameAudio(log);
		}
		catch (Exception e)
		{
			log($"audio: no device ({e.GetType().Name}: {e.Message}); playing silently");
			return new RecordingAudio();
		}
	}

	private SoundEffect Sound(SoundKind kind, string id)
	{
		if (!_sounds.TryGetValue((kind, id), out SoundEffect? sound))
		{
			short[] pcm = Synth.Generate(kind, id);
			sound = new SoundEffect(MemoryMarshal.AsBytes(pcm.AsSpan()).ToArray(), Synth.Rate, AudioChannels.Mono);
			_sounds[(kind, id)] = sound;
		}
		return sound;
	}

	// A device that goes away mid-run leaves the sample silent, not crashed.
	private void Guard(Action play)
	{
		if (_failed)
		{
			return;
		}
		try
		{
			play();
		}
		catch (Exception e)
		{
			_failed = true;
			_log($"audio: playback failed ({e.GetType().Name}: {e.Message}); playing silently from now on");
			foreach (Loop loop in _music.Concat(_ambient))
			{
				loop.Instance.Dispose();
			}
			_music.Clear();
			_ambient.Clear();
		}
	}

	private static void FadeOutAll(List<Loop> loops, int fadeMs)
	{
		foreach (Loop loop in loops)
		{
			loop.Target = 0f;
			loop.PerMs = fadeMs <= 0 ? 1f : 1f / fadeMs;
		}
	}

	private void Start(List<Loop> loops, SoundKind kind, string id, int fadeMs)
	{
		FadeOutAll(loops, fadeMs);
		SoundEffectInstance instance = Sound(kind, id).CreateInstance();
		instance.IsLooped = true;
		float gain = fadeMs <= 0 ? 1f : 0f;
		instance.Volume = 0f;
		instance.Play();
		loops.Add(new Loop(instance, gain, 1f, fadeMs));
	}

	public override void PlayMusic(string track, int fadeMs)
	{
		bool changed = track != Music;
		base.PlayMusic(track, fadeMs);
		if (changed)
		{
			Guard(() => Start(_music, SoundKind.Music, track, fadeMs));
		}
	}

	public override void StopMusic(int fadeMs)
	{
		base.StopMusic(fadeMs);
		FadeOutAll(_music, fadeMs);
	}

	public override void PlayAmbient(string sound, int fadeMs)
	{
		bool changed = sound != Ambient;
		base.PlayAmbient(sound, fadeMs);
		if (changed)
		{
			Guard(() => Start(_ambient, SoundKind.Ambient, sound, fadeMs));
		}
	}

	public override void StopAmbient(int fadeMs)
	{
		base.StopAmbient(fadeMs);
		FadeOutAll(_ambient, fadeMs);
	}

	public override void PlayCue(string cue)
	{
		base.PlayCue(cue);
		if (Volumes.Effects > 0)
		{
			Guard(() => Sound(SoundKind.Cue, cue).Play(Volumes.Effects / 100f, 0f, 0f));
		}
	}

	public override void Update(int dtMs)
	{
		Guard(() =>
		{
			Step(_music, Volumes.Music, dtMs);
			Step(_ambient, Volumes.Ambient, dtMs);
		});
	}

	private static void Step(List<Loop> loops, int volume, int dtMs)
	{
		for (int i = loops.Count - 1; i >= 0; i--)
		{
			Loop loop = loops[i];
			float step = loop.PerMs * dtMs;
			loop.Gain = loop.Gain < loop.Target ? Math.Min(loop.Target, loop.Gain + step) : Math.Max(loop.Target, loop.Gain - step);
			if (loop.Gain <= 0f && loop.Target <= 0f)
			{
				loop.Instance.Stop();
				loop.Instance.Dispose();
				loops.RemoveAt(i);
				continue;
			}
			loop.Instance.Volume = Math.Clamp(loop.Gain * Math.Max(0, volume) / 100f, 0f, 1f);
		}
	}

	public void Dispose()
	{
		foreach (Loop loop in _music.Concat(_ambient))
		{
			loop.Instance.Dispose();
		}
		foreach (SoundEffect sound in _sounds.Values)
		{
			sound.Dispose();
		}
		_sounds.Clear();
	}
}
