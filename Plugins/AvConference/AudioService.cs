using NAudio.CoreAudioApi;
using NAudio.Wave;

namespace YanJi.Plugin.AvConference;

/// <summary>音频采集/播放（NAudio，Ms-PL 开源协议）：WASAPI 设备枚举、麦克风电平监听、
/// 录制 wav、指定输出设备回放（mp3/wav 均走 MediaFoundationReader）。</summary>
public class AudioService : IDisposable
{
    WasapiCapture? _capture;
    WaveFileWriter? _writer;
    WasapiOut? _output;
    MediaFoundationReader? _reader;
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
            return en.EnumerateAudioEndPoints(flow, DeviceState.Active)
                .Select(d => (d.ID, d.FriendlyName)).ToList();
        }
        catch { return []; }
    }

    // ═══ 麦克风：电平监听 + 录制（同一条捕获流复用） ═══

    /// <summary>开启指定录音设备的捕获（电平监听；StartRecord 后同时写 wav）。</summary>
    public void StartInput(string deviceId)
    {
        StopInput();
        using var en = new MMDeviceEnumerator();
        var cap = new WasapiCapture(en.GetDevice(deviceId));
        cap.DataAvailable += OnData;
        cap.StartRecording();
        _capture = cap;
    }

    void OnData(object? sender, WaveInEventArgs e)
    {
        if (_capture == null || e.BytesRecorded <= 0) return;
        _peak = ComputePeak(e.Buffer, e.BytesRecorded, _capture.WaveFormat);
        if (_recording)
        {
            try { _writer?.Write(e.Buffer, 0, e.BytesRecorded); } catch { /* 写失败丢帧 */ }
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
        if (_capture == null) throw new InvalidOperationException("麦克风监听未启动");
        _writer?.Dispose();
        _writer = new WaveFileWriter(wavPath, _capture.WaveFormat);
        _recording = true;
    }

    /// <summary>停止录制并落盘（之后可回放）。</summary>
    public void StopRecord()
    {
        _recording = false;
        _writer?.Dispose();
        _writer = null;
    }

    /// <summary>停止输入捕获并释放设备。</summary>
    public void StopInput()
    {
        StopRecord();
        var cap = _capture; _capture = null;
        if (cap != null)
        {
            try { cap.StopRecording(); } catch { /* 已停止 */ }
            cap.Dispose();
        }
        _peak = 0;
    }

    // ═══ 扬声器：播放（可指定输出设备；deviceId=null 用默认） ═══

    /// <summary>播放音频文件（mp3/wav）。自然结束或被 StopPlay 时回调 onStopped（线程池线程，UI 侧自行封送）。</summary>
    public void Play(string filePath, string? deviceId, Action onStopped)
    {
        StopPlay();
        _reader = new MediaFoundationReader(filePath);
        _output = deviceId != null
            ? new WasapiOut(new MMDeviceEnumerator().GetDevice(deviceId), AudioClientShareMode.Shared, false, 150)
            : new WasapiOut();
        _output.PlaybackStopped += (_, _) =>
        {
            StopPlay();
            onStopped();
        };
        _output.Init(_reader);
        _output.Play();
    }

    /// <summary>停止回放并释放资源。</summary>
    public void StopPlay()
    {
        var output = _output; _output = null;
        if (output != null)
        {
            try { output.Stop(); } catch { /* 已停止 */ }
            output.Dispose();
        }
        _reader?.Dispose();
        _reader = null;
    }

    public void Dispose()
    {
        StopInput();
        StopPlay();
    }
}
