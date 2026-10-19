using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Devices.Enumeration;
using Windows.Graphics.Imaging;
using Windows.Media.Capture;
using Windows.Media.Capture.Frames;
using Windows.Media.MediaProperties;
using Windows.Storage.Streams;

namespace YanJi.Plugin.AvConference;

/// <summary>摄像头采集：WinRT Windows.Media.Capture（Windows 自带，无第三方依赖/许可证负担）。
/// MediaCapture + MediaFrameReader 抓 BGRA8 帧回调给页面显示；StopAsync/Dispose 释放后摄像头指示灯即灭。
/// 无权限/被占用/无设备时 InitializeAsync 等会抛异常，调用方捕获后提示即可。</summary>
public class CameraService : IDisposable
{
    readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    MediaCapture? _capture;
    MediaFrameReader? _reader;
    MediaCaptureFailedEventHandler? _captureFailedHandler;
    int _generation;
    int _activeGeneration;
    TaskCompletionSource<bool>? _firstFrame;
    string? _frameDiagnostic;

    /// <summary>新帧（宽 / 高 / BGRA8 像素字节；后台线程触发，UI 侧自行封送）。</summary>
    public event Action<int, int, byte[]>? FrameReady;
    public event Action<string>? Failed;

    /// <summary>枚举视频输入设备（Id, 名称）。</summary>
    public static async Task<List<(string Id, string Name)>> GetDevicesAsync()
    {
        var infos = await DeviceInformation.FindAllAsync(DeviceClass.VideoCapture);
        return infos.Select(i => (i.Id, i.Name)).ToList();
    }

    /// <summary>开启指定设备预览（重复调用先停旧的）。失败抛异常（占用/无权限/无帧源）。</summary>
    public Task StartAsync(string deviceId) => StartAsync(deviceId, CancellationToken.None);

    public async Task StartAsync(string deviceId, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        int generation = Interlocked.Increment(ref _generation);
        await _lifecycleGate.WaitAsync(cancellationToken);
        MediaCapture? capture = null;
        MediaFrameReader? reader = null;
        string? captureError = null;
        MediaCaptureFailedEventHandler failedHandler = (sender, args) =>
        {
            Volatile.Write(ref captureError, $"{args.Message}（错误码 0x{args.Code:X8}）");
            OnCaptureFailed(sender, args);
        };
        try
        {
            CheckRequest(generation, cancellationToken);
            await StopCoreAsync();
            CheckRequest(generation, cancellationToken);
            capture = new MediaCapture();
            capture.Failed += failedHandler;
            await capture.InitializeAsync(new MediaCaptureInitializationSettings
            {
                VideoDeviceId = deviceId,
                StreamingCaptureMode = StreamingCaptureMode.Video,
                // CPU 内存帧：SoftwareBitmap 直接可拷，省掉 GPU 回读
                MemoryPreference = MediaCaptureMemoryPreference.Cpu,
            });
            CheckRequest(generation, cancellationToken);
            if (Volatile.Read(ref captureError) is { } initializeError)
                throw new InvalidOperationException("摄像头采集失败：" + initializeError);
            var source = capture.FrameSources.Values.FirstOrDefault(s =>
                s.Info.MediaStreamType == MediaStreamType.VideoPreview)
                ?? capture.FrameSources.Values.FirstOrDefault();
            if (source == null)
                throw new InvalidOperationException("该摄像头没有可用的预览帧源");
            reader = await capture.CreateFrameReaderAsync(source, MediaEncodingSubtypes.Bgra8);
            CheckRequest(generation, cancellationToken);
            reader.FrameArrived += OnFrame;
            var status = await reader.StartAsync();
            CheckRequest(generation, cancellationToken);
            if (status == MediaFrameReaderStartStatus.OutputFormatNotSupported)
            {
                // 部分设备拒绝驱动侧 BGRA8 转换；改读原生格式，OnFrame 在 CPU 上转换。
                var unsupported = reader;
                reader = null;
                await ReleaseAsync(unsupported, null, null);
                CheckRequest(generation, cancellationToken);
                reader = await capture.CreateFrameReaderAsync(source);
                CheckRequest(generation, cancellationToken);
                reader.FrameArrived += OnFrame;
                status = await reader.StartAsync();
                CheckRequest(generation, cancellationToken);
            }
            if (status != MediaFrameReaderStartStatus.Success)
                throw new InvalidOperationException("摄像头预览启动失败：" + status);
            var firstFrame = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            Volatile.Write(ref _frameDiagnostic, null);
            _firstFrame = firstFrame;
            _capture = capture;
            _reader = reader;
            _captureFailedHandler = failedHandler;
            Volatile.Write(ref _activeGeneration, generation);
            capture = null;
            reader = null;
            try
            {
                if (Volatile.Read(ref captureError) is { } startError)
                    throw new InvalidOperationException("摄像头采集失败：" + startError);
                // StartAsync 成功不保证设备真的交付画面；无帧必须明确失败并释放，不能无限等待。
                try { await firstFrame.Task.WaitAsync(TimeSpan.FromSeconds(6), cancellationToken); }
                catch (TimeoutException)
                {
                    throw new InvalidOperationException("摄像头启动后6秒未收到可用画面：" +
                        (Volatile.Read(ref _frameDiagnostic) ?? "设备未交付视频帧，请检查设备占用、隐私权限或驱动"));
                }
                CheckRequest(generation, cancellationToken);
            }
            catch
            {
                await StopCoreAsync();
                throw;
            }
        }
        finally
        {
            try { await ReleaseAsync(reader, capture, failedHandler); }
            finally { _lifecycleGate.Release(); }
        }
    }

