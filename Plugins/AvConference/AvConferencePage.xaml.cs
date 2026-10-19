using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace YanJi.Plugin.AvConference;

/// <summary>「影音会议」页面：摄像头（WinRT 预览）/ 麦克风（NAudio 电平+录制回放）/ 扬声器（NAudio 播放试音曲）。
/// 进入页面初始化设备并开始预览/监听；离开页面（Unloaded，含关程序）释放全部设备——摄像头指示灯随之熄灭。
/// 通过约定同其它模块：确认无故障 → PassChanged(true) → 导航圆点变绿 + 自动跳下一模块。</summary>
public partial class AvConferencePage : UserControl, YanJi.PluginSdk.IModulePage
{
    const string MusicFileName = "不死のバイオレット - 幻月遠征隊.mp3";
    const int RecordLimitSec = 15;

    readonly CameraService _camera = new();
    readonly AudioService _audio = new();
    readonly string _recordFile = Path.Combine(Path.GetTempPath(), $"yanji_mic_test_{Guid.NewGuid():N}.wav");
    readonly DispatcherTimer _levelTimer;
    readonly DispatcherTimer _recordLimitTimer;
    readonly SemaphoreSlim _initGate = new(1, 1);

    List<(string Id, string Name)> _camDevices = [], _micDevices = [], _spkDevices = [];
    WriteableBitmap? _wb;
    CancellationTokenSource? _sessionCts;
    Action<int, int, byte[]>? _frameHandler;
    Action<string>? _cameraFailureHandler;
    int _session;
    int _previewGeneration;
    int _playbackGeneration;
    bool _initializing;
    bool _recording;
    bool _inputReady;
    bool _hasRecording;
    bool _cameraFailed;

    /// <summary>核对完成状态变化（true = 已通过，false = 回到待测试）；MainWindow 据此同步导航圆点并自动跳转。</summary>
    public event Action<bool>? PassChanged;

    /// <summary>当前是否已确认通过。</summary>
    public bool Passed { get; private set; }

