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
    MediaCapture? _capture;
    MediaFrameReader? _reader;

    /// <summary>新帧（宽 / 高 / BGRA8 像素字节；后台线程触发，UI 侧自行封送）。</summary>
    public event Action<int, int, byte[]>? FrameReady;

    /// <summary>枚举视频输入设备（Id, 名称）。</summary>
    public static async Task<List<(string Id, string Name)>> GetDevicesAsync()
    {
        var infos = await DeviceInformation.FindAllAsync(DeviceClass.VideoCapture);
        return infos.Select(i => (i.Id, i.Name)).ToList();
    }

    /// <summary>开启指定设备预览（重复调用先停旧的）。失败抛异常（占用/无权限/无帧源）。</summary>
    public async Task StartAsync(string deviceId)
    {
        await StopAsync();
        var capture = new MediaCapture();
        try
        {
            await capture.InitializeAsync(new MediaCaptureInitializationSettings
            {
                VideoDeviceId = deviceId,
                StreamingCaptureMode = StreamingCaptureMode.Video,
                // CPU 内存帧：SoftwareBitmap 直接可拷，省掉 GPU 回读
                MemoryPreference = MediaCaptureMemoryPreference.Cpu,
            });
        }
        catch
        {
            capture.Dispose();
            throw;
        }
        var source = capture.FrameSources.Values.FirstOrDefault(s =>
            s.Info.MediaStreamType == MediaStreamType.VideoPreview)
            ?? capture.FrameSources.Values.FirstOrDefault();
        if (source == null)
        {
            capture.Dispose();
            throw new InvalidOperationException("该摄像头没有可用的预览帧源");
        }
        _capture = capture;
        _reader = await capture.CreateFrameReaderAsync(source, MediaEncodingSubtypes.Bgra8);
        _reader.FrameArrived += OnFrame;
        await _reader.StartAsync();
    }

    void OnFrame(MediaFrameReader sender, MediaFrameArrivedEventArgs args)
    {
        using var frame = sender.TryAcquireLatestFrame();
        var sb = frame?.VideoMediaFrame?.SoftwareBitmap;
        if (sb == null) return;
        try
        {
            SoftwareBitmap bmp = sb;
            SoftwareBitmap? converted = null;
            if (bmp.BitmapPixelFormat != BitmapPixelFormat.Bgra8)
            {
                converted = SoftwareBitmap.Convert(bmp, BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied);
                bmp = converted;
            }
            int w = bmp.PixelWidth, h = bmp.PixelHeight;
            var buf = new Windows.Storage.Streams.Buffer((uint)(w * h * 4));
            bmp.CopyToBuffer(buf);
            converted?.Dispose();
            FrameReady?.Invoke(w, h, buf.ToArray());
        }
        catch { /* 单帧失败丢帧 */ }
    }

    /// <summary>停止预览并释放设备（指示灯熄灭）。幂等。</summary>
    public async Task StopAsync()
    {
        var reader = _reader; _reader = null;
        var capture = _capture; _capture = null;
        if (reader != null)
        {
            try { reader.FrameArrived -= OnFrame; await reader.StopAsync(); }
            catch { /* 已停止 */ }
            reader.Dispose();
        }
        capture?.Dispose();
    }

    public void Dispose() => _ = StopAsync();
}
