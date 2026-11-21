using FontStashSharp;

using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;

using Psiv.Session;

namespace Psiv.Sample;

// The windowed front end: MonoGame owns the window, the clock, the keyboard, the mouse and the gamepad;
// SampleController does the rest. One session update per frame, driven from Update with the frame's elapsed
// milliseconds. The controller draws a frame at the layout's logical resolution (1280x720 by default), which goes into the window letterboxed (Letterbox): the window
// can be resized, and F11 toggles borderless fullscreen; clicks are mapped back to the frame's pixels.
public sealed class SampleGame : Game
{
	private readonly GraphicsDeviceManager _graphics;
	private readonly GameSession _session;
	public const string ControlsFile = "controls.json";

	private SampleController _controller;
	private readonly RecordingAudio _audio;
	private readonly InputState _input = new();
	private readonly string? _fontPath;
	private readonly int _exitAfterMs;
	private readonly string? _scriptPath;
	private readonly string? _screenshot;
	private readonly string? _controlsPath;
	private readonly string? _saves;
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
	private bool _padConnected;
	private RenderTarget2D? _target;
	private bool _mouseWasDown;
	private bool _fullscreenKeyWasDown;
	private Letterbox _box = Letterbox.Fit(Letterbox.BaseWidth, Letterbox.BaseHeight);
	private readonly GameData _data;
	private readonly string? _assets;
	private readonly bool _dev;
	private readonly string? _dataDir;
	private readonly BattleLayout? _battleLayout;
	private readonly FileSystemWatcher? _watcher;
	// When the watcher last saw a change in the asset directory; the reload waits for a quiet moment after it.
	private long _assetChangeTicks;
	private const int WatchQuietMs = 300;

	public SampleGame(GameSession session, GameData data, string? fontPath, int exitAfterMs, string? saves, string? scriptPath = null, string? screenshot = null, bool mute = false,
		(int Width, int Height)? windowSize = null, bool fullscreen = false, string? assets = null, bool watchAssets = false, bool dev = false, string? dataDir = null,
		BattleLayout? battleLayout = null)
	{
		_dev = dev;
		_battleLayout = battleLayout;
		_dataDir = dataDir;
		_data = data;
		_assets = assets;
		if (watchAssets && assets is not null)
		{
			_watcher = new FileSystemWatcher(assets) { IncludeSubdirectories = true, EnableRaisingEvents = true };
			FileSystemEventHandler changed = (_, _) => Interlocked.Exchange(ref _assetChangeTicks, DateTime.UtcNow.Ticks);
			_watcher.Changed += changed;
			_watcher.Created += changed;
			_watcher.Deleted += changed;
			_watcher.Renamed += (_, _) => Interlocked.Exchange(ref _assetChangeTicks, DateTime.UtcNow.Ticks);
		}
		_saves = saves;
		_controlsPath = saves is null ? null : Path.Combine(saves, ControlsFile);
		_session = session;
		_fontPath = fontPath;
		_exitAfterMs = exitAfterMs;
		_scriptPath = scriptPath;
		_screenshot = screenshot;
		_audio = MonoGameAudio.Create(mute, Console.WriteLine);
		_controller = new SampleController(session, data, Console.WriteLine, _controlsPath, _audio, assets, dev, dataDir, battleLayout);
		_graphics = new GraphicsDeviceManager(this)
		{
			PreferredBackBufferWidth = windowSize?.Width ?? Letterbox.BaseWidth,
			PreferredBackBufferHeight = windowSize?.Height ?? Letterbox.BaseHeight,
			HardwareModeSwitch = false,
			IsFullScreen = fullscreen,
		};
		Window.Title = session.GameInfo().Title;
		Window.AllowUserResizing = true;
		Window.ClientSizeChanged += (_, _) =>
		{
			if (!_graphics.IsFullScreen && Window.ClientBounds.Width > 0 && Window.ClientBounds.Height > 0)
			{
				_graphics.PreferredBackBufferWidth = Window.ClientBounds.Width;
				_graphics.PreferredBackBufferHeight = Window.ClientBounds.Height;
				_graphics.ApplyChanges();
			}
		};
		IsMouseVisible = true;
	}

	// A new window size, or borderless fullscreen, from the window or a script's `window` line.
	private void Resize(int width, int height, bool fullscreen)
	{
		_graphics.IsFullScreen = fullscreen;
		if (!fullscreen)
		{
			_graphics.PreferredBackBufferWidth = width;
			_graphics.PreferredBackBufferHeight = height;
		}
		_graphics.ApplyChanges();
	}

