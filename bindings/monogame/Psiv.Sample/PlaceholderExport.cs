using System.Text.Json;
using System.Text.Json.Nodes;

using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// --export-placeholders DIR: every generated picture and sound as files, with a manifest that reproduces the generated
// look, so an artist starts from a working asset directory and replaces one file at a time. The UI skin's frame,
// cursor and bars are drawn with rectangles, which a picture cannot reproduce exactly at every size, so they are
// written as a starting point with the skin turned off. Every file is read back and compared with what was generated.
public static class PlaceholderExport
{
	private const int SheetColumns = 8;

	public static int Run(string dir, GameData data)
	{
		Directory.CreateDirectory(dir);
		var canvas = new CanvasSurface(1, 1);
		var art = new Art(canvas);
		var written = new List<(string File, int W, int H, Color[] Pixels)>();
		var manifest = new JsonObject { ["version"] = AssetManifest.Version };

		// A sheet of the images, `columns` to a row, and the frame index of each image in it.
		string Sheet(string file, IReadOnlyList<int> images, int columns = SheetColumns)
		{
			(int fw, int fh, _) = canvas.ImageData(images[0]);
			int cols = Math.Min(columns, images.Count), rows = (images.Count + cols - 1) / cols;
			var pixels = new Color[cols * fw * rows * fh];
			for (int i = 0; i < images.Count; i++)
			{
				Color[] cell = canvas.ImageData(images[i]).Pixels;
				for (int y = 0; y < fh; y++)
				{
					Array.Copy(cell, y * fw, pixels, (i / cols * fh + y) * cols * fw + i % cols * fw, fw);
				}
			}
			return Picture(file, cols * fw, rows * fh, pixels);
		}
		string Picture(string file, int w, int h, Color[] pixels)
		{
			string full = Path.Combine(dir, file);
			Directory.CreateDirectory(Path.GetDirectoryName(full)!);
			Png.Write(full, w, h, pixels);
			written.Add((file, w, h, pixels));
			return file;
		}
		string Single(string file, int image)
		{
			(int w, int h, Color[] pixels) = canvas.ImageData(image);
			return Picture(file, w, h, pixels);
		}
		static JsonArray Ints(IEnumerable<int> values) => new([.. values.Select(v => (JsonNode?)v)]);
		static JsonObject Run(FrameRun run, Func<int, int> frame) => new() { ["frames"] = Ints(run.Frames.Select(frame)), ["ms"] = Ints(run.Ms) };

		var tilesets = new JsonArray();
		foreach (Theme theme in Enum.GetValues<Theme>())
		{
			Art.TileSet t = art.Tiles(theme);
			int[] objects = [t.Door, t.ChestClosed, t.ChestOpen, t.Stall, t.Bed, t.Sign, t.Rune, .. t.ExitArrow];
			int[] all = [.. t.Floor, .. t.Decor, .. t.WallTop, .. t.WallFace, .. t.Water, .. objects];
			string kind = theme.ToString().ToLowerInvariant();
			int at = 0;
			JsonArray Group(int[] images)
			{
				JsonArray cells = Ints(Enumerable.Range(at, images.Length));
				at += images.Length;
				return cells;
			}
			(Color edge, Color bank) = (t.Edge, t.Bank);
			var entry = new JsonObject
			{
				["kind"] = kind, ["image"] = Sheet($"tilesets/{kind}.png", all), ["tile"] = Pixels.Size,
				["floor"] = Group(t.Floor), ["decor"] = Group(t.Decor), ["decor_one_in"] = t.DecorOneIn,
				["wall_top"] = Group(t.WallTop), ["wall_top_alt_one_in"] = t.WallTopAltOneIn,
				["wall_face"] = Group(t.WallFace), ["wall_face_alt_one_in"] = t.WallFaceAltOneIn,
				["water"] = Group(t.Water), ["water_frame_ms"] = t.WaterFrameMs,
				["edge"] = $"#{edge.R:x2}{edge.G:x2}{edge.B:x2}", ["bank"] = $"#{bank.R:x2}{bank.G:x2}{bank.B:x2}",
			};
			entry["objects"] = new JsonObject(AssetManifest.ObjectNames.Select((name, i) => KeyValuePair.Create(name, (JsonNode?)(at + i))));
			tilesets.Add(entry);
		}
		manifest["tilesets"] = tilesets;

		string[] people = [.. data.Characters.Concat(data.Actors).Order(StringComparer.Ordinal)];
		var characters = new JsonArray();
		var portraits = new JsonObject();
		foreach (string id in people)
		{
			// Walk frames first, three to a facing, then each pose's pictures that are not among them.
			List<int> frames = [.. Enumerable.Range(0, 12).Select(i => art.PersonFrame(id, (Direction)(i / 3), i % 3))];
			int Cell(int image)
			{
				if (frames.IndexOf(image) is int at and >= 0)
				{
					return at;
				}
				frames.Add(image);
				return frames.Count - 1;
			}
			var poses = new JsonObject();
			foreach (string animation in data.Animations.Order(StringComparer.Ordinal))
			{
				Pose pose = Art.PoseFor(animation);
				(int[] phases, int[] ms) = Art.PoseTimeline(pose);
				poses[animation] = new JsonObject
				{
					["frames"] = new JsonObject(AssetManifest.Facings.Select((f, i) => KeyValuePair.Create(f,
						(JsonNode?)Ints(phases.Select(phase => Cell(art.GeneratedPoseImage(id, (Direction)i, pose, phase))))))),
					["ms"] = Ints(ms),
				};
			}
			characters.Add(new JsonObject
			{
				["id"] = id, ["image"] = Sheet($"characters/{id}.png", frames, 6), ["frame"] = Ints([Pixels.Size, Pixels.Size]),
				["walk"] = new JsonObject(AssetManifest.Facings.Select((f, i) => KeyValuePair.Create(f, (JsonNode?)Ints([i * 3, i * 3 + 1, i * 3 + 2])))),
				["poses"] = poses,
				["animations"] = new JsonObject(data.Animations.Order(StringComparer.Ordinal).Select(a => KeyValuePair.Create(a, (JsonNode?)a))),
			});
			portraits[id] = Single($"portraits/{id}.png", art.Portrait("", id));
		}
		manifest["characters"] = characters;
		manifest["portraits"] = portraits;

		var figures = new JsonArray();
		foreach ((string id, bool enemy) in data.Characters.Concat(data.Vehicles).Order(StringComparer.Ordinal).Select(id => (id, false))
			.Concat(data.Enemies.Order(StringComparer.Ordinal).Select(id => (id, true))))
		{
			Figure f = !enemy && data.FieldVehicles.Contains(id) ? art.VehicleFigure(id) : art.BattleFigure(id, enemy);
			figures.Add(new JsonObject
			{
				["id"] = id, ["side"] = enemy ? "enemy" : "party", ["image"] = Sheet($"figures/{(enemy ? "enemy" : "party")}_{id}.png", f.Images),
				["frame"] = Ints([Pixels.Size, Pixels.Size]), ["idle"] = Run(f.Idle, i => i),
				["animations"] = new JsonObject(f.Animations.Select(a => KeyValuePair.Create(a.Key, (JsonNode?)Run(a.Value, i => i)))),
			});
		}
		foreach (string id in data.Characters.Concat(data.Vehicles).Order(StringComparer.Ordinal))
		{
			Figure f = data.FieldVehicles.Contains(id) ? art.VehicleBackFigure(id) : art.BackFigure(id);
			figures.Add(new JsonObject
			{
				["id"] = id, ["side"] = "party", ["view"] = "back", ["image"] = Sheet($"figures/party_back_{id}.png", f.Images),
				["frame"] = Ints([Pixels.Size, Pixels.Size]), ["idle"] = Run(f.Idle, i => i),
				["animations"] = new JsonObject(f.Animations.Select(a => KeyValuePair.Create(a.Key, (JsonNode?)Run(a.Value, i => i)))),
			});
		}
		manifest["figures"] = figures;

		var fieldEnemies = new JsonArray();
		foreach (string encounter in data.Encounters.Order(StringComparer.Ordinal))
		{
			uint seed = Noise.Hash(encounter);
			fieldEnemies.Add(new JsonObject
			{
				["encounter"] = encounter, ["image"] = Sheet($"field_enemies/{encounter}.png", [art.Creature(seed, 0), art.Creature(seed, 1)]),
				["frames"] = Ints([0, 1]), ["ms"] = 280,
			});
		}
		manifest["field_enemies"] = fieldEnemies;

		var vehicles = new JsonArray();
		foreach (string id in data.FieldVehicles.Order(StringComparer.Ordinal))
		{
			var images = new List<int>();
			var facings = new JsonObject();
			foreach (Direction facing in Art.VehicleFacings)
			{
				facings[facing.ToString().ToLowerInvariant()] = Ints([images.Count, images.Count + 1]);
				images.Add(art.GeneratedVehicle(id, facing, 0));
				images.Add(art.GeneratedVehicle(id, facing, 1));
			}
			vehicles.Add(new JsonObject
			{
				["id"] = id, ["image"] = Sheet($"vehicles/{id}.png", images), ["frame"] = Ints([Pixels.Size, Pixels.Size]), ["facings"] = facings,
				["ms"] = Art.VehicleFrameMs,
			});
		}
		manifest["vehicles"] = vehicles;

		manifest["backdrops"] = new JsonArray([.. Enum.GetValues<Theme>().Select(theme =>
		{
			string kind = theme.ToString().ToLowerInvariant();
			return (JsonNode?)new JsonObject { ["kind"] = kind, ["image"] = Picture($"backdrops/{kind}.png", Art.BackdropWidth, Art.BackdropHeight, Art.BackdropPixels(theme)) };
		})]);

		manifest["effects"] = new JsonArray([.. BattlePresentation.GeneratedEffects.Select(id =>
		{
			Figure e = art.Effect(id)!;
			return (JsonNode?)new JsonObject
			{
				["id"] = id, ["image"] = Sheet($"effects/{id}.png", e.Images), ["frame"] = Ints([Art.EffectSize, Art.EffectSize]),
				["frames"] = Ints(e.Idle.Frames), ["ms"] = Ints(e.Idle.Ms),
			};
		})]);

		manifest["sprites"] = new JsonObject
		{
			["shadow"] = Single("sprites/shadow.png", art.Shadow), ["bubble"] = Single("sprites/bubble.png", art.Bubble),
			["alert"] = Single("sprites/alert.png", art.Alert), ["pointer"] = Single("sprites/pointer.png", art.Pointer),
			["pier"] = Single("sprites/pier.png", art.Pier),
		};
		manifest["status_icons"] = new JsonObject(data.Statuses.Where(s => s != "defeated").Order(StringComparer.Ordinal)
			.Select(s => KeyValuePair.Create(s, (JsonNode?)Single($"status_icons/{s}.png", art.StatusIcon(s)))));

		// The generated window, panel border, cursor and bar drawn once into pictures.
		Color[] Draw(int w, int h, Action<IDrawSurface> draw)
		{
			var c = new CanvasSurface(w, h);
			draw(c);
			return c.Pixels;
		}
		manifest["ui"] = new JsonObject
		{
			["enabled"] = false,
			["frame"] = new JsonObject { ["image"] = Picture("ui/frame.png", 24, 24, Draw(24, 24, c => Style.Box(c, 0, 0, 24, 24))), ["border"] = 8 },
			["panel_frames"] = new JsonArray(new JsonObject
			{
				["image"] = Picture("ui/panel_frame.png", 12, 12, Draw(12, 12, c =>
				{
					c.Fill(0, 0, 12, 12, Color.Black);
					c.Fill(1, 1, 10, 10, Style.Text);
					c.Fill(4, 4, 4, 4, Style.Background);
				})),
				["border"] = 4,
			}),
			["cursor"] = Picture("ui/cursor.png", 12, 12, Draw(12, 12, c => Style.Pointer(c, 0, 0, Style.Accent))),
			["bar_track"] = Picture("ui/bar_track.png", 8, 8, Draw(8, 8, c => c.Fill(0, 0, 8, 8, Style.Track))),
			["bar_fill"] = Picture("ui/bar_fill.png", 8, 8, Draw(8, 8, c => c.Fill(0, 0, 8, 8, Color.White))),
		};

		// Each area's map as written, drawn from its tiles, as a starting point for whole-area pictures. Turned off, since a
		// still picture cannot reproduce the water's frames, and with the section off the maps/ file convention is off too.
		manifest["maps"] = new JsonObject
		{
			["enabled"] = false,
			["cell"] = Pixels.Size,
			["areas"] = new JsonArray([.. data.Areas.Order(StringComparer.Ordinal).Select(area =>
			{
				AreaMap map = data.Maps[area];
				string file = Picture($"maps/{area}.png", map.Width * Pixels.Size, map.Height * Pixels.Size,
					FieldRenderer.WrittenPixels(map, data.AreaKinds[area], area, Pixels.Size));
				return (JsonNode?)new JsonObject { ["area"] = area, ["image"] = file };
			})]),
		};
		manifest["field"] = new JsonObject { ["wall_shadows"] = true, ["water_foam"] = true };

		// A bitmap font per language with every glyph its text tables use, turned off: the generated look is the system font.
		Directory.CreateDirectory(Path.Combine(dir, "fonts"));
		manifest["bitmap_fonts"] = new JsonObject
		{
			["enabled"] = false,
			["fonts"] = new JsonArray([.. data.Languages.Order(StringComparer.Ordinal).Select(language =>
			{
				(int w, int h, Color[] pixels, JsonObject metrics) = GeneratedFont.Build(data.TextGlyphs(language, null));
				string metricsFile = $"fonts/{language}.json";
				File.WriteAllText(Path.Combine(dir, metricsFile), metrics.ToJsonString(new JsonSerializerOptions
				{
					WriteIndented = true, IndentCharacter = '\t', IndentSize = 1, Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
				}) + "\n");
				return (JsonNode?)new JsonObject { ["language"] = language, ["image"] = Picture($"fonts/{language}.png", w, h, pixels), ["metrics"] = metricsFile };
			})]),
		};

		var audio = new JsonArray();
		int sounds = 0;
		List<string> problems = [];
		foreach ((SoundKind kind, IEnumerable<string> ids) in new[] { (SoundKind.Music, (IEnumerable<string>)data.Music), (SoundKind.Ambient, data.Ambient), (SoundKind.Cue, data.Cues) })
		{
			string name = kind.ToString().ToLowerInvariant();
			foreach (string id in ids.Order(StringComparer.Ordinal))
			{
				short[] pcm = Synth.Generate(kind, id);
				string file = $"audio/{name}/{id}.wav";
				Directory.CreateDirectory(Path.Combine(dir, $"audio/{name}"));
				Synth.WriteWav(Path.Combine(dir, file), pcm);
				var entry = new JsonObject { ["kind"] = name, ["id"] = id, ["file"] = file };
				if (kind != SoundKind.Cue)
				{
					entry["loop_start"] = 0;
					entry["loop_end"] = pcm.Length;
				}
				audio.Add(entry);
				sounds++;
				if (!SoundFile.Read(Path.Combine(dir, file)).Samples.AsSpan().SequenceEqual(pcm))
				{
					problems.Add($"{file}: reads back different from the generated sound");
				}
			}
		}
		manifest["audio"] = audio;

		BattlePresentation.Defaults().Write(Path.Combine(dir, BattlePresentation.FileName));
		manifest["battle_presentation"] = BattlePresentation.FileName;
		File.WriteAllText(Path.Combine(dir, AssetManifest.FileName),
			manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true, IndentCharacter = '\t', IndentSize = 1 }) + "\n");

		foreach ((string file, int w, int h, Color[] pixels) in written)
		{
			Color[] back = Png.Read(Path.Combine(dir, file), out int rw, out int rh);
			if (rw != w || rh != h || !back.AsSpan().SequenceEqual(pixels))
			{
				problems.Add($"{file}: reads back different from the generated picture");
			}
		}
		problems.AddRange(Assets.Load(dir, data).Problems.Select(p => "check: " + p));
		foreach (string problem in problems)
		{
			Console.WriteLine("export: " + problem);
		}
		Console.WriteLine($"export: {written.Count} pictures and {sounds} sounds in {Path.GetFullPath(dir)}, each read back: {problems.Count} problems");
		return problems.Count == 0 ? 0 : 1;
	}
}
