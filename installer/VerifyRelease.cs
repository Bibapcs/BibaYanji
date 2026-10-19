using System.Collections;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Management;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using YanJi;
using YanJi.PluginSdk;
using YanJi.Plugin.ConfigCheck;
using YanJi.Plugin.StressTest;

// 验收专用入口：真实宿主/插件/设备；反射观察状态及纯事件测试，不启动烤机或磁盘负载。
internal static class VerifyRelease
{
    static readonly List<object> Checks = [];
    static readonly ConcurrentQueue<SensorMonitorService.Reading> Readings = new();
    static readonly ConcurrentQueue<string> Failures = new();
    static MainWindow Window = null!;
    static string Output = "";
    static int ExitCode;
    static SensorMonitorService.SensorMap? LastMap;
    static readonly Stopwatch Runtime = Stopwatch.StartNew();
    static readonly ConcurrentQueue<double> SampleTimes = new();
    static readonly List<object> ResourceSamples = [];
    static int SoakSeconds;
    static readonly object EvidenceGate = new();
    static StreamWriter? RawWriter, HeartbeatWriter, ResourceWriter;
    static double SoakStartSeconds = -1, SoakEndSeconds = -1, LastSampleSeconds = -1, LastUiSeconds;
    static int SoakActive, WindowClosed;
    static long UiHeartbeatCount, WatchdogCount;
    static bool SleepGuardApplied, SleepGuardRestored;
    static uint PreviousExecutionState;
    static readonly DateTimeOffset RunStartedAt = DateTimeOffset.Now;
    static readonly JsonSerializerOptions PrettyJson = new() { WriteIndented = true };
    // 只供验收持续监控防睡眠；不更改产品设置/电源计划，必须在相同UI线程finally还原。
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint SetThreadExecutionState(uint flags);
    static object Clock(double globalSeconds)
    {
        double start = Volatile.Read(ref SoakStartSeconds), end = Volatile.Read(ref SoakEndSeconds);
        return new { globalElapsedSeconds = globalSeconds,
            soakElapsedSeconds = start < 0 ? (double?)null : Math.Max(0, (end >= 0 ? end : globalSeconds) - start) };
    }
    static void RecordFailure(string context, Exception ex)
    {
        Failures.Enqueue($"{context}: {ex}");
        Interlocked.Exchange(ref ExitCode, 1);
        Console.Error.WriteLine($"{context}: {ex}");
    }
    static void WriteLine(StreamWriter? writer, object evidence, string context)
    {
        try
        {
            if (writer == null) throw new IOException("证据writer未打开");
            // 各writer分别加锁；原始采样写入受阻时不占用后台heartbeat的锁。
            lock (writer) writer.WriteLine(JsonSerializer.Serialize(evidence));
        }
        catch (Exception ex) { RecordFailure(context, ex); }
    }
    static void WriteJson(string name, object evidence)
    {
        try
        {
            lock (EvidenceGate) File.WriteAllText(Path.Combine(Output, name), JsonSerializer.Serialize(evidence, PrettyJson));
        }
        catch (Exception ex) { RecordFailure($"落盘 {name}", ex); }
    }
    static void DisposeWriter(ref StreamWriter? writer)
    {
        var closing = writer;
        writer = null;
        try { if (closing != null) lock (closing) closing.Dispose(); }
        catch (Exception ex) { RecordFailure("关闭证据writer", ex); }
    }
    static void OnSample(SensorMonitorService.Reading reading)
    {
        double globalSeconds = Runtime.Elapsed.TotalSeconds;
        Readings.Enqueue(reading);
        SampleTimes.Enqueue(globalSeconds);
        Volatile.Write(ref LastSampleSeconds, globalSeconds);
        WriteLine(RawWriter, new { elapsedSeconds = globalSeconds, clock = Clock(globalSeconds), reading }, "原始样本落盘");
    }
    static void OnMap(SensorMonitorService.SensorMap map) => LastMap = map;
    static async Task Watchdog(CancellationToken cancellation)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(5));
        bool sampleAlarm = false, uiAlarm = false, runningAlarm = false, stressAlarm = false;
        try
        {
            while (await timer.WaitForNextTickAsync(cancellation).ConfigureAwait(false))
            {
                double globalSeconds = Runtime.Elapsed.TotalSeconds;
                double lastSample = Volatile.Read(ref LastSampleSeconds);
                double sampleAge = lastSample >= 0 ? globalSeconds - lastSample : double.PositiveInfinity;
                double uiAge = globalSeconds - Volatile.Read(ref LastUiSeconds);
                bool active = Volatile.Read(ref SoakActive) == 1;
                bool isRunning = SensorMonitorService.IsRunning;
                Interlocked.Increment(ref WatchdogCount);
                WriteLine(HeartbeatWriter, new { date = DateTimeOffset.Now, clock = Clock(globalSeconds), soakActive = active,
                    lastSampleAgeSeconds = double.IsFinite(sampleAge) ? (double?)sampleAge : null, isRunning,
                    uiHeartbeatAgeSeconds = uiAge, uiHeartbeatCount = Interlocked.Read(ref UiHeartbeatCount),
                    windowClosed = Volatile.Read(ref WindowClosed) == 1, stressRunning = StressTestService.IsRunning }, "后台heartbeat落盘");
                if (active && sampleAge > 30 && !sampleAlarm)
                { sampleAlarm = true; RecordFailure("后台看门狗", new TimeoutException($"采样年龄 {sampleAge:F3}s 超过30秒")); }
                if (active && uiAge > 30 && !uiAlarm)
                { uiAlarm = true; RecordFailure("后台看门狗", new TimeoutException($"UI heartbeat年龄 {uiAge:F3}s 超过30秒")); }
                if (active && !isRunning && !runningAlarm)
                { runningAlarm = true; RecordFailure("后台看门狗", new InvalidOperationException("Soak中监控已停止")); }
                if (StressTestService.IsRunning && !stressAlarm)
                { stressAlarm = true; RecordFailure("后台看门狗", new InvalidOperationException("非预期烤机负载")); }
            }
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
        catch (Exception ex) { RecordFailure("后台看门狗异常", ex); }
    }
    static void Check(string name, bool passed, object? detail = null)
    {
        Checks.Add(new { name, passed, detail, clock = Clock(Runtime.Elapsed.TotalSeconds) });
        Console.WriteLine($"{(passed ? "PASS" : "FAIL")} {name}: {JsonSerializer.Serialize(detail)}");
        if (!passed) throw new InvalidOperationException(name);
    }
    [STAThread]
    static int Main(string[] args)
    {
        if (args.Length is not (1 or 3)) { Console.Error.WriteLine("Usage: VerifyRelease <existing-output-directory> [--soak-seconds <60..21600>]"); return 2; }
        if (args.Length == 3 && (args[1] != "--soak-seconds" || !int.TryParse(args[2], out SoakSeconds) || SoakSeconds is < 60 or > 21600)) return 2;
        Console.OutputEncoding = System.Text.Encoding.UTF8;
        Console.InputEncoding = System.Text.Encoding.UTF8;
        Output = Path.GetFullPath(args[0]);
        if (!Directory.Exists(Output)) return 2;
        try
        {
            RawWriter = new StreamWriter(Path.Combine(Output, "samples.ndjson")) { AutoFlush = true };
            HeartbeatWriter = new StreamWriter(Path.Combine(Output, "heartbeat.ndjson")) { AutoFlush = true };
            ResourceWriter = new StreamWriter(Path.Combine(Output, "resources.ndjson")) { AutoFlush = true };
            // 不安装/启动驱动；前置状态不满足就结束，不自动绕过。
            using (var query = new ManagementObjectSearcher("SELECT State FROM Win32_SystemDriver WHERE Name='PawnIO'"))
            using (var rows = query.Get())
            {
                bool running = false;
                foreach (ManagementObject row in rows)
                    using (row) running |= string.Equals(row["State"] as string, "Running", StringComparison.OrdinalIgnoreCase);
                if (!running)
                {
                    Failures.Enqueue("PawnIO必须已运行；未执行服务操作。");
                    ExitCode = 3;
                    WriteJson("real-monitor-verification.json", new { status = "precondition-failed", exitCode = ExitCode, clock = Clock(Runtime.Elapsed.TotalSeconds), failures = Failures.ToArray() });
                    return ExitCode;
                }
            }
            SensorMonitorService.AllowAutomaticDriverStart = false;
            var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            ApplyTheme("Light");
            app.Resources.MergedDictionaries.Add(new ResourceDictionary { Source = new Uri("/验机软件;component/Themes/Controls.xaml", UriKind.Relative) });
            app.DispatcherUnhandledException += (_, e) => { RecordFailure("Dispatcher异常", e.Exception); e.Handled = true; };
            SensorMonitorService.Sampled += OnSample;
            SensorMonitorService.SensorsResolved += OnMap;
            SensorMonitorService.Failed += Failures.Enqueue;
            Window = new MainWindow { IsHitTestVisible = false, ShowActivated = false };
            Window.Closed += (_, _) => Interlocked.Exchange(ref WindowClosed, 1);
            Volatile.Write(ref LastUiSeconds, Runtime.Elapsed.TotalSeconds);
            var uiHeartbeat = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromSeconds(1) };
            uiHeartbeat.Tick += (_, _) =>
            {
                Volatile.Write(ref LastUiSeconds, Runtime.Elapsed.TotalSeconds);
                Interlocked.Increment(ref UiHeartbeatCount);
            };
            using var watchdogCancellation = new CancellationTokenSource();
            var watchdog = Task.Run(() => Watchdog(watchdogCancellation.Token));
            uiHeartbeat.Start();
            bool started = false;
            Window.Loaded += async (_, _) =>
            {
                if (started) return;
                started = true;
                try
                {
                    if (SoakSeconds > 0)
                    {
                        PreviousExecutionState = SetThreadExecutionState(0x80000001); // ES_CONTINUOUS | ES_SYSTEM_REQUIRED，验收专用。
                        SleepGuardApplied = PreviousExecutionState != 0;
                        Check("验收专用防睡眠请求成功", SleepGuardApplied, new { previous = PreviousExecutionState, scope = "只防系统自动睡眠，不改电源计划；UI线程finally恢复原执行状态" });
                    }
                    await Run();
                }
                catch (Exception ex)
                {
                    RecordFailure("验收异常", ex);
                    Checks.Add(new { name = "exception", passed = false, detail = ex.ToString(), clock = Clock(Runtime.Elapsed.TotalSeconds) });
                }
                finally
                {
                    Volatile.Write(ref SoakActive, 0);
                    try
                    {
                        if (Volatile.Read(ref WindowClosed) == 0) Window.Close();
                        // 已有失败不阻止清理等待；清理超时仍记失败，绝不计为通过。
                        var closing = Stopwatch.StartNew();
                        while ((SensorMonitorService.IsRunning || !AvStopped(StoredPage("av"))) && closing.Elapsed.TotalSeconds <= 90)
                            await Task.Delay(100);
                        Check("最终正常关闭事件及监控停止", Volatile.Read(ref WindowClosed) == 1 && !Window.IsVisible && !SensorMonitorService.IsRunning);
                        Check("最终关闭全部设备及测试窗口清理", AvStopped(StoredPage("av"))
                            && !File.Exists((string)Field(StoredPage("av"), "_recordFile")!)
                            && Field(StoredPage("screen"), "_testWindow") == null && !StressTestService.IsRunning);
                    }
                    catch (Exception ex) { RecordFailure("最终关闭", ex); }
                    finally
                    {
                        uiHeartbeat.Stop();
                        watchdogCancellation.Cancel();
                        await watchdog;
                        if (SleepGuardApplied)
                        {
                            SleepGuardRestored = SetThreadExecutionState(PreviousExecutionState | 0x80000000) != 0; // 相同线程清除本次ES_SYSTEM_REQUIRED。
                            try { Check("验收防睡眠finally还原", SleepGuardRestored); }
                            catch (Exception ex) { RecordFailure("还原执行状态", ex); }
                        }
                        SensorMonitorService.Sampled -= OnSample;
                        SensorMonitorService.SensorsResolved -= OnMap;
                        SensorMonitorService.Failed -= Failures.Enqueue;
                        DisposeWriter(ref RawWriter);
                        DisposeWriter(ref HeartbeatWriter);
                        DisposeWriter(ref ResourceWriter);
                        if (!Failures.IsEmpty) Interlocked.Exchange(ref ExitCode, 1);
                        var samples = Readings.ToArray();
                        if (SoakSeconds > 0) WriteJson("soak-progress.json", new
                        {
                            status = ExitCode == 0 && SoakEndSeconds >= SoakStartSeconds && SoakStartSeconds >= 0
                                && SoakEndSeconds - SoakStartSeconds >= SoakSeconds ? "completed" : "failed",
                            requestedSeconds = SoakSeconds, clock = Clock(Runtime.Elapsed.TotalSeconds),
                            totalElapsedSeconds = Runtime.Elapsed.TotalSeconds, soakStartGlobalSeconds = SoakStartSeconds,
                            soakEndGlobalSeconds = SoakEndSeconds, sampleCount = samples.Length, exitCode = ExitCode,
                            windowClosed = Volatile.Read(ref WindowClosed) == 1, sleepGuardApplied = SleepGuardApplied, sleepGuardRestored = SleepGuardRestored,
                            failures = Failures.ToArray()
                        });
                        WriteJson("real-monitor-verification.json", new
                        {
                            date = DateTimeOffset.Now, runStartedAt = RunStartedAt, mode = "real-release-MainWindow-real-hardware-no-stress-load",
                            driver = "already-running-PawnIO", exitCode = ExitCode, checks = Checks,
                            requestedSoakSeconds = SoakSeconds, totalElapsedSeconds = Runtime.Elapsed.TotalSeconds, clock = Clock(Runtime.Elapsed.TotalSeconds),
                            soakStartGlobalSeconds = SoakStartSeconds, soakEndGlobalSeconds = SoakEndSeconds,
                            sleepGuardApplied = SleepGuardApplied, sleepGuardRestored = SleepGuardRestored,
                            watchdogCount = Interlocked.Read(ref WatchdogCount), uiHeartbeatCount = Interlocked.Read(ref UiHeartbeatCount),
                            failures = Failures.ToArray(), sampleCount = samples.Length, resources = ResourceSamples,
                            stats = new { cpuTemperature = Stats(samples.Select(x => x.CpuTemp)), cpuPower = Stats(samples.Select(x => x.CpuPower)),
                                gpuTemperature = Stats(samples.Select(x => x.GpuTemp)), gpuPower = Stats(samples.Select(x => x.GpuPower)) },
                            limitations = new[]
                            {
                                "电池异常场景由用户验收；不安装/启动驱动，不运行烤机或真实磁盘基准。",
                                "本程序禁鼠标命中；正常EXE鼠标/键盘入口必须由主代理另实测，不能用本程序代替。",
                                "键盘HandleKey为接口输入，不是物理按键；屏幕五色不判定坏点、色准或显示器质量。",
                                "AV只使用实际设备；无设备/占用/未授权须明确degraded，不代表设备质量通过；录音仅2秒且离页删除，页面截图遮盖摄像区域。",
                                "Disk ParseScores仅synthetic内存数据；SetRunning仅state test；不点击Run/OpenResultDir或伪造成绩。",
                                "通过→重置仅验证单项UI状态，未人为标记全部硬件通过；插件管理仅列表/关闭取消，不测导入/删除。",
                                "后台看门狗能记录UI停滞但不能强制解开原生或UI死锁，需外部主代理监督进程；旧Soak失败原因未知。",
                                "不能识别上游未抛异常的旧传感器值；连续主段独立计时，短跑/重跑不可拼接三小时。"
                            }
                        });
                        app.Shutdown(ExitCode);
                    }
                }
            };
            app.Run(Window);
        }
        catch (Exception ex)
        {
            RecordFailure("入口或收尾异常", ex);
            WriteJson("real-monitor-verification.json", new { status = "failed", exitCode = ExitCode, clock = Clock(Runtime.Elapsed.TotalSeconds), checks = Checks, failures = Failures.ToArray() });
        }
        finally
        {
            SensorMonitorService.Sampled -= OnSample;
            SensorMonitorService.SensorsResolved -= OnMap;
            SensorMonitorService.Failed -= Failures.Enqueue;
            if (SleepGuardApplied && !SleepGuardRestored)
            {
                SleepGuardRestored = SetThreadExecutionState(PreviousExecutionState | 0x80000000) != 0;
                if (!SleepGuardRestored) RecordFailure("入口finally还原防睡眠", new InvalidOperationException("SetThreadExecutionState失败"));
            }
            DisposeWriter(ref RawWriter);
            DisposeWriter(ref HeartbeatWriter);
            DisposeWriter(ref ResourceWriter);
        }
        return ExitCode;
    }
    static object Stats(IEnumerable<double?> source)
    {
        var values = source.Where(x => x.HasValue).Select(x => x!.Value).ToArray();
        double? mean = values.Length > 0 ? values.Average() : null;
        return new { count = values.Length, min = values.Length > 0 ? (double?)values.Min() : null,
            max = values.Length > 0 ? (double?)values.Max() : null, mean,
            median = values.Length > 0 ? (double?)Median(values) : null,
            populationVariance = mean is double average ? (double?)values.Select(v => Math.Pow(v - average, 2)).Average() : null,
            distinctValues = values.Distinct().Count() };
    }
    static double Median(double[] values)
    {
        var ordered = values.Order().ToArray();
        return ordered.Length % 2 == 0 ? (ordered[ordered.Length / 2 - 1] + ordered[ordered.Length / 2]) / 2 : ordered[ordered.Length / 2];
    }
    static async Task Soak()
    {
        Nav("stress");
        int initial = Readings.Count;
        await Until(() => Readings.Count >= initial + 2);
        // 全部证据同一单调全局时钟；主段从此刻起连续计时，不纳入准备或其它回归时间。
        double start = Runtime.Elapsed.TotalSeconds;
        Volatile.Write(ref SoakStartSeconds, start);
        Volatile.Write(ref SoakEndSeconds, -1);
        Volatile.Write(ref SoakActive, 1);
        int startingCount = Readings.Count;
        int startingTimeCount = SampleTimes.Count;
        using var process = Process.GetCurrentProcess();
        var previousCpu = process.TotalProcessorTime;
        double previousSeconds = 0;
        int checkpoint = 0;
        long initialUiHeartbeats = Interlocked.Read(ref UiHeartbeatCount);
        long initialWatchdogs = Interlocked.Read(ref WatchdogCount);
        try
        {
            while (Runtime.Elapsed.TotalSeconds - start < SoakSeconds)
            {
                await Task.Delay(TimeSpan.FromSeconds(Math.Max(0.1, Math.Min(10, SoakSeconds - (Runtime.Elapsed.TotalSeconds - start)))));
                process.Refresh();
                var cpu = process.TotalProcessorTime;
                double globalSeconds = Runtime.Elapsed.TotalSeconds;
                double seconds = globalSeconds - start;
                var resource = new { elapsedSeconds = seconds, clock = Clock(globalSeconds), samples = Readings.Count,
                    workingSetBytes = process.WorkingSet64, privateBytes = process.PrivateMemorySize64,
                    handles = process.HandleCount, threads = process.Threads.Count,
                    cpuPercentOfMachine = (cpu - previousCpu).TotalSeconds / (seconds - previousSeconds) / Environment.ProcessorCount * 100 };
                ResourceSamples.Add(resource);
                WriteLine(ResourceWriter, new { kind = "resource", resource }, "Soak资源落盘");
                previousCpu = cpu; previousSeconds = seconds;
                if (!Failures.IsEmpty) throw new InvalidOperationException(string.Join("\n", Failures));
                if (!SensorMonitorService.IsRunning || SampleTimes.IsEmpty || Runtime.Elapsed.TotalSeconds - SampleTimes.Last() > 30)
                    throw new InvalidOperationException("No recent monitor sample within 30 seconds");
                if (Runtime.Elapsed.TotalSeconds - Volatile.Read(ref LastUiSeconds) > 30)
                    throw new TimeoutException("UI heartbeat超过30秒");
                if (StressTestService.IsRunning) throw new InvalidOperationException("Unexpected stress workload");
                if ((int)(seconds / 300) > checkpoint)
                {
                    checkpoint = (int)(seconds / 300);
                    Console.WriteLine($"SOAK {seconds:F0}s global={globalSeconds:F0}s samples={Readings.Count} privateMiB={process.PrivateMemorySize64 / 1048576.0:F1}");
                }
                WriteJson("soak-progress.json", new
                {
                    status = "running", requestedSeconds = SoakSeconds, elapsedSeconds = seconds, clock = Clock(globalSeconds),
                    soakStartGlobalSeconds = start, sampleCount = Readings.Count,
                    lastSampleAgeSeconds = globalSeconds - Volatile.Read(ref LastSampleSeconds), isRunning = SensorMonitorService.IsRunning,
                    uiHeartbeatAgeSeconds = globalSeconds - Volatile.Read(ref LastUiSeconds), uiHeartbeatCount = Interlocked.Read(ref UiHeartbeatCount),
                    watchdogCount = Interlocked.Read(ref WatchdogCount), sleepGuardApplied = SleepGuardApplied, failures = Failures.ToArray(), resource
                });
                if (!Failures.IsEmpty) throw new InvalidOperationException(string.Join("\n", Failures));
            }
            double end = Runtime.Elapsed.TotalSeconds;
            var times = SampleTimes.Skip(startingTimeCount).ToArray();
            double maxGap = times.Length > 1 ? times.Zip(times.Skip(1), (a, b) => b - a).Max() : double.PositiveInfinity;
            // 原间隔门槛保留，并补齐主段首尾边界，避免末尾无采样却通过。
            double boundaryGap = times.Length > 0 ? Math.Max(Math.Max(0, times[0] - start), Math.Max(0, end - times[^1])) : double.PositiveInfinity;
            Check("soak requested wall duration reached", end - start >= SoakSeconds, end - start);
            Check("soak periodic samples sustained", Readings.Count - startingCount >= SoakSeconds / 5, Readings.Count - startingCount);
            Check("soak no sampling gap over 30 seconds", maxGap <= 30, maxGap);
            Check("soak主段首尾采样间隔不超过30秒", boundaryGap <= 30, boundaryGap);
            Check("soak独立看门狗和UI heartbeat持续", Interlocked.Read(ref WatchdogCount) - initialWatchdogs >= SoakSeconds / 10
                && Interlocked.Read(ref UiHeartbeatCount) > initialUiHeartbeats,
                new { watchdogs = Interlocked.Read(ref WatchdogCount) - initialWatchdogs, uiHeartbeats = Interlocked.Read(ref UiHeartbeatCount) - initialUiHeartbeats });
            Check("soak no dispatcher or monitor failures", Failures.IsEmpty);
            Capture("soak-final.png");
        }
        finally
        {
            Volatile.Write(ref SoakEndSeconds, Runtime.Elapsed.TotalSeconds);
            Volatile.Write(ref SoakActive, 0);
            WriteLine(HeartbeatWriter, new { kind = "soak-end", clock = Clock(Runtime.Elapsed.TotalSeconds), failures = Failures.ToArray() }, "Soak结束落盘");
        }
    }
    static async Task Until(Func<bool> condition, int seconds = 120)
    {
        // 导航/内容替换后先让 WPF 完成 Loaded、Unloaded 与布局，避免旧 IsLoaded 瞬态误判。
        await Dispatcher.Yield(DispatcherPriority.ContextIdle);
        var timer = Stopwatch.StartNew();
        while (!condition())
        {
            if (!Failures.IsEmpty) throw new InvalidOperationException(string.Join("\n", Failures));
            if (timer.Elapsed.TotalSeconds > seconds) throw new TimeoutException("Acceptance condition timed out");
            await Task.Delay(100);
        }
    }
    static void Nav(string id)
    {
        var nav = (ListBox)Window.FindName("NavList");
        nav.SelectedItem = nav.Items.Cast<ListBoxItem>().Single(x => Equals(x.Tag, id));
    }
    static ConfigCheckPage Config => (ConfigCheckPage)((ContentControl)Window.FindName("ContentHost")).Content;
    static StressTestPage Stress => (StressTestPage)((ContentControl)Window.FindName("ContentHost")).Content;
    static bool ConfigReady() => ((ContentControl)Window.FindName("ContentHost")).Content is ConfigCheckPage page
        && ((ItemsControl)page.FindName("ResultsHost")).ItemsSource != null;
    static void ApplyTheme(string name)
    {
        var dicts = Application.Current.Resources.MergedDictionaries;
        for (int i = dicts.Count - 1; i >= 0; --i)
            if (dicts[i].Source?.OriginalString.EndsWith("Light.xaml") == true || dicts[i].Source?.OriginalString.EndsWith("Dark.xaml") == true) dicts.RemoveAt(i);
        dicts.Add(new ResourceDictionary { Source = new Uri($"/验机软件;component/Themes/{name}.xaml", UriKind.Relative) });
    }
    static void Capture(string name, Window? target = null)
    {
        target ??= Window;
        target.UpdateLayout();
        var image = new RenderTargetBitmap((int)target.ActualWidth, (int)target.ActualHeight, 96, 96, PixelFormats.Pbgra32);
        image.Render(target);
        var previews = Visuals<Image>(target).Where(x => x.Name == "PreviewImage" && x.IsVisible && x.ActualWidth > 0 && x.ActualHeight > 0).ToArray();
        if (previews.Length > 0)
        {
            // 截图保留页面布局，但在编码落盘前遮盖摄像区域；不改变产品预览状态。
            var redacted = new DrawingVisual();
            using (var drawing = redacted.RenderOpen())
            {
                drawing.DrawImage(image, new Rect(0, 0, target.ActualWidth, target.ActualHeight));
                foreach (var preview in previews)
                {
                    Point position = preview.TranslatePoint(new Point(), target);
                    drawing.DrawRectangle(Brushes.Black, null, new Rect(position.X - 2, position.Y - 2, preview.ActualWidth + 4, preview.ActualHeight + 4));
                }
            }
            image = new RenderTargetBitmap(image.PixelWidth, image.PixelHeight, 96, 96, PixelFormats.Pbgra32);
            image.Render(redacted);
        }
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(image));
        using var file = File.Create(Path.Combine(Output, name)); encoder.Save(file);
    }
    static object? ChartField(string chart, string field) => typeof(LineChartControl).GetField(field, BindingFlags.Instance | BindingFlags.NonPublic)!
        .GetValue((LineChartControl)Stress.FindName(chart));
    const BindingFlags InstanceMembers = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
    static object? Field(object target, string name) => (target.GetType().GetField(name, InstanceMembers)
        ?? throw new MissingFieldException(target.GetType().FullName, name)).GetValue(target);
    static object? Property(object target, string name) => (target.GetType().GetProperty(name, InstanceMembers)
        ?? throw new MissingMemberException(target.GetType().FullName, name)).GetValue(target);
    static object? Invoke(object target, string name, params object?[] args) => (target.GetType().GetMethod(name, InstanceMembers)
        ?? throw new MissingMethodException(target.GetType().FullName, name)).Invoke(target, args);
    static T Control<T>(FrameworkElement parent, string name) where T : FrameworkElement =>
        parent.FindName(name) as T ?? throw new InvalidOperationException($"{parent.GetType().Name}.{name} 不存在");
    static UserControl Page => (UserControl)Control<ContentControl>(Window, "ContentHost").Content;
    static UserControl StoredPage(string id) => (UserControl)((IDictionary)Field(Window, "_pages")!)[id]!;
    static void Click(Button button)
    {
        if (!button.IsEnabled || !button.IsVisible) throw new InvalidOperationException($"按钮不可用：{button.Name}/{button.Content}");
        // 验收入口禁鼠标命中；触发真实按钮的路由事件，不直接调用 Click handler 或改产品状态。
        button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent, button));
    }
    static IEnumerable<T> Visuals<T>(DependencyObject root) where T : DependencyObject
    {
        if (root is T match) yield return match;
        for (int i = 0; i < VisualTreeHelper.GetChildrenCount(root); ++i)
            foreach (var child in Visuals<T>(VisualTreeHelper.GetChild(root, i))) yield return child;
    }
    static void CheckCompletion(string name)
    {
        var passable = (HashSet<string>)Field(Window, "_passableTags")!;
        var done = (HashSet<string>)Field(Window, "_doneTags")!;
        int count = passable.Count(done.Contains);
        bool dotsMatch = true;
        foreach (DictionaryEntry entry in (IDictionary)Field(Window, "_navDots")!)
        {
            bool completed = done.Contains((string)entry.Key);
            var tuple = (System.Runtime.CompilerServices.ITuple)entry.Value!;
            var dot = (System.Windows.Shapes.Ellipse)tuple[0]!;
            var mark = (TextBlock)tuple[1]!;
            dotsMatch &= mark.Visibility == (completed ? Visibility.Visible : Visibility.Collapsed)
                && dot.Fill == (completed ? Brushes.ForestGreen : Brushes.Transparent)
                && dot.Stroke is SolidColorBrush stroke && stroke.Color == (completed ? Colors.ForestGreen
                    : ((SolidColorBrush)Window.FindResource("TextSecondaryBrush")).Color);
        }
        var progress = Control<ProgressBar>(Window, "CompletionProgress");
        Check(name, Control<TextBlock>(Window, "CompletionText").Text == $"{count} / {passable.Count}"
            && progress.Maximum == Math.Max(1, passable.Count) && progress.Value == count && dotsMatch
            && Control<TextBlock>(Window, "ModuleCountText").Text == "6 个模块"
            && Control<TextBlock>(Window, "AllDoneHint").Visibility == (count == passable.Count && count > 0 ? Visibility.Visible : Visibility.Collapsed),
            new { count, total = passable.Count, scope = "仅UI状态，不代表硬件合格" });
    }
    static async Task CheckSixPages()
    {
        var expected = new[]
        {
            (Id: "config", Type: typeof(ConfigCheckPage).FullName!, Title: "逐项核对，确认这台设备"),
            (Id: "keyboard", Type: "YanJi.Plugin.KeyboardTest.KeyboardTestPage", Title: "键盘逐键测试"),
            (Id: "screen", Type: "YanJi.Plugin.ScreenDeadPixel.DeadPixelPage", Title: "屏幕坏点检测"),
            (Id: "av", Type: "YanJi.Plugin.AvConference.AvConferencePage", Title: "影音设备检查"),
            (Id: "stress", Type: typeof(StressTestPage).FullName!, Title: "数据监控（LibreHardwareMonitor 2 秒轮询）"),
            (Id: "disk", Type: "YanJi.Plugin.DiskMark.DiskMarkPage", Title: "硬盘性能测试")
        };
        foreach (string theme in new[] { "Light", "Dark" })
        {
            ApplyTheme(theme);
            foreach (var item in expected)
            {
                Nav(item.Id);
                await Until(() => Page.IsLoaded, 10);
                Window.UpdateLayout();
                var navItem = (ListBoxItem)Control<ListBox>(Window, "NavList").SelectedItem;
                Check($"{theme} {item.Id} 导航与Content", Equals(navItem.Tag, item.Id)
                    && ReferenceEquals(Page, StoredPage(item.Id)) && Page.GetType().FullName == item.Type);
                string moduleName = System.Windows.Automation.AutomationProperties.GetName(navItem);
                Check($"{theme} {item.Id} 标题与主题", Control<TextBlock>(Window, "CurrentModuleText").Text == moduleName
                    && Visuals<TextBlock>(Page).Any(x => x.Text == item.Title)
                    && !string.IsNullOrWhiteSpace(Control<TextBlock>(Window, "ModuleHintText").Text)
                    && Page.Foreground is SolidColorBrush foreground
                    && Window.FindResource("TextBrush") is SolidColorBrush token && foreground.Color == token.Color,
                    new { moduleName, item.Title });
                CheckCompletion($"{theme} {item.Id} 完成进度一致");
                Capture($"page-{item.Id}-{theme}.png");
                if (item.Id == "av") await AwaitAvInit(Page);
            }
            await Until(() => !SensorMonitorService.IsRunning, 120);
            await CheckAvStopped(StoredPage("av"), $"{theme} 导航离页");
        }
        Check("自动入口禁命中不代表正常EXE不可点击", !Window.IsHitTestVisible,
            "需主代理另启动正常EXE实测鼠标/键盘；本程序仅驱动按钮路由事件及真实接口");
    }
    static async Task CheckConfigButtons()
    {
        Nav("config");
        await Until(ConfigReady, 180);
        var page = Config;
        Click(Control<Button>(page, "PassButton"));
        Check("Config通过真实按钮仅验证状态(state test)", page.Passed
            && ((HashSet<string>)Field(Window, "_doneTags")!).SetEquals(["config"]), "并非已人工核对硬件；马上用真实重新检测按钮撤回");
        CheckCompletion("Config通过后进度(state test)");
        Nav("config");
        await Until(() => page.IsLoaded, 10);
        Click(Control<Button>(page, "RunButton"));
        Check("Config重新检测真实按钮撤回通过", !page.Passed && !Control<Button>(page, "PassButton").IsEnabled
            && !((HashSet<string>)Field(Window, "_doneTags")!).Contains("config"));
        CheckCompletion("Config重新检测后进度(state test)");
        await Until(ConfigReady, 180);
        Check("Config重新检测完成真实报告", Control<Button>(page, "PassButton").IsEnabled
            && Control<ProgressBar>(page, "DetectProgress").Value == 100);
    }
    static async Task CheckKeyboard()
    {
        Nav("keyboard");
        await Until(() => Page.IsLoaded, 10);
        var page = Page;
        var handler = (IKeyHandlerPage)page;
        var progress = Control<ProgressBar>(page, "KeyProgress");
        var log = Control<TextBox>(page, "LogBox");
        Click(Control<Button>(page, "ResetButton"));
        var map = (IDictionary)Field(page, "_keyMap")!;
        var visuals = ((IEnumerable)map[Key.A]!).Cast<object>().ToArray();
        Check("Keyboard HandleKey down真实高亮", handler.HandleKey(Key.A, true) && progress.Value == 0
            && visuals.All(x => ((Border)Property(x, "Bd")!).Background == page.FindResource("AccentBrush")));
        Check("Keyboard HandleKey重复down不累计", handler.HandleKey(Key.A, true) && progress.Value == 0);
        Check("Keyboard HandleKey up真实累计", handler.HandleKey(Key.A, false) && progress.Value == visuals.Length
            && visuals.All(x => Equals(Property(x, "Tested"), true) && ((Border)Property(x, "Bd")!).Background == Brushes.ForestGreen));
        Check("Keyboard HandleKey重复up幂等", handler.HandleKey(Key.A, false) && progress.Value == visuals.Length);
        int before = (int)progress.Value;
        Check("Keyboard Enter共享两个键帽", ((ICollection)map[Key.Return]!).Count == 2
            && handler.HandleKey(Key.Return, true) && handler.HandleKey(Key.Return, false) && progress.Value == before + 2);
        Check("Keyboard未知键不吞事件", !handler.HandleKey(Key.None, true) && !handler.HandleKey(Key.None, false));
        Check("Keyboard真实接口日志", log.Text.Contains("按下") && log.Text.Contains("松开"));
        Click(Control<Button>(page, "PassButton"));
        Check("Keyboard通过按钮更新宿主(state test)", Equals(Property(page, "Passed"), true)
            && ((HashSet<string>)Field(Window, "_doneTags")!).SetEquals(["keyboard"])
            && Equals(((ListBoxItem)Control<ListBox>(Window, "NavList").SelectedItem).Tag, "screen"),
            "仅按钮/进度/自动导航状态测试，未声称实体键盘无故障");
        CheckCompletion("Keyboard通过后完成进度(state test)");
        Nav("keyboard");
        await Until(() => page.IsLoaded, 10);
        Click(Control<Button>(page, "ResetButton"));
        Check("Keyboard重置按钮撤回通过并清标记", !Equals(Property(page, "Passed"), true)
            && progress.Value == 0 && log.Text.Length == 0 && Control<Button>(page, "PassButton").IsEnabled
            && !((HashSet<string>)Field(Window, "_doneTags")!).Contains("keyboard")
            && ((IEnumerable)Field(page, "_all")!).Cast<object>().All(x => Equals(Property(x, "Tested"), false)
                && ((SolidColorBrush)((Border)Property(x, "Bd")!).Background).Color == ((SolidColorBrush)page.FindResource("SurfaceBgBrush")).Color));
        CheckCompletion("Keyboard重置后完成进度(state test)");
    }
    static async Task CheckScreen()
    {
        Nav("screen");
        await Until(() => Page.IsLoaded, 10);
        var page = Page;
        var start = Control<Button>(page, "StartButton");
        Check("Screen真实显示器入口", Control<ComboBox>(page, "MonitorBox").Items.Count > 0 && start.IsEnabled);
        try
        {
            Click(start);
            var test = (Window)Field(page, "_testWindow")!;
            await Until(() => test.IsLoaded, 10);
            Check("Screen Start创建真实ColorTestWindow", test.GetType().FullName == "YanJi.Plugin.ScreenDeadPixel.ColorTestWindow"
                && test.IsVisible && !start.IsEnabled && Control<TextBlock>(page, "TestingHint").Text.Length > 0);
            foreach (Color color in new[] { Colors.White, Colors.Red, Colors.Lime, Colors.Blue, Colors.Black })
            {
                Check($"Screen Next真实五色 {color}", test.Background is SolidColorBrush brush && brush.Color == color,
                    "只验证渲染状态，不判断坏点或色准");
                Capture($"screen-color-{color.R:X2}{color.G:X2}{color.B:X2}.png", test);
                Invoke(test, "Next");
            }
            Check("Screen Next五色循环回白且停提示计时器", ((SolidColorBrush)test.Background).Color == Colors.White
                && !((DispatcherTimer)Field(test, "_hintTimer")!).IsEnabled);
            test.Close();
            Check("Screen Close释放窗口引用与入口", Field(page, "_testWindow") == null && !test.IsVisible && start.IsEnabled
                && Control<TextBlock>(page, "TestingHint").Text == "");

            Click(Control<Button>(page, "PassButton"));
            Check("Screen通过按钮仅单项state test", Equals(Property(page, "Passed"), true)
                && ((HashSet<string>)Field(Window, "_doneTags")!).SetEquals(["screen"]), "未判定显示器质量；紧接真实Start撤回");
            CheckCompletion("Screen通过后完成进度(state test)");
            Nav("screen");
            await Until(() => page.IsLoaded, 10);
            Click(start);
            var leaving = (Window)Field(page, "_testWindow")!;
            await Until(() => leaving.IsLoaded, 10);
            Check("Screen真实Start撤回通过(state test)", !Equals(Property(page, "Passed"), true)
                && !((HashSet<string>)Field(Window, "_doneTags")!).Contains("screen"));
            CheckCompletion("Screen重新测试后完成进度(state test)");
            Nav("keyboard");
            await Until(() => !page.IsLoaded, 10);
            await Task.Delay(300);
            Check("Screen离页自动关闭纯色窗口", Field(page, "_testWindow") == null && !leaving.IsVisible
                && !((DispatcherTimer)Field(leaving, "_hintTimer")!).IsEnabled);
        }
        finally
        {
            // 失败也关闭本验收创建的窗口；不会把此兜底算作产品离页通过。
            if (Field(page, "_testWindow") is Window leftover) leftover.Close();
        }
    }
    static async Task AwaitAvInit(UserControl page)
    {
        await Until(() => page.IsLoaded, 10);
        await Until(() => ((SemaphoreSlim)Field(page, "_initGate")!).CurrentCount == 1
            && !Equals(Field(page, "_initializing"), true)
            && (((DispatcherTimer)Field(page, "_levelTimer")!).IsEnabled
                || CameraUnavailable(page)), 30);
    }
    static bool CameraUnavailable(UserControl page)
    {
        string text = Control<TextBlock>(page, "CameraError").Text;
        return Control<TextBlock>(page, "CameraError").Visibility == Visibility.Visible
            && (text.Contains("未检测到") || text.Contains("不可用") || text.Contains("失败"));
    }
    static bool AvStopped(UserControl page)
    {
        var camera = Field(page, "_camera")!;
        var audio = Field(page, "_audio")!;
        return ((SemaphoreSlim)Field(page, "_initGate")!).CurrentCount == 1
            && ((SemaphoreSlim)Field(camera, "_lifecycleGate")!).CurrentCount == 1
            && Field(page, "_sessionCts") == null && Field(page, "_frameHandler") == null && Field(page, "_cameraFailureHandler") == null
            && !Equals(Field(page, "_initializing"), true) && !Equals(Field(page, "_recording"), true)
            && !((DispatcherTimer)Field(page, "_levelTimer")!).IsEnabled
            && !((DispatcherTimer)Field(page, "_recordLimitTimer")!).IsEnabled
            && Field(camera, "_capture") == null && Field(camera, "_reader") == null
            && Field(audio, "_capture") == null && Field(audio, "_writer") == null
            && Field(audio, "_output") == null && Field(audio, "_reader") == null
            && Control<Image>(page, "PreviewImage").Source == null;
    }
    static async Task CheckAvStopped(UserControl page, string phase)
    {
        await Until(() => AvStopped(page), 30);
        await Task.Delay(700);
        Check($"AV {phase}设备与计时器清理", AvStopped(page)
            && !File.Exists((string)Field(page, "_recordFile")!), "无捕获/播放对象、无预览、无levelTimer/录音计时器、临时录音已删除");
    }
    static bool AudioPlaybackRunning(object audio) => Field(audio, "_output") is { } output
        && Equals(Property(audio, "IsPlaying"), true) && Property(output, "PlaybackState")?.ToString() == "Playing";
    static (long DataBytes, int ByteRate, double DurationSeconds) RecordingEvidence(string path)
    {
        if (!File.Exists(path)) return (0, 0, 0);
        using var stream = File.OpenRead(path);
        using var reader = new BinaryReader(stream);
        if (stream.Length < 12 || new string(reader.ReadChars(4)) != "RIFF") throw new InvalidDataException("录音不是RIFF WAV");
        reader.ReadUInt32();
        if (new string(reader.ReadChars(4)) != "WAVE") throw new InvalidDataException("录音缺少WAVE标记");
        long dataBytes = 0;
        int byteRate = 0;
        while (stream.Position + 8 <= stream.Length)
        {
            string chunk = new(reader.ReadChars(4));
            uint size = reader.ReadUInt32();
            long content = stream.Position;
            if (content + size > stream.Length) throw new InvalidDataException("录音chunk超出文件长度");
            if (chunk == "fmt " && size >= 16)
            {
                reader.ReadUInt16(); reader.ReadUInt16(); reader.ReadUInt32();
                byteRate = reader.ReadInt32();
            }
            if (chunk == "data") dataBytes += size;
            stream.Position = content + size + (size % 2);
        }
        return (dataBytes, byteRate, byteRate > 0 ? (double)dataBytes / byteRate : 0);
    }
    static async Task CheckAv()
    {
        Nav("av");
        var page = StoredPage("av");
        var camera = Field(page, "_camera")!;
        var audio = Field(page, "_audio")!;
        int frames = 0;
        int width = 0, height = 0, bytes = 0;
        Action<int, int, byte[]> onFrame = (w, h, pixels) =>
        {
            Volatile.Write(ref width, w); Volatile.Write(ref height, h); Volatile.Write(ref bytes, pixels.Length);
            Interlocked.Increment(ref frames);
        };
        var frameEvent = camera.GetType().GetEvent("FrameReady")!;
        frameEvent.AddEventHandler(camera, onFrame);
        try
        {
            await AwaitAvInit(page);
            var previewTimer = Stopwatch.StartNew();
            while ((Volatile.Read(ref frames) == 0 || Control<Image>(page, "PreviewImage").Source == null)
                && !CameraUnavailable(page) && previewTimer.Elapsed.TotalSeconds < 10) await Task.Delay(100);
            string cameraDiagnostic = Control<TextBlock>(page, "CameraError").Text;
            bool realFrame = Volatile.Read(ref frames) > 0 && width > 0 && height > 0 && bytes == (long)width * height * 4
                && Control<Image>(page, "PreviewImage").Source is BitmapSource;
            bool unavailable = CameraUnavailable(page);
            Check("AV Init真实摄像帧或明确不可用诊断", realFrame || unavailable,
                new { cameraCount = Control<ComboBox>(page, "CameraBox").Items.Count, frames, width, height, bytes,
                    outcome = realFrame ? "real frames; 非画质判定" : "degraded", cameraDiagnostic });
            string micDiagnostic = Control<TextBlock>(page, "MicStatus").Text;
            bool input = Field(audio, "_capture") != null;
            Check("AV Init真实麦克风捕获或明确诊断", input || micDiagnostic.Contains("不可用") || micDiagnostic.Contains("未检测到麦克风"),
                new { input, micDiagnostic, diagnostic = input ? "real WASAPI capture; 非音质判定" : "麦克风不可用或未枚举到活动输入设备" });
            Check("AV Init输出设备或明确诊断", Control<ComboBox>(page, "SpkBox").Items.Count > 0
                || Control<TextBlock>(page, "MusicStatus").Text.Contains("未检测到音频输出设备"),
                new { outputs = Control<ComboBox>(page, "SpkBox").Items.Count, status = Control<TextBlock>(page, "MusicStatus").Text });
            Check("AV Init真实levelTimer", ((DispatcherTimer)Field(page, "_levelTimer")!).IsEnabled);

            string recordFile = (string)Field(page, "_recordFile")!;
            bool recordEnabled = Control<Button>(page, "RecordButton").IsEnabled;
            if (recordEnabled) Click(Control<Button>(page, "RecordButton"));
            bool recording = Equals(Field(page, "_recording"), true);
            if (recording)
            {
                Check("AV Record真实按钮启动", input && Field(audio, "_writer") != null
                    && ((DispatcherTimer)Field(page, "_recordLimitTimer")!).IsEnabled
                    && !Control<Button>(page, "PlayRecButton").IsEnabled,
                    "本轮用户授权仅2秒录音，不复制音频到报告");
                await Task.Delay(2000);
                Click(Control<Button>(page, "RecordButton"));
                var wav = RecordingEvidence(recordFile);
                bool recordedData = wav.DataBytes > 0 && wav.ByteRate > 0 && wav.DurationSeconds > 0;
                string stopStatus = Control<TextBlock>(page, "MicStatus").Text;
                Check("AV Stop真实按钮停止并落盘或明确降级", !Equals(Field(page, "_recording"), true) && Field(audio, "_writer") == null
                    && !((DispatcherTimer)Field(page, "_recordLimitTimer")!).IsEnabled
                    && ((recordedData && Control<Button>(page, "PlayRecButton").IsEnabled)
                        || (!Control<Button>(page, "PlayRecButton").IsEnabled && (stopStatus.Contains("未获得有效录音") || stopStatus.Contains("录制保存失败")))),
                    new { bytes = File.Exists(recordFile) ? new FileInfo(recordFile).Length : 0, requestedSeconds = 2,
                        wav.DataBytes, wav.ByteRate, wav.DurationSeconds,
                        outcome = recordedData ? "real WAV data chunk; 非音质判定" : "degraded", stopStatus });
                if (recordedData && Control<Button>(page, "PlayRecButton").IsEnabled)
                {
                    Click(Control<Button>(page, "PlayRecButton"));
                    bool playing = AudioPlaybackRunning(audio)
                        && !Control<TextBlock>(page, "MicStatus").Text.Contains("回放失败");
                    Check("AV replay真实播放或明确降级", playing || Control<TextBlock>(page, "MicStatus").Text.Contains("回放失败"),
                        new { outcome = playing ? "real playback; 非听感判定" : "degraded", status = Control<TextBlock>(page, "MicStatus").Text });
                    if (playing)
                    {
                        await Task.Delay(400);
                        Click(Control<Button>(page, "PlayRecButton"));
                        await Until(() => !Equals(Property(audio, "IsPlaying"), true), 10);
                        Check("AV replay真实停止按钮释放播放", Field(audio, "_output") == null && Field(audio, "_reader") == null);
                    }
                }
                // 播放失败可能已创建部分NAudio对象；由真实离页清理验证，不调用私有StopPlay掩盖问题。
            }
            else Check("AV Record不可用明确诊断(degraded)", Control<TextBlock>(page, "MicStatus").Text.Contains("无法录制")
                || (!recordEnabled && !input && (micDiagnostic.Contains("不可用") || micDiagnostic.Contains("未检测到麦克风"))),
                new { recordEnabled, status = Control<TextBlock>(page, "MicStatus").Text, scope = "录音/回放未执行，不计设备质量通过" });

            Nav("keyboard");
            await Until(() => !page.IsLoaded, 10);
            await CheckAvStopped(page, "录音回放离页");
            int stoppedFrames = Volatile.Read(ref frames);
            await Task.Delay(700);
            Check("AV离页后无新真实帧", Volatile.Read(ref frames) == stoppedFrames, new { stoppedFrames, frames });
            Nav("av");
            await AwaitAvInit(page);
            bool musicEnabled = Control<Button>(page, "PlayMusicButton").IsEnabled;
            if (musicEnabled) Click(Control<Button>(page, "PlayMusicButton"));
            bool music = AudioPlaybackRunning(audio)
                && Control<TextBlock>(page, "MusicStatus").Text == "播放中…";
            Check("AV music真实短播放或明确降级", music || Control<TextBlock>(page, "MusicStatus").Text.Contains("播放失败")
                || Control<TextBlock>(page, "MusicStatus").Text.Contains("未找到")
                || Control<TextBlock>(page, "MusicStatus").Text.Contains("未检测到"),
                new { outcome = music ? "real playback; 非扬声器质量判定" : "degraded", status = Control<TextBlock>(page, "MusicStatus").Text });
            if (music)
            {
                await Task.Delay(400);
                Click(Control<Button>(page, "PlayMusicButton"));
                await Until(() => !Equals(Property(audio, "IsPlaying"), true), 10);
                Check("AV music真实停止按钮释放播放", Field(audio, "_output") == null && Field(audio, "_reader") == null);
                Click(Control<Button>(page, "PlayMusicButton"));
                var oldOutput = Field(audio, "_output")!;
                Click(Control<Button>(page, "PlayMusicButton"));
                Click(Control<Button>(page, "PlayMusicButton"));
                var newOutput = Field(audio, "_output");
                Check("AV旧播放回调不停止新播放", !ReferenceEquals(oldOutput, newOutput)
                    && Equals(Invoke(audio, "StopPlayback", oldOutput), false) && AudioPlaybackRunning(audio),
                    "真实音乐Stop/Start后重放旧对象身份回调，不注入声音或成绩");
                Click(Control<Button>(page, "PlayMusicButton"));
                await Until(() => !Equals(Property(audio, "IsPlaying"), true), 10);
            }
            Nav("keyboard");
            await Until(() => !page.IsLoaded, 10);
            await CheckAvStopped(page, "音乐离页");
            for (int cycle = 1; cycle <= 2; ++cycle)
            {
                Nav("av");
                await Until(() => page.IsLoaded, 10);
                // 不等待Init结束：覆盖异步初始化过程中快速离页，禁止人为Cleanup代替产品行为。
                Nav("keyboard");
                await Until(() => !page.IsLoaded, 10);
                await CheckAvStopped(page, $"快速离页{cycle}");
                stoppedFrames = Volatile.Read(ref frames);
                await Task.Delay(700);
                Check($"AV快速离页{cycle}不再帧或levelTimer", Volatile.Read(ref frames) == stoppedFrames && AvStopped(page));
            }
            Nav("av");
            await AwaitAvInit(page);
            int sessionBeforeReentry = (int)Field(page, "_session")!;
            Nav("keyboard");
            await Until(() => !page.IsLoaded, 10);
            await CheckAvStopped(page, "再初始化前离页");
            int framesBeforeReentry = Volatile.Read(ref frames);
            Nav("av");
            await AwaitAvInit(page);
            previewTimer.Restart();
            while ((Volatile.Read(ref frames) <= framesBeforeReentry || Control<Image>(page, "PreviewImage").Source == null)
                && !CameraUnavailable(page) && previewTimer.Elapsed.TotalSeconds < 10) await Task.Delay(100);
            Check("AV再次进入能重新初始化", ((DispatcherTimer)Field(page, "_levelTimer")!).IsEnabled
                && !Equals(Field(page, "_initializing"), true) && (int)Field(page, "_session")! > sessionBeforeReentry
                && ((Volatile.Read(ref frames) > framesBeforeReentry && Control<Image>(page, "PreviewImage").Source is BitmapSource)
                    || CameraUnavailable(page)),
                new { framesBeforeReentry, frames, diagnostic = Control<TextBlock>(page, "CameraError").Text, scope = "实际新会话/新帧或明确degraded，不点击影音通过按钮" });
        }
        finally
        {
            Nav("keyboard");
            frameEvent.RemoveEventHandler(camera, onFrame);
        }
        await Until(() => !page.IsLoaded, 10);
        await CheckAvStopped(page, "最终离页");
    }
    static void CheckDisk()
    {
        Nav("disk");
        var page = Page;
        var service = Field(page, "_cdm")!.GetType();
        Check("Disk入口未启动真实跑分", Field(page, "_cts") == null && Field(page, "_result") == null
            && service.GetField("_current", BindingFlags.Static | BindingFlags.NonPublic)!.GetValue(null) == null
            && Control<Button>(page, "RunButton").IsEnabled && !Control<Button>(page, "CancelButton").IsEnabled
            && Control<TextBlock>(page, "PassHintText").Text == "尚未跑过分"
            && new[] { "ScoreSeqRead", "ScoreSeqWrite", "Score4KRead", "Score4KWrite" }.All(x => Control<TextBlock>(page, x).Text == "—"),
            new { drives = Control<ComboBox>(page, "DriveBox").Items.Count, scope = "仅枚举/入口，不点击Run/OpenResultDir，不读写基准文件" });
        const string synthetic = "{\"seq_read\":123.25,\"seq_write\":67.5,\"rnd4k_read\":8.75,\"rnd4k_write\":4.125}";
        const string syntheticPath = "synthetic-not-a-file";
        var parse = service.GetMethod("ParseScores", BindingFlags.Static | BindingFlags.Public)!;
        var result = parse.Invoke(null, [synthetic, syntheticPath])!;
        Check("Disk ParseScores synthetic四字段", Equals(Property(result, "SeqRead"), 123.25)
            && Equals(Property(result, "SeqWrite"), 67.5) && Equals(Property(result, "Rnd4KRead"), 8.75)
            && Equals(Property(result, "Rnd4KWrite"), 4.125) && Equals(Property(result, "JsonPath"), syntheticPath),
            new { source = "synthetic", input = synthetic, scope = "内存解析测试，不是磁盘性能成绩，不注入页面_result" });
        bool rejected = false;
        try { parse.Invoke(null, ["{}", syntheticPath]); }
        catch (TargetInvocationException ex) when (ex.InnerException is KeyNotFoundException) { rejected = true; }
        Check("Disk ParseScores缺字段拒绝(synthetic)", rejected);
        rejected = false;
        try { parse.Invoke(null, ["not-json", syntheticPath]); }
        catch (TargetInvocationException ex) when (ex.InnerException is JsonException) { rejected = true; }
        Check("Disk ParseScores无效JSON拒绝(synthetic)", rejected);
        // 只测重跑状态通知，绝不触发RunAsync/RunButton或假成绩/硬件通过。
        bool? changed = null;
        Action<bool> onChanged = value => changed = value;
        var module = (IModulePage)page;
        string previousStatus = Control<TextBlock>(page, "StatusText").Text;
        bool previousPassed = (bool)Field(page, "_passed")!;
        module.PassChanged += onChanged;
        try
        {
            Invoke(page, "SetRunning");
            Check("Disk SetRunning撤回通过事件(state test)", changed == false && Equals(Field(page, "_passed"), false)
                && Control<TextBlock>(page, "StatusText").Text == "跑分中…" && Field(page, "_cts") == null,
                "纯事件/反射状态测试，不触发磁盘读写且不点击通过按钮");
        }
        finally
        {
            module.PassChanged -= onChanged;
            Invoke(page, "SetIdle", previousStatus);
        }
        Check("Disk state test恢复未跑分状态", previousPassed == false && Equals(Field(page, "_passed"), false)
            && Control<TextBlock>(page, "StatusText").Text == previousStatus && Field(page, "_result") == null
            && Field(page, "_cts") == null && service.GetField("_current", BindingFlags.Static | BindingFlags.NonPublic)!.GetValue(null) == null);
        CheckCompletion("Disk state test后完成进度一致");
    }
    static async Task CheckPluginManager()
    {
        string? ops = typeof(YanJi.Services.PluginPackageService).GetProperty("OpsFile", BindingFlags.Static | BindingFlags.NonPublic)!.GetValue(null) as string;
        byte[]? before = File.Exists(ops) ? File.ReadAllBytes(ops!) : null;
        Window? manager = null;
        Exception? inspectionError = null;
        bool inspected = false;
        bool closed = false;
        // 宿主按钮ShowDialog进入嵌套消息循环，定时器在其中只读列表并Close取消，不触发导入/删除。
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
        timer.Tick += (_, _) =>
        {
            try
            {
                manager ??= Application.Current.Windows.Cast<Window>().SingleOrDefault(x => x.GetType().FullName == "YanJi.Views.PluginManagerWindow");
                if (manager?.IsLoaded != true) return;
                timer.Stop();
                manager.Closed += (_, _) => closed = true;
                var rows = Control<StackPanel>(manager, "PluginList");
                Check("插件管理真实入口列表", rows.Children.Count == 6
                    && Visuals<TextBlock>(rows).Count(x => x.Text.Contains("  v")) == 6);
                inspected = true;
            }
            catch (Exception ex) { inspectionError = ex; }
            finally { if (manager?.IsLoaded == true) manager.Close(); }
        };
        var deadline = new DispatcherTimer { Interval = TimeSpan.FromSeconds(10) };
        deadline.Tick += (_, _) =>
        {
            deadline.Stop(); timer.Stop();
            inspectionError ??= new TimeoutException("插件管理真实入口10秒未完成");
            foreach (var candidate in Application.Current.Windows.Cast<Window>().Where(x => x.GetType().FullName == "YanJi.Views.PluginManagerWindow").ToArray()) candidate.Close();
        };
        try
        {
            timer.Start(); deadline.Start();
            var button = Visuals<Button>(Window).Single(x => System.Windows.Automation.AutomationProperties.GetName(x) == "插件管理");
            Click(button);
        }
        finally { timer.Stop(); deadline.Stop(); }
        if (inspectionError != null) throw inspectionError;
        await Task.Delay(100);
        byte[]? after = File.Exists(ops) ? File.ReadAllBytes(ops!) : null;
        Check("插件管理取消无破坏", inspected && closed && ((before == null && after == null)
            || (before != null && after != null && before.SequenceEqual(after)))
            && Control<ListBox>(Window, "NavList").Items.Count == 6,
            "只打开列表并关闭取消；不覆盖/删除/导入/重启，待处理队列字节不变");
    }
    static async Task Run()
    {
        Check("six production plugins loaded", ((ListBox)Window.FindName("NavList")).Items.Count == 6);
        bool preview = File.Exists(Path.Combine(AppContext.BaseDirectory, "preview-mode"));
        Check("settings and plugin queue use correct data directory", Path.GetFileName(Settings.DataDirectory) == (preview ? "YanJiPreview" : "YanJi"));
        var ops = typeof(YanJi.Services.PluginPackageService).GetProperty("OpsFile", BindingFlags.Static | BindingFlags.NonPublic)!.GetValue(null) as string;
        Check("plugin queue follows isolated settings", Path.GetDirectoryName(ops) == Settings.DataDirectory);
        // Change pages while real collection is in progress; no fake sensor/session injection.
        await Task.Delay(100);
        Nav("stress");
        await Task.Delay(100);
        Nav("config");
        await Until(ConfigReady, 180);
        var groups = ((IEnumerable)((ItemsControl)Config.FindName("ResultsHost")).ItemsSource).Cast<InfoGroup>().ToList();
        Check("real configuration resumes and completes", ((Button)Config.FindName("PassButton")).IsEnabled);
        Check("ten report groups present", groups.Count >= 10, groups.Select(x => x.Title));
        Check("public firmware groups populated", new[] { "BIOS", "整机", "主板" }.All(t => groups.Any(g => g.Title == t && g.Entries.Any(e => e.Name != "采集结果"))));
        await CheckConfigButtons();
        await CheckSixPages();
        await CheckKeyboard();
        await CheckScreen();
        await CheckAv();
        Nav("disk");
        await Until(() => Page.IsLoaded, 10);
        CheckDisk();
        await CheckPluginManager();
        Check("新增验收未标记全部硬件通过", ((HashSet<string>)Field(Window, "_doneTags")!).Count == 0);
        for (int cycle = 1; cycle <= 3; ++cycle)
        {
            int before = Readings.Count;
            Nav("stress");
            await Until(() => Readings.Count >= before + 4, 120);
            await Task.Delay(200);
            Check($"cycle {cycle} real periodic samples", Readings.Count >= before + 4);
            Check($"cycle {cycle} supported sensor has positive data", Readings.Skip(before).Any(r => r.CpuTemp > 0 || r.GpuTemp > 0));
            foreach (var chartName in new[] { "CpuTempChart", "CpuPowerChart", "GpuTempChart", "GpuPowerChart" })
            {
                double? Expected()
                {
                    var r = Readings.Last();
                    return chartName switch { "CpuTempChart" => r.CpuTemp, "CpuPowerChart" => r.CpuPower, "GpuTempChart" => r.GpuTemp, _ => r.GpuPower };
                }
                await Until(() => Expected() is double value ? Equals(ChartField(chartName, "_current"), value)
                    : ChartField(chartName, "_current") is null && ChartField(chartName, "_unavailableText") is string);
                var points = (ICollection)ChartField(chartName, "_samples")!;
                Check($"cycle {cycle} {chartName} reflects latest real sample", Expected().HasValue ? points.Count >= 4 : points.Count == 0);
            }
            Check($"cycle {cycle} monitoring does not start workloads", !StressTestService.IsRunning);
            if (cycle == 1)
            {
                foreach (var theme in new[] { "Light", "Dark" })
                {
                    ApplyTheme(theme); await Task.Delay(150);
                    Check(theme + " theme tokens applied", Window.TryFindResource("TextBrush") is SolidColorBrush);
                    Capture("monitor-" + theme + ".png");
                }
                var gpuBox = (ComboBox)Stress.FindName("GpuBox");
                if (gpuBox.Items.Count > 1)
                {
                    int original = gpuBox.SelectedIndex;
                    before = Readings.Count;
                    gpuBox.SelectedIndex = original == 0 ? 1 : 0;
                    await Until(() => Readings.Count > before);
                    string selectedName = ((ComboBoxItem)gpuBox.SelectedItem).Content.ToString()!;
                    await Until(() => LastMap?.GpuName == selectedName);
                    Check("real GPU selection switches sensor mapping", LastMap?.GpuName == selectedName);
                    gpuBox.SelectedIndex = original;
                }
            }
            var oldSession = SensorMonitorService.SessionToken;
            Nav("config");
            await Until(() => !SensorMonitorService.IsRunning, 120);
            int stopped = Readings.Count;
            await Task.Delay(2300);
            Check($"cycle {cycle} no samples after page exit", Readings.Count == stopped);
            Check($"cycle {cycle} old session cancelled", oldSession.IsCancellationRequested);
        }
        Nav("stress");
        int restartCount = Readings.Count;
        await Until(() => Readings.Count > restartCount);
        var previousSession = SensorMonitorService.SessionToken;
        SensorMonitorService.Restart();
        int explicitRestart = Readings.Count;
        await Until(() => SensorMonitorService.SessionToken != previousSession && !SensorMonitorService.SessionToken.IsCancellationRequested && Readings.Count >= explicitRestart + 2, 120);
        Check("explicit stop/start replaces session and resumes real sampling", previousSession.IsCancellationRequested);
        if (SoakSeconds > 0) await Soak();
        Window.Close();
        await Until(() => !SensorMonitorService.IsRunning, 120);
        int closed = Readings.Count;
        await Task.Delay(2300);
        Check("window close stops monitoring", Readings.Count == closed);
        Check("no monitoring or dispatcher errors", Failures.IsEmpty, Failures.ToArray());
        Check("no stress workloads started", !StressTestService.IsRunning);
    }
}
