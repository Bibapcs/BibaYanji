using NAudio.CoreAudioApi;
using NAudio.Wave;

namespace YanJi.Plugin.AvConference;

/// <summary>音频采集/播放（NAudio 2.2.1，MIT 开源协议）：WASAPI 设备枚举、麦克风电平监听、
/// 录制 wav、指定输出设备回放（mp3/wav 均走 MediaFoundationReader）。</summary>
public class AudioService : IDisposable
{
    WasapiCapture? _capture;
    WaveFileWriter? _writer;
    WasapiOut? _output;
    MediaFoundationReader? _reader;
    readonly object _recordGate = new();
    readonly object _playGate = new();
    MMDevice? _inputDevice, _outputDevice;
    float _peak;
    bool _recording;

    /// <summary>当前输入电平峰值 0..1（页面用定时器轮询驱动电平条，避免事件刷屏）。</summary>
    public float Peak => _peak;

    /// <summary>是否有回放正在进行。</summary>
    public bool IsPlaying => _output != null;

    // ═══ 设备枚举 ═══

    public static List<(string Id, string Name)> GetInputDevices() => GetDevices(DataFlow.Capture);
    public static List<(string Id, string Name)> GetOutputDevices() => GetDevices(DataFlow.Render);

    static List<(string Id, string Name)> GetDevices(DataFlow flow)
    {
        try
        {
            using var en = new MMDeviceEnumerator();
            var result = new List<(string Id, string Name)>();
            foreach (var device in en.EnumerateAudioEndPoints(flow, DeviceState.Active))
                using (device) result.Add((device.ID, device.FriendlyName));
            return result;
        }
        catch { return []; }
    }

    // ═══ 麦克风：电平监听 + 录制（同一条捕获流复用） ═══

    /// <summary>开启指定录音设备的捕获（电平监听；StartRecord 后同时写 wav）。</summary>
    public void StartInput(string deviceId)
    {
        StopInput();
        using var en = new MMDeviceEnumerator();
        var device = en.GetDevice(deviceId);
        WasapiCapture? cap = null;
        try
        {
            cap = new WasapiCapture(device);
            cap.DataAvailable += OnData;
            lock (_recordGate) { _capture = cap; _inputDevice = device; }
            cap.StartRecording();
        }
        catch
        {
            lock (_recordGate) { _capture = null; _inputDevice = null; }
            if (cap != null) { cap.DataAvailable -= OnData; cap.Dispose(); }
            device.Dispose();
            throw;
        }
    }

    void OnData(object? sender, WaveInEventArgs e)
    {
        lock (_recordGate)
        {
            if (_capture == null || !ReferenceEquals(sender, _capture) || e.BytesRecorded <= 0) return;
            _peak = ComputePeak(e.Buffer, e.BytesRecorded, _capture.WaveFormat);
            if (_recording)
            {
                try { _writer?.Write(e.Buffer, 0, e.BytesRecorded); }
                catch (Exception ex) { System.Diagnostics.Trace.WriteLine("录音写入失败：" + ex.Message); }
            }
        }
    }

    static float ComputePeak(byte[] buf, int len, WaveFormat wf)
    {
        float peak = 0;
        if (wf.Encoding == WaveFormatEncoding.IeeeFloat)
        {
            for (int i = 0; i + 4 <= len; i += 4)
                peak = Math.Max(peak, Math.Abs(BitConverter.ToSingle(buf, i)));
        }
        else // 16bit PCM 兜底
        {
            for (int i = 0; i + 2 <= len; i += 2)
                peak = Math.Max(peak, Math.Abs(BitConverter.ToInt16(buf, i) / 32768f));
        }
        return Math.Min(1, peak);
    }

    /// <summary>开始录制到指定 wav（格式沿用当前捕获流）。</summary>
    public void StartRecord(string wavPath)
    {
        lock (_recordGate)
        {
            if (_capture == null) throw new InvalidOperationException("麦克风监听未启动");
            _recording = false;
            var previous = _writer; _writer = null;
            previous?.Dispose();
            _writer = new WaveFileWriter(wavPath, _capture.WaveFormat);
            _recording = true;
        }
    }

    /// <summary>停止录制并落盘（之后可回放）。</summary>
    public void StopRecord()
    {
        lock (_recordGate)
        {
            _recording = false;
            var writer = _writer; _writer = null;
            writer?.Dispose();
        }
    }

    /// <summary>停止输入捕获并释放设备。</summary>
    public void StopInput()
    {
        WasapiCapture? cap;
        MMDevice? device;
        WaveFileWriter? writer;
        lock (_recordGate)
        {
            _recording = false;
            cap = _capture; _capture = null;
            device = _inputDevice; _inputDevice = null;
            writer = _writer; _writer = null;
            _peak = 0;
        }
        try { writer?.Dispose(); }
        finally
        {
            try
            {
                if (cap != null)
                {
                    cap.DataAvailable -= OnData;
                    try { cap.StopRecording(); } catch { /* 已停止，仍释放设备 */ }
                    finally { cap.Dispose(); }
                }
            }
            finally { device?.Dispose(); }
        }
    }

    // ═══ 扬声器：播放（可指定输出设备；deviceId=null 用默认） ═══

    /// <summary>播放音频文件（mp3/wav）。自然结束或被 StopPlay 时回调 onStopped（线程池线程，UI 侧自行封送）。</summary>
    public void Play(string filePath, string? deviceId, Action onStopped)
    {
        StopPlay();
        MediaFoundationReader? reader = null;
        WasapiOut? output = null;
        MMDevice? device = null;
        bool published = false;
        try
        {
            reader = new MediaFoundationReader(filePath);
            if (deviceId != null)
            {
                using var en = new MMDeviceEnumerator();
                device = en.GetDevice(deviceId);
            }
            output = device != null
                ? new WasapiOut(device, AudioClientShareMode.Shared, false, 150) : new WasapiOut();
            var owner = output;
            output.PlaybackStopped += (_, _) =>
            {
                // 旧播放延迟回调只能释放自己，不能停止后来启动的播放。
                if (StopPlayback(owner)) onStopped();
            };
            output.Init(reader);
            lock (_playGate) { _reader = reader; _output = output; _outputDevice = device; }
            published = true;
            output.Play();
        }
        catch
        {
            if (published) StopPlayback(output);
            else ReleasePlayback(output, reader, device);
            throw;
        }
    }

    /// <summary>停止回放并释放资源。</summary>
    public void StopPlay() => StopPlayback(null);

    bool StopPlayback(WasapiOut? expected)
    {
        WasapiOut? output;
        MediaFoundationReader? reader;
        MMDevice? device;
        lock (_playGate)
        {
            if (expected != null && !ReferenceEquals(expected, _output)) return false;
            output = _output; _output = null;
            reader = _reader; _reader = null;
            device = _outputDevice; _outputDevice = null;
        }
        // Stop/Dispose 可能等待音频线程，不在状态锁内执行。
        ReleasePlayback(output, reader, device);
        return output != null;
    }

    static void ReleasePlayback(WasapiOut? output, MediaFoundationReader? reader, MMDevice? device)
    {
        try
        {
            if (output != null)
            {
                try { output.Stop(); } catch { /* 已停止，仍释放 */ }
                finally { output.Dispose(); }
            }
        }
        finally
        {
            try { reader?.Dispose(); }
            finally { device?.Dispose(); }
        }
    }

    public void Dispose()
    {
        StopInput();
        StopPlay();
    }
}