    public AvConferencePage()
    {
        InitializeComponent();
        _levelTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
        _levelTimer.Tick += (_, _) =>
        {
            if (IsLoaded && _sessionCts != null) MicLevel.Value = _inputReady ? _audio.Peak * 100 : 0;
        };
        _recordLimitTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(RecordLimitSec) };
        _recordLimitTimer.Tick += (_, _) => StopRecord();
        SpkBox.SelectionChanged += SpkBox_Changed;
        Loaded += (_, _) =>
        {
            _sessionCts?.Cancel();
            _sessionCts?.Dispose();
            _sessionCts = new CancellationTokenSource();
            int session = Interlocked.Increment(ref _session);
            CameraBox.IsEnabled = MicBox.IsEnabled = SpkBox.IsEnabled = false;
            RecordButton.IsEnabled = PlayRecButton.IsEnabled = PlayMusicButton.IsEnabled = false;
            _ = InitAsync(session, _sessionCts.Token);
        };
        Unloaded += (_, _) => Cleanup();
    }

    // ═══ 初始化 / 资源释放 ═══

    /// <summary>枚举三类设备填充下拉框，并开启摄像头预览与麦克风监听。</summary>
    async Task InitAsync(int session, CancellationToken cancellationToken)
    {
        bool entered = false;
        try
        {
            // 回页请求等待旧初始化收尾，不丢请求，也不轮询。
            await _initGate.WaitAsync(cancellationToken);
            entered = true;
            if (!IsCurrentSession(session)) return;
            CameraError.Text = "正在初始化摄像头…";
            CameraError.Visibility = Visibility.Visible;
            var micDevices = AudioService.GetInputDevices();
            var spkDevices = AudioService.GetOutputDevices();
            List<(string Id, string Name)> camDevices;
            string? cameraError = null;
            try { camDevices = await CameraService.GetDevicesAsync(); }
            catch (Exception ex)
            {
                camDevices = [];
                cameraError = "摄像头枚举失败：" + ex.Message;
            }
            if (!IsCurrentSession(session)) return;
            var previousDevices = (CurrentCamId, CurrentMicId, CurrentSpkId);
            _initializing = true;
            try
            {
                _micDevices = micDevices;
                _spkDevices = spkDevices;
                _camDevices = camDevices;
                FillBox(MicBox, _micDevices);
                FillBox(SpkBox, _spkDevices);
                FillBox(CameraBox, _camDevices);
            }
            finally { _initializing = false; }
            if (previousDevices != (CurrentCamId, CurrentMicId, CurrentSpkId)) ResetPassed();
            if (!IsCurrentSession(session)) return;
            PlayMusicButton.IsEnabled = _spkDevices.Count > 0;
            MusicStatus.Text = _spkDevices.Count == 0 ? "未检测到音频输出设备"
                : !File.Exists(MusicPath()) ? "未找到试音曲文件（应随程序一同发布）" : "";
            if (CurrentMicId != null) StartInput(CurrentMicId);
            else
            {
                _inputReady = false;
                RecordButton.IsEnabled = false;
                MicStatus.Text = "未检测到麦克风";
            }
            _levelTimer.Start();
            await StartPreviewAsync(CurrentCamId, session, cancellationToken);
            if (!IsCurrentSession(session)) return;
            if (cameraError != null) CameraError.Text = cameraError;
        }
        catch (OperationCanceledException) { /* 离页或已被新请求替代 */ }
        catch (Exception ex)
        {
            if (IsCurrentSession(session))
            {
                CameraError.Text = "影音设备初始化失败：" + ex.Message;
                CameraError.Visibility = Visibility.Visible;
            }
        }
        finally { if (entered) _initGate.Release(); }
    }

    bool IsCurrentSession(int session) => IsLoaded && session == _session &&
        _sessionCts is { IsCancellationRequested: false };

    static void FillBox(ComboBox box, List<(string Id, string Name)> devices)
    {
        box.Items.Clear();
        foreach (var d in devices) box.Items.Add(d.Name);
        if (devices.Count > 0) box.SelectedIndex = 0;
        box.IsEnabled = devices.Count > 0;
    }

    string? CurrentCamId => CameraBox.SelectedIndex >= 0 && CameraBox.SelectedIndex < _camDevices.Count
        ? _camDevices[CameraBox.SelectedIndex].Id : null;
    string? CurrentMicId => MicBox.SelectedIndex >= 0 && MicBox.SelectedIndex < _micDevices.Count
        ? _micDevices[MicBox.SelectedIndex].Id : null;
    string? CurrentSpkId => SpkBox.SelectedIndex >= 0 && SpkBox.SelectedIndex < _spkDevices.Count
        ? _spkDevices[SpkBox.SelectedIndex].Id : null;

    static string MusicPath() => Path.Combine(
            Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location)!, MusicFileName);

    /// <summary>离开页面/关闭程序：停预览（灭灯）、停捕获与回放、清录音临时文件。</summary>
    void Cleanup()
    {
        // 在任何异步释放前同步失效，已排队的帧/播放回调也不能再写本页。
        Interlocked.Increment(ref _session);
        _sessionCts?.Cancel();
        _sessionCts?.Dispose();
        _sessionCts = null;
        DetachCameraHandlers();
        _ = StopCameraAsync();
        _levelTimer.Stop();
        _recordLimitTimer.Stop();
        _recording = false;
        _inputReady = false;
        _hasRecording = false;
        ++_playbackGeneration;
        RecordButton.Content = "录制";
        RecordButton.IsEnabled = false;
        PlayMusicButton.Content = "播放音乐";
        PlayMusicButton.IsEnabled = false;
        PlayRecButton.Content = "回放";
        PlayRecButton.IsEnabled = false;
        MicLevel.Value = 0;
        MicStatus.Text = "";
        MusicStatus.Text = "";
        _wb = null;
        PreviewImage.Source = null;
        try { _audio.StopInput(); }
        catch (Exception ex) { MicStatus.Text = "停止麦克风失败：" + ex.Message; }
        try { _audio.StopPlay(); }
        catch (Exception ex) { MusicStatus.Text = "停止播放失败：" + ex.Message; }
        try { if (File.Exists(_recordFile)) File.Delete(_recordFile); }
        catch { /* 仅清理本页唯一临时文件，删不掉不致命 */ }
    }

    async Task StopCameraAsync()
    {
        try { await _camera.StopAsync(); }
        catch (Exception ex) { System.Diagnostics.Trace.WriteLine("停止摄像头失败：" + ex.Message); }
    }

    // ═══ 摄像头 ═══

    void CameraBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (_initializing || !IsCurrentSession(_session)) return;
        int session = _session;
        var cancellationToken = _sessionCts!.Token;
        ResetPassed();
        _ = StartPreviewAsync(CurrentCamId, session, cancellationToken);
    }

    void DetachCameraHandlers()
    {
        ++_previewGeneration;
        if (_frameHandler != null) _camera.FrameReady -= _frameHandler;
        if (_cameraFailureHandler != null) _camera.Failed -= _cameraFailureHandler;
        _frameHandler = null;
        _cameraFailureHandler = null;
    }

    async Task StartPreviewAsync(string? deviceId, int session, CancellationToken cancellationToken)
    {
        if (!IsCurrentSession(session)) return;
        DetachCameraHandlers();
        int preview = _previewGeneration;
        _cameraFailed = false;
        _wb = null;
        PreviewImage.Source = null;
        CameraError.Visibility = Visibility.Visible;
        if (deviceId == null)
        {
            CameraError.Text = "未检测到摄像头";
            await StopCameraAsync();
            return;
        }
        CameraError.Text = "正在启动摄像头…";
        _frameHandler = (w, h, px) => OnCameraFrame(session, preview, w, h, px);
        _cameraFailureHandler = error =>
        {
            if (Dispatcher.HasShutdownStarted) return;
            Dispatcher.BeginInvoke(() =>
            {
                if (!IsCurrentSession(session) || preview != _previewGeneration) return;
                _cameraFailed = true;
                _wb = null;
                PreviewImage.Source = null;
                CameraError.Text = "摄像头采集失败：" + error;
                CameraError.Visibility = Visibility.Visible;
            });
        };
        _camera.FrameReady += _frameHandler;
        _camera.Failed += _cameraFailureHandler;
        try
        {
            await _camera.StartAsync(deviceId, cancellationToken);
            if (!IsCurrentSession(session) || preview != _previewGeneration) return;
            if (!_cameraFailed && _wb == null) CameraError.Text = "摄像头已启动，等待画面…";
        }
        catch (OperationCanceledException) { /* 离页或设备切换 */ }
        catch (Exception ex)
        {
            if (!IsCurrentSession(session) || preview != _previewGeneration) return;
            _cameraFailed = true;
            CameraError.Text = "摄像头不可用：" + ex.Message + "\n（可能被其它程序占用或未授权）";
            CameraError.Visibility = Visibility.Visible;
        }
    }

    /// <summary>摄像头帧回调（后台线程）：封送到 UI 线程，并再次核对页面会话。</summary>
    void OnCameraFrame(int session, int preview, int w, int h, byte[] px)
    {
        if (session != Volatile.Read(ref _session) || Dispatcher.HasShutdownStarted) return;
        Dispatcher.BeginInvoke(() =>
        {
            if (!IsCurrentSession(session) || preview != _previewGeneration || _cameraFailed) return;
            if (_wb == null || _wb.PixelWidth != w || _wb.PixelHeight != h)
            {
                _wb = new WriteableBitmap(w, h, 96, 96, PixelFormats.Pbgra32, null);
                PreviewImage.Source = _wb;
                CameraError.Visibility = Visibility.Collapsed;
            }
            _wb.WritePixels(new Int32Rect(0, 0, w, h), px, w * 4, 0);
        });
    }

    // ═══ 麦克风 ═══

    void MicBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (_initializing || !IsCurrentSession(_session)) return;
        ResetPassed();
        if (_recording) StopRecord();
        if (CurrentMicId != null) StartInput(CurrentMicId);
        else
        {
            _inputReady = false;
            RecordButton.IsEnabled = false;
            MicLevel.Value = 0;
            try { _audio.StopInput(); }
            catch (Exception ex) { MicStatus.Text = "停止麦克风失败：" + ex.Message; }
        }
    }

    void StartInput(string deviceId)
    {
        if (_recording) StopRecord();
        _inputReady = false;
        RecordButton.IsEnabled = false;
        MicLevel.Value = 0;
        try
        {
            _audio.StartInput(deviceId);
            _inputReady = true;
            RecordButton.IsEnabled = true;
            MicStatus.Text = "";
        }
        catch (Exception ex)
        {
            MicStatus.Text = "麦克风不可用：" + ex.Message;
        }
    }

    void RecordButton_Click(object sender, RoutedEventArgs e)
    {
        if (!IsCurrentSession(_session)) return;
        if (_recording) { StopRecord(); return; }
        if (!_inputReady) { MicStatus.Text = "麦克风监听未启动，无法录制"; return; }
        _hasRecording = false;
        PlayRecButton.IsEnabled = false;
        try
        {
            StopPlayback();
            _audio.StartRecord(_recordFile);
            _recording = true;
            RecordButton.Content = "停止录制";
            MicStatus.Text = "录制中…";
            _recordLimitTimer.Start();
        }
        catch (Exception ex)
        {
            MicStatus.Text = "无法录制：" + ex.Message;
        }
    }

    void StopRecord()
    {
        if (!_recording) return;
        _recordLimitTimer.Stop();
        _hasRecording = false;
        try
        {
            _audio.StopRecord();
            _hasRecording = File.Exists(_recordFile) && new FileInfo(_recordFile).Length > 44;
            MicStatus.Text = _hasRecording ? "已录制，可点「回放」试听" : "未获得有效录音，请检查麦克风后重试";
        }
        catch (Exception ex) { MicStatus.Text = "录制保存失败：" + ex.Message; }
        finally
        {
            _recording = false;
            RecordButton.Content = "录制";
            PlayRecButton.IsEnabled = _hasRecording;
        }
    }

    void PlayRecButton_Click(object sender, RoutedEventArgs e)
    {
        if (!IsCurrentSession(_session)) return;
        if (_audio.IsPlaying) { StopPlayback(); return; }
        if (_recording || !_hasRecording || !File.Exists(_recordFile)) { MicStatus.Text = "还没有录音"; return; }
        PlayRecButton.Content = "停止";
        int session = _session, playback = ++_playbackGeneration;
        try { _audio.Play(_recordFile, CurrentSpkId, () => OnPlaybackStopped(session, playback)); }
        catch (Exception ex)
        {
            StopPlayback();
            MicStatus.Text = "回放失败：" + ex.Message;
        }
    }

    // ═══ 扬声器 ═══

    void SpkBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (_initializing || !IsCurrentSession(_session)) return;
        ResetPassed();
        StopPlayback();
    }

    void PlayMusicButton_Click(object sender, RoutedEventArgs e)
    {
        if (!IsCurrentSession(_session)) return;
        if (_audio.IsPlaying) { StopPlayback(); return; }
        if (!File.Exists(MusicPath()))
        {
            MusicStatus.Text = "未找到试音曲文件（应随程序一同发布）";
            return;
        }
        PlayMusicButton.Content = "停止";
        int session = _session, playback = ++_playbackGeneration;
        try
        {
            _audio.Play(MusicPath(), CurrentSpkId, () => OnPlaybackStopped(session, playback));
            MusicStatus.Text = "播放中…";
        }
        catch (Exception ex)
        {
            StopPlayback();
            MusicStatus.Text = "播放失败：" + ex.Message;
        }
    }

    void StopPlayback()
    {
        ++_playbackGeneration;
        try { _audio.StopPlay(); }
        catch (Exception ex) { MusicStatus.Text = "停止播放失败：" + ex.Message; }
        PlayRecButton.Content = "回放";
        PlayMusicButton.Content = "播放音乐";
        if (MusicStatus.Text == "播放中…") MusicStatus.Text = "";
    }

    /// <summary>回放/播放结束（自然结束或手动停止）复位按钮文案（AudioService 回调在线程池线程）。</summary>
    void OnPlaybackStopped(int session, int playback)
    {
        if (session != Volatile.Read(ref _session) || Dispatcher.HasShutdownStarted) return;
        Dispatcher.BeginInvoke(() =>
        {
            if (!IsCurrentSession(session) || playback != _playbackGeneration) return;
            PlayRecButton.Content = "回放";
            PlayMusicButton.Content = "播放音乐";
            if (MusicStatus.Text == "播放中…") MusicStatus.Text = "";
        });
    }

    // ═══ 通过态 ═══

    void PassButton_Click(object sender, RoutedEventArgs e)
    {
        if (IsCurrentSession(_session)) SetPassed(true);
    }

    void ResetPassed()
    {
        PassButton.IsEnabled = true;
        if (Passed) SetPassed(false);
    }

    /// <summary>更新测试状态行（通过 = 绿色「✓ 已确认通过」）并通知导航圆点/自动跳转。</summary>
    void SetPassed(bool passed)
    {
        Passed = passed;
        PassButton.IsEnabled = !passed;
        if (passed)
        {
            StatusText.Text = "✓ 已确认通过";
            StatusText.Foreground = Brushes.ForestGreen;
        }
        else
        {
            StatusText.Text = "待测试";
            StatusText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
        }
        PassChanged?.Invoke(passed);
    }
}
