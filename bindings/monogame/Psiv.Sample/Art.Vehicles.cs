using Microsoft.Xna.Framework;

using Psiv.Session;

namespace Psiv.Sample;

// Vehicles in the field and the pier of a port: generated from code like the rest, each replaceable from the asset
// directory (`vehicles` entries, the `pier` sprite).
public sealed partial class Art
{
	public const int VehicleFrameMs = 240;
	public static readonly Direction[] VehicleFacings = [Direction.North, Direction.East, Direction.South, Direction.West];

	private readonly Dictionary<(string, Direction, int), int> _vehicles = [];
	private readonly Dictionary<string, (VehicleEntry Entry, int[] Images)> _vehicleSheets = [];
	private int _pier = -1;

	private static readonly string[] VehicleSide =
	[
		"................",
		"................",
		"................",
		".oo.............",
		".ofo....oooo....",
		".ofo...owwwwo...",
		".ofo..ommmmmmo..",
		".ofooommmmmmmmo.",
		"..oddddddddddddo",
		"..oddddddddddddo",
		"..ommmmmmmmmmmo.",
		"...olllllllllo..",
		"....ooooooooo...",
		"...ss..ss..ss...",
		"................",
		"................",
	];

	private static readonly string[] VehicleFront =
	[
		"................",
		"................",
		"................",
		"......oooo......",
		".....owwwwo.....",
		"....ommmmmmo....",
		"...ommmmmmmmo...",
		"..oddddddddddo..",
		"..oddddddddddo..",
		"..ommmmmmmmmmo..",
		"..omllllllllmo..",
		"...ommmmmmmmo...",
		"....oooooooo....",
		"...ss..ss..ss...",
		"................",
		"................",
	];

	private static readonly string[] VehicleBack =
	[
		"................",
		"................",
		"....oooooooo....",
		"....offffffo....",
		"....ofoffofo....",
		"....offffffo....",
		"...oommmmmmoo...",
		"..oddddddddddo..",
		"..oddddddddddo..",
		"..ommmmmmmmmmo..",
		"..ommmmmmmmmmo..",
		"...ommmmmmmmo...",
		"....oooooooo....",
		"...ss..ss..ss...",
		"................",
		"................",
	];

	private static readonly string[] PierRows =
	[
		"................",
		"..........oo....",
		"..........orro..",
		"..........orrro.",
		"..........oo....",
		"..........ob....",
		"..........ob....",
		"..oooooooooboo..",
		".ommmmmmmmmmmmo.",
		".odddddddddddddo",
		".ommmmmmmmmmmmo.",
		"..ob......ob....",
		"..ob......ob....",
		"..ob......ob....",
		"..oo......oo....",
		"................",
	];

	// A generated vehicle frame: the hull colored from the vehicle's id, frame 1 a pixel lower, with the spray between.
	public static Pixels VehicleTemplate(string id, Direction facing, int frame)
	{
		uint seed = Noise.Hash(id);
		Color body = FromHsv((seed & 0xff) / 255f, 0.5f, 0.8f);
		Color Palette(char c) => c switch
		{
			'o' => Outline,
			'm' => body,
			'l' => Color.Lerp(body, Color.White, 0.5f),
			'd' => Shade(body, 0.6f),
			'w' => new Color(178, 224, 240),
			'f' => new Color(150, 150, 164),
			's' => frame == 0 ? new Color(224, 244, 252) : Color.Transparent,
			_ => Color.Magenta,
		};
		string[] rows = facing switch
		{
			Direction.North => VehicleBack,
			Direction.South or Direction.None => VehicleFront,
			_ => VehicleSide,
		};
		Pixels p = Pixels.FromTemplate(rows, Palette);
		if (facing == Direction.West)
		{
			p = p.MirrorX();
		}
		return frame == 1 ? p.Shift(1) : p;
	}

	// The vehicle as the field draws it now: its sheet's frame run for the facing when the manifest gives one, else the
	// generated bob.
	public int VehicleFrame(string id, Direction facing, int clockMs)
	{
		Direction d = facing == Direction.None ? Direction.South : facing;
		if (_vehicleSheets.TryGetValue(id, out var sheet))
		{
			int[] frames = sheet.Entry.Facings[d];
			return sheet.Images[frames[clockMs / sheet.Entry.Ms % frames.Length]];
		}
		return GeneratedVehicle(id, d, clockMs / VehicleFrameMs % 2);
	}

	public int GeneratedVehicle(string id, Direction facing, int frame)
	{
		if (!_vehicles.TryGetValue((id, facing, frame), out int image))
		{
			image = Add(VehicleTemplate(id, facing, frame));
			_vehicles[(id, facing, frame)] = image;
		}
		return image;
	}

	// The pier a port stands on.
	public int Pier
	{
		get
		{
			if (_pier < 0)
			{
				Color Palette(char c) => c switch
				{
					'o' => Outline,
					'm' => new Color(168, 120, 72),
					'd' => new Color(112, 78, 46),
					'b' => new Color(92, 64, 40),
					'r' => new Color(224, 86, 70),
					_ => Color.Magenta,
				};
				_pier = Add(Pixels.FromTemplate(PierRows, Palette));
			}
			return _pier;
		}
	}

	private readonly Dictionary<string, Figure> _vehicleFigures = [];

	// A vehicle in battle, facing the enemies on the left: the manifest's party figure for its id, else its west frames
	// bobbing.
	public Figure VehicleFigure(string id)
	{
		if (FromFile("figure:party:" + id) && _figures.TryGetValue((id, false), out Figure? file))
		{
			return file;
		}
		if (!_vehicleFigures.TryGetValue(id, out Figure? figure))
		{
			figure = new Figure([GeneratedVehicle(id, Direction.West, 0), GeneratedVehicle(id, Direction.West, 1)], new FrameRun([0, 1], [VehicleFrameMs, VehicleFrameMs]),
				new Dictionary<string, FrameRun> { ["attack"] = new([1, 0], [140, 140]) }, _figureShadows);
			_vehicleFigures[id] = figure;
		}
		return figure;
	}

	private void LoadVehicleFiles(AssetManifest m)
	{
		foreach (VehicleEntry v in m.Vehicles)
		{
			_vehicleSheets[v.Id] = (v, Cut(v.Image));
			_fromFile["vehicle:" + v.Id] = v.Image.Path;
		}
		if (m.Sprites.TryGetValue("pier", out string? pier))
		{
			_pier = Whole(pier);
		}
	}
}
