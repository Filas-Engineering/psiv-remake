namespace Psiv.Sample;

// The asset directory as loaded: its manifest and battle presentation table, every problem found in either, and the
// size and time of each file it names, so a reload can say what changed. Art and the audio read the files.
public sealed class Assets
{
	private readonly Dictionary<string, (long Length, DateTime Written)> _stamps;

	public AssetManifest Manifest { get; }
	public BattlePresentation Battle { get; }
	public UiLayoutSet Layout { get; }
	public IReadOnlyList<AssetProblem> Problems { get; }

	private Assets(AssetManifest manifest, BattlePresentation battle, UiLayoutSet layout, List<AssetProblem> problems)
	{
		Manifest = manifest;
		Battle = battle;
		Layout = layout;
		Problems = problems;
		_stamps = manifest.Files().Select(manifest.Full).Where(File.Exists).ToDictionary(f => f, f => (new FileInfo(f).Length, File.GetLastWriteTimeUtc(f)));
		_stamps[Path.Combine(manifest.Directory, AssetManifest.FileName)] = Stamp(Path.Combine(manifest.Directory, AssetManifest.FileName));
	}

	private static (long, DateTime) Stamp(string file) => File.Exists(file) ? (new FileInfo(file).Length, File.GetLastWriteTimeUtc(file)) : (-1, default);

	public static Assets Load(string directory, GameData data)
	{
		AssetManifest manifest = AssetManifest.Load(directory, data);
		var problems = new List<AssetProblem>(manifest.Problems);
		BattlePresentation battle = manifest.BattlePresentation is { } table
			? BattlePresentation.Load(manifest.Full(table), table, data, manifest.Effects.Select(e => e.Id), problems)
			: BattlePresentation.Defaults();
		UiLayoutSet layout = manifest.UiLayoutEnabled == false ? UiLayoutSet.Defaults : UiLayoutSet.Load(manifest.Directory, manifest.UiLayoutEnabled == true, problems);
		return new Assets(manifest, battle, layout, problems);
	}

	// The files whose size or time differ from what `before` loaded, the manifest included; empty when nothing changed.
	public IEnumerable<string> ChangedSince(Assets before) =>
		_stamps.Where(f => before._stamps.GetValueOrDefault(f.Key) != f.Value).Select(f => Path.GetRelativePath(Manifest.Directory, f.Key))
			.Concat(before._stamps.Keys.Where(f => !_stamps.ContainsKey(f)).Select(f => Path.GetRelativePath(Manifest.Directory, f)))
			.Order(StringComparer.Ordinal);
}
