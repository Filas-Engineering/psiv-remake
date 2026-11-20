using FontStashSharp;

using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// The windowed front end: MonoGame owns the window, the clock and the keyboard; SampleController does the
// rest. One session update per frame, driven from Update with the frame's elapsed milliseconds.
public sealed class SampleGame : Game
{
	private readonly GraphicsDeviceManager _graphics;
	private readonly GameSession _session;
	private readonly SampleController _controller;
	private readonly InputState _input = new();
	private readonly string? _fontPath;
	private readonly int _exitAfterMs;
	private readonly string? _scriptPath;
	private readonly string? _screenshot;
	private ScriptPlayer? _player;
	private readonly List<string> _shots = [];
	private int _frame;
	private SpriteBatch? _batch;
	private MonoGameSurface? _surface;
	private FontSystem? _fonts;
	private double _carryMs;
	private double _elapsedMs;
	private bool _exiting;
	private bool _finished;

	public SampleGame(GameSession session, string? fontPath, int exitAfterMs, string? scriptPath = null, string? screenshot = null)
	{
		_session = session;
		_fontPath = fontPath;
		_exitAfterMs = exitAfterMs;
		_scriptPath = scriptPath;
		_screenshot = screenshot;
		_controller = new SampleController(session, Console.WriteLine);
		_graphics = new GraphicsDeviceManager(this) { PreferredBackBufferWidth = 1280, PreferredBackBufferHeight = 720 };
		Window.Title = session.GameInfo().Title;
		IsMouseVisible = true;
	}

	protected override void LoadContent()
	{
		_batch = new SpriteBatch(GraphicsDevice);
		_fonts = Fonts.Load(_fontPath);
		_surface = new MonoGameSurface(GraphicsDevice, _batch, _fonts);
		if (_scriptPath is not null)
		{
			_player = new ScriptPlayer(_session, _controller, _scriptPath, () => [], () => _frame, _shots.Add);
		}
	}

	public int Failures => _player?.Failures ?? 0;

	protected override void Update(GameTime gameTime)
	{
		// A requested shot is of the frame the script asked for, so nothing moves until it is drawn.
		if (_shots.Count > 0)
		{
			base.Update(gameTime);
			return;
		}
		_input.Set(_player is not null ? _player.Next() : Keyboard.GetState().GetPressedKeys());
		_frame++;
		// The session takes whole milliseconds; the remainder carries into the next frame.
		_carryMs += gameTime.ElapsedGameTime.TotalMilliseconds;
		int dt = (int)_carryMs;
		_carryMs -= dt;
		_controller.Update(dt, _input);
		_elapsedMs += gameTime.ElapsedGameTime.TotalMilliseconds;
		bool scriptDone = _player is not null && _player.Done && _exitAfterMs == 0;
		if (_controller.QuitRequested || scriptDone || (_exitAfterMs > 0 && _elapsedMs >= _exitAfterMs))
		{
			_exiting = true;
		}
		base.Update(gameTime);
	}

	protected override void Draw(GameTime gameTime)
	{
		GraphicsDevice.Clear(Color.Black);
		_batch!.Begin(samplerState: SamplerState.PointClamp);
		_controller.Draw(_surface!);
		_batch.End();
		foreach (string shot in _shots)
		{
			SaveScreenshot(shot);
		}
		_shots.Clear();
		if (_exiting && !_finished)
		{
			_finished = true;
			if (_screenshot is not null)
			{
				SaveScreenshot(_screenshot);
			}
			_player?.Finish(_frame);
			Exit();
		}
		base.Draw(gameTime);
	}

	private void SaveScreenshot(string path)
	{
		int w = GraphicsDevice.PresentationParameters.BackBufferWidth;
		int h = GraphicsDevice.PresentationParameters.BackBufferHeight;
		var pixels = new Color[w * h];
		GraphicsDevice.GetBackBufferData(pixels);
		using var texture = new Texture2D(GraphicsDevice, w, h);
		texture.SetData(pixels);
		if (Path.GetDirectoryName(Path.GetFullPath(path)) is { } dir)
		{
			Directory.CreateDirectory(dir);
		}
		using FileStream file = File.Create(path);
		texture.SaveAsPng(file, w, h);
		Console.WriteLine($"screenshot: {path} ({w}x{h})");
	}

	protected override void UnloadContent()
	{
		_surface?.Dispose();
		_fonts?.Dispose();
		_batch?.Dispose();
		base.UnloadContent();
	}
}
