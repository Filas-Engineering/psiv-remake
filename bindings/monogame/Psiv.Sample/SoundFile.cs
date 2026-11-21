using System.Text;

namespace Psiv.Sample;

// A sound file decoded to 16-bit PCM, interleaved when stereo. Frames count one sample per channel, the unit loop
// points are given in.
public sealed record PcmSound(short[] Samples, int Rate, int Channels);

// WAV (8 or 16-bit PCM) and Ogg Vorbis (through NVorbis, which MonoGame's DesktopGL package already brings), read
// without an audio device: the header alone for the asset check, the whole file to play.
public static class SoundFile
{
	// The number of frames, from the header, or null with a reason the file does not play.
	public static long? ReadFrames(string path, out string problem)
	{
		problem = "";
		try
		{
			if (path.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase))
			{
				using var vorbis = new NVorbis.VorbisReader(path);
				return vorbis.TotalSamples;
			}
			using var reader = new BinaryReader(File.OpenRead(path), Encoding.ASCII);
			(_, int channels, int bits, long dataBytes, _) = WavLayout(reader, out problem);
			return problem.Length > 0 ? null : dataBytes / (channels * bits / 8);
		}
		catch (Exception e) when (e is IOException or UnauthorizedAccessException or InvalidDataException or ArgumentException or NotSupportedException or EndOfStreamException)
		{
			problem = "cannot be read: " + e.Message;
			return null;
		}
	}

	public static PcmSound Read(string path)
	{
		if (path.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase))
		{
			using var vorbis = new NVorbis.VorbisReader(path);
			var floats = new List<float>();
			float[] buffer = new float[4096 * vorbis.Channels];
			int read;
			while ((read = vorbis.ReadSamples(buffer, 0, buffer.Length)) > 0)
			{
				floats.AddRange(buffer.AsSpan(0, read));
			}
			return new PcmSound([.. floats.Select(f => (short)Math.Clamp(MathF.Round(f * short.MaxValue), short.MinValue, short.MaxValue))], vorbis.SampleRate, vorbis.Channels);
		}
		using var reader = new BinaryReader(File.OpenRead(path), Encoding.ASCII);
		(int rate, int channels, int bits, long dataBytes, long dataAt) = WavLayout(reader, out string problem);
		if (problem.Length > 0)
		{
			throw new InvalidDataException(problem);
		}
		reader.BaseStream.Position = dataAt;
		var samples = new short[dataBytes / (bits / 8)];
		for (int i = 0; i < samples.Length; i++)
		{
			samples[i] = bits == 16 ? reader.ReadInt16() : (short)((reader.ReadByte() - 128) << 8);
		}
		return new PcmSound(samples, rate, channels);
	}

	// The fmt chunk's numbers and where the data chunk is, or a reason it is not a WAV the sample plays.
	private static (int Rate, int Channels, int Bits, long DataBytes, long DataAt) WavLayout(BinaryReader r, out string problem)
	{
		problem = "";
		Stream s = r.BaseStream;
		if (s.Length < 12 || new string(r.ReadChars(4)) != "RIFF")
		{
			problem = "not a WAV or Ogg Vorbis file";
			return default;
		}
		r.ReadInt32();
		if (new string(r.ReadChars(4)) != "WAVE")
		{
			problem = "not a WAV or Ogg Vorbis file";
			return default;
		}
		int format = 0, channels = 0, rate = 0, bits = 0;
		while (s.Position + 8 <= s.Length)
		{
			string id = new(r.ReadChars(4));
			long size = r.ReadUInt32();
			long next = s.Position + size + (size & 1);
			if (id == "fmt ")
			{
				format = r.ReadInt16();
				channels = r.ReadInt16();
				rate = r.ReadInt32();
				r.ReadInt32();
				r.ReadInt16();
				bits = r.ReadInt16();
			}
			else if (id == "data")
			{
				if (format != 1 || bits is not (8 or 16) || channels is not (1 or 2))
				{
					problem = $"WAV format {format}, {bits}-bit, {channels} channels; use 8 or 16-bit PCM, mono or stereo";
					return default;
				}
				return (rate, channels, bits, Math.Min(size, s.Length - s.Position), s.Position);
			}
			s.Position = next;
		}
		problem = "a WAV file without a data chunk";
		return default;
	}
}
