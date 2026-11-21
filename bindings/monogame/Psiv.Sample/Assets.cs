namespace Psiv.Sample;

// The asset directory as loaded: its manifest and battle presentation table, every problem found in either, and the
// size and time of each file it names, so a reload can say what changed. Art and the audio read the files.
public sealed class Assets
{
	private readonly Dictionary<string, (long Length, DateTime Written)> _stamps;

	public AssetManifest Manifest { get; }
	public BattlePresentation Battle { get; }
	public BattleAnimation Animation { get; }
	public UiLayoutSet Layout { get; }
	public IReadOnlyList<AssetProblem> Problems { get; }

	private Assets(AssetManifest manifest, BattlePresentation battle, BattleAnimation animation, UiLayoutSet layout, List<AssetProblem> problems)
	{
		Manifest = manifest;
		Battle = battle;
		Animation = animation;
		Layout = layout;
		Problems = problems;
		_stamps = manifest.Files().Select(manifest.Full).Where(File.Exists).ToDictionary(f => f, f => (new FileInfo(f).Length, File.GetLastWriteTimeUtc(f)));
		_stamps[Path.Combine(manifest.Directory, AssetManifest.FileName)] = Stamp(Path.Combine(manifest.Directory, AssetManifest.FileName));
		foreach (string included in animation.Files)
		{
			_stamps[included] = Stamp(included);
		}
	}

	private static (long, DateTime) Stamp(string file) => File.Exists(file) ? (new FileInfo(file).Length, File.GetLastWriteTimeUtc(file)) : (-1, default);

	public static Assets Load(string directory, GameData data)
	{
		AssetManifest manifest = AssetManifest.Load(directory, data);
		var layoutProblems = new List<AssetProblem>();
		UiLayoutSet layout = manifest.UiLayoutEnabled == false ? UiLayoutSet.Defaults : UiLayoutSet.Load(manifest.Directory, manifest.UiLayoutEnabled == true, layoutProblems, data.Statuses);
		// Scaled tilesets must fill the field's cell, so styles with a cell of their own read the manifest again against it.
		if (layout.Styles.Param("field_cell") is var cell && cell != Art.Tile)
		{
			manifest = AssetManifest.Load(directory, data, cell);
		}
		var problems = new List<AssetProblem>(manifest.Problems);
		BattlePresentation battle = BattlePresentation.Defaults();
		if (manifest.BattlePresentation is { } table)
		{
			battle = BattlePresentation.Load(manifest.Full(table), table, data, manifest.Effects.Select(e => e.Id), problems);
			battle.CheckPoses(manifest.Figures, table, problems);
		}
		BattleAnimation animation = BattleAnimation.Builtin();
		if (manifest.BattleAnimation is { } file)
		{
			var frames = new Dictionary<string, int>(StringComparer.Ordinal);
			foreach (string id in BattlePresentation.GeneratedEffects)
			{
				frames[id] = Art.EffectFrames;
			}
			foreach (EffectEntry e in manifest.Effects)
			{
				frames[e.Id] = e.Run.Frames.Length;
			}
			var backdrops = new HashSet<string>(manifest.Backdrops.SelectMany(b => new[] { b.Kind is null ? null : "kind:" + b.Kind, b.Area is null ? null : "area:" + b.Area,
				b.Encounter is null ? null : "encounter:" + b.Encounter, b.Terrain is null ? null : "terrain:" + b.Terrain, b.Id is null ? null : "id:" + b.Id }).OfType<string>()) { "generated" };
			animation = BattleAnimation.Load(manifest.Directory, file, new AnimCheckContext(data, frames, [.. layout.Styles.Colors.Keys], manifest.Directory, backdrops), problems);
		}
		problems.AddRange(layoutProblems);
		// The dialogue's fx tags name scripts of the animation set.
		foreach ((string name, UiTag tag) in layout.Screen("dialogue").Tags.Where(t => t.Value.Action == "fx"))
		{
			foreach (string script in new[] { tag.Id, tag.Default }.Concat(tag.Values?.Values ?? []).OfType<string>().Distinct())
			{
				if (animation.FxProblem(script) is { } reason)
				{
					problems.Add(new AssetProblem("ui/dialogue.json", $"tags.{name}", reason));
				}
			}
		}
		return new Assets(manifest, battle, animation, layout, problems);
	}

	// The files whose size or time differ from what `before` loaded, the manifest included; empty when nothing changed.
	public IEnumerable<string> ChangedSince(Assets before) =>
		_stamps.Where(f => before._stamps.GetValueOrDefault(f.Key) != f.Value).Select(f => Path.GetRelativePath(Manifest.Directory, f.Key))
			.Concat(before._stamps.Keys.Where(f => !_stamps.ContainsKey(f)).Select(f => Path.GetRelativePath(Manifest.Directory, f)))
			.Order(StringComparer.Ordinal);
}