    void CheckRequest(int generation, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (generation != Volatile.Read(ref _generation))
            throw new OperationCanceledException("摄像头请求已失效");
    }

    void OnCaptureFailed(MediaCapture sender, MediaCaptureFailedEventArgs args)
    {
        var failed = Failed;
        if (ReferenceEquals(sender, _capture) &&
            Volatile.Read(ref _activeGeneration) == Volatile.Read(ref _generation))
        {
            string error = $"{args.Message}（错误码 0x{args.Code:X8}）";
            _firstFrame?.TrySetException(new InvalidOperationException("摄像头采集失败：" + error));
            failed?.Invoke(error);
        }
    }

    void OnFrame(MediaFrameReader sender, MediaFrameArrivedEventArgs args)
    {
        var frameReady = FrameReady;
        int generation = Volatile.Read(ref _activeGeneration);
        if (!ReferenceEquals(sender, _reader) || generation != Volatile.Read(ref _generation)) return;
        try
        {
            using var frame = sender.TryAcquireLatestFrame();
            using var sb = frame?.VideoMediaFrame?.SoftwareBitmap;
            if (sb == null)
            {
                Volatile.Write(ref _frameDiagnostic, "视频帧不包含可读取的CPU位图");
                return;
            }
            using var converted = sb.BitmapPixelFormat != BitmapPixelFormat.Bgra8
                ? SoftwareBitmap.Convert(sb, BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied) : null;
            var bmp = converted ?? sb;
            int w = bmp.PixelWidth, h = bmp.PixelHeight;
            var buf = new Windows.Storage.Streams.Buffer((uint)(w * h * 4));
            bmp.CopyToBuffer(buf);
            if (ReferenceEquals(sender, _reader) && generation == Volatile.Read(ref _generation))
            {
                frameReady?.Invoke(w, h, buf.ToArray());
                _firstFrame?.TrySetResult(true);
            }
        }
        catch (Exception ex)
        {
            if (ReferenceEquals(sender, _reader) && generation == Volatile.Read(ref _generation))
                Volatile.Write(ref _frameDiagnostic, "视频帧转换失败：" + ex.Message);
        }
    }

    /// <summary>停止预览并释放设备（指示灯熄灭）。幂等。</summary>
    public async Task StopAsync()
    {
        int generation = Interlocked.Increment(ref _generation);
        await _lifecycleGate.WaitAsync();
        try
        {
            // 只释放本次 Stop 之前的设备；更新 Start 即使先获得锁也不会被误停。
            if (Volatile.Read(ref _activeGeneration) <= generation) await StopCoreAsync();
        }
        finally { _lifecycleGate.Release(); }
    }

    async Task StopCoreAsync()
    {
        Volatile.Write(ref _activeGeneration, 0);
        _firstFrame = null;
        var reader = _reader; _reader = null;
        var capture = _capture; _capture = null;
        var failedHandler = _captureFailedHandler; _captureFailedHandler = null;
        await ReleaseAsync(reader, capture, failedHandler);
    }

    async Task ReleaseAsync(MediaFrameReader? reader, MediaCapture? capture, MediaCaptureFailedEventHandler? failedHandler)
    {
        try
        {
            if (reader != null)
            {
                try
                {
                    reader.FrameArrived -= OnFrame;
                    await reader.StopAsync();
                }
                catch { /* 未启动或已停止，仍须释放 */ }
                finally { reader.Dispose(); }
            }
        }
        finally
        {
            if (capture != null)
            {
                try { if (failedHandler != null) capture.Failed -= failedHandler; }
                finally { capture.Dispose(); }
            }
        }
    }

    public void Dispose() => _ = StopAsync();
}