	protected override void LoadContent()
	{
		_batch = new SpriteBatch(GraphicsDevice);
		_fonts = Fonts.Load(_fontPath);
		_surface = new MonoGameSurface(GraphicsDevice, _batch, _fonts);
		if (_scriptPath is not null)
		{
			_player = new ScriptPlayer(_session, _controller, _scriptPath, () => [], () => _frame, _shots.Add,
				() => _controller = new SampleController(_session, _data, Console.WriteLine, _controlsPath, _audio, _assets, _dev, _dataDir, _battleLayout), _saves, Resize);
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
		_box = Fit();
		if (_player is not null)
		{
			InputFrame next = _player.Next();
			_input.Set(next.Keys, next.Buttons, click: next.Click);
		}
		else
		{
			GamePadState pad = GamePad.GetState(PlayerIndex.One);
			if (pad.IsConnected != _padConnected)
			{
				_padConnected = pad.IsConnected;
				Console.WriteLine(_padConnected ? "gamepad connected" : "gamepad disconnected");
			}
			KeyboardState keys = Keyboard.GetState();
			bool fullscreenKey = keys.IsKeyDown(Keys.F11);
			if (fullscreenKey && !_fullscreenKeyWasDown)
			{
				Resize(_graphics.PreferredBackBufferWidth, _graphics.PreferredBackBufferHeight, !_graphics.IsFullScreen);
			}
			_fullscreenKeyWasDown = fullscreenKey;
			MouseState mouse = Mouse.GetState();
			bool mouseDown = mouse.LeftButton == ButtonState.Pressed && IsActive;
			(int X, int Y)? click = mouseDown && !_mouseWasDown ? _box.ToBase(mouse.X, mouse.Y) : null;
			_mouseWasDown = mouseDown;
			_input.Set(keys.GetPressedKeys(), Pad.ButtonsDown(pad), _padConnected, click);
		}
		long changedAt = Interlocked.Read(ref _assetChangeTicks);
		if (changedAt != 0 && (DateTime.UtcNow.Ticks - changedAt) / TimeSpan.TicksPerMillisecond >= WatchQuietMs)
		{
			Interlocked.CompareExchange(ref _assetChangeTicks, 0, changedAt);
			_controller.ReloadAssets();
		}
		_frame++;
		// The session takes whole milliseconds; the remainder carries into the next frame. A script's frames take the
		// headless frame time, so its screenshots show the moments the headless run of the same script checks.
		_carryMs += gameTime.ElapsedGameTime.TotalMilliseconds;
		int dt = _player is not null ? Headless.FrameMs : (int)_carryMs;
		_carryMs -= _player is not null ? _carryMs : dt;
		_controller.Update(dt, _input);
		_elapsedMs += gameTime.ElapsedGameTime.TotalMilliseconds;
		bool scriptDone = _player is not null && _player.Done && _exitAfterMs == 0;
		if (_controller.QuitRequested || scriptDone || (_exitAfterMs > 0 && _elapsedMs >= _exitAfterMs))
		{
			_exiting = true;
		}
		base.Update(gameTime);
	}

	private Letterbox Fit() => Letterbox.Fit(GraphicsDevice.PresentationParameters.BackBufferWidth, GraphicsDevice.PresentationParameters.BackBufferHeight,
		_controller.LogicalSize.Width, _controller.LogicalSize.Height, _controller.Scaling);

	protected override void Draw(GameTime gameTime)
	{
		// The layout's resolution can change with an asset reload.
		(int width, int height) = _controller.LogicalSize;
		if (_target is null || _target.Width != width || _target.Height != height)
		{
			_target?.Dispose();
			_target = new RenderTarget2D(GraphicsDevice, width, height);
		}
		GraphicsDevice.SetRenderTarget(_target);
		GraphicsDevice.Clear(Color.Black);
		_batch!.Begin(samplerState: SamplerState.PointClamp);
		_controller.Draw(_surface!);
		_batch.End();
		GraphicsDevice.SetRenderTarget(null);
		GraphicsDevice.Clear(Color.Black);
		_box = Fit();
		bool point = _controller.Scaling.Filter switch { "point" => true, "linear" => false, _ => _box.Scale >= 1f };
		_batch.Begin(samplerState: point ? SamplerState.PointClamp : SamplerState.LinearClamp);
		_batch.Draw(_target, new Rectangle(_box.X, _box.Y, _box.Width, _box.Height), Color.White);
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
		_watcher?.Dispose();
		_surface?.Dispose();
		_target?.Dispose();
		(_audio as IDisposable)?.Dispose();
		_fonts?.Dispose();
		_batch?.Dispose();
		base.UnloadContent();
	}
}
