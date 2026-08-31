using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace YanJi.Plugin.StressTest;

/// <summary>「散热测试」页面：CPU 单烤（prime95）/ GPU 单烤（FurMark）/ 双烤。
/// 进程起停全部走 StressTestService；安全约定：页面 Unloaded（切走/关窗）自动停止烤机，
/// 程序退出还有 Job 对象兜底，绝不留下后台烤机进程。通过后状态行变绿并同步导航圆点。</summary>
public partial class StressTestPage : UserControl, YanJi.PluginSdk.IModulePage
{
    readonly DispatcherTimer _tick = new() { Interval = TimeSpan.FromMilliseconds(500) };
    TimeSpan _lastRun = TimeSpan.Zero; // 最近一次烤了多久（未烤过为 0，状态行提示用）
    DateTime? _runStart;               // 本轮烤机起点（running→停止 的那一拍结算进 _lastRun）

    /// <summary>核对完成状态变化（true = 已通过，false = 回到待核对）；MainWindow 据此同步导航圆点。</summary>
    public event Action<bool>? PassChanged;

    /// <summary>当前是否已核对通过。</summary>
    public bool Passed { get; private set; }

    public StressTestPage()
    {
        InitializeComponent();

        // 线程数下拉：1 .. 逻辑核心数，默认拉满
        var (phys, logical) = StressTestService.CoreCounts;
        for (int i = 1; i <= logical; i++)
            ThreadsBox.Items.Add(new ComboBoxItem { Content = $"{i} 线程", Tag = i });
        ThreadsBox.SelectedIndex = logical - 1;
        CoresHint.Text = $"本机 {phys} 物理核 / {logical} 逻辑线程；超过物理核数的部分以超线程跑满";

        _tick.Tick += (_, _) => RefreshRunningState();
        StressTestService.StateChanged += OnServiceStateChanged;
        StressTestService.Logged += OnStressLogged;
        SensorMonitorService.Sampled += OnSensorSampled;
        SensorMonitorService.SensorsResolved += OnSensorsResolved;
        SensorMonitorService.GpuListReady += OnGpuListReady;
        SensorMonitorService.GpuChanging += OnGpuChanging;
        // 页面常驻机制下切走即 Unloaded：必须停烤机与传感器轮询（关窗也会触发，双保险）
        Unloaded += (_, _) =>
        {
            _tick.Stop();
            StressTestService.Stop();
            SensorMonitorService.Stop();
        };
        // 页面可见时才开传感器轮询（Loaded 在切回时会重发，Start 幂等）
        Loaded += (_, _) => SensorMonitorService.Start();
        SensorHintText.Text = "正在初始化传感器……";
        RefreshRunningState();
    }

    void StartButton_Click(object sender, RoutedEventArgs e)
    {
        SetPassed(false);
        var mode = ParseTag<StressTestService.StressMode>(ModeBox);
        var fft = ParseTag<StressTestService.FftPreset>(FftBox);
        int threads = ParseTag<int>(ThreadsBox);
        int msaa = ParseTag<int>(MsaaBox);
        var (w, h) = ParseResolution();
        try
        {
            StressTestService.Start(mode,
                new StressTestService.Prime95Settings(threads, fft, P95ShowUiCheck.IsChecked == true),
                new StressTestService.FurMarkSettings(w, h, FullscreenCheck.IsChecked == true, msaa,
                    FurShowUiCheck.IsChecked == true));
        }
        catch (Exception ex)
        {
            RunStateText.Text = "启动失败：" + ex.Message;
        }
        RefreshRunningState();
    }

    /// <summary>烤机事件日志（服务在后台线程触发，封送 UI 追加；上限 500 条截最旧，自动滚底）。</summary>
    void OnStressLogged(string line) =>
        Dispatcher.BeginInvoke(() =>
        {
            LogBox.AppendText(line + Environment.NewLine);
            if (LogBox.LineCount > 500)
            {
                int i = LogBox.Text.IndexOf('\n');
                if (i >= 0) LogBox.Text = LogBox.Text[(i + 1)..];
            }
            LogBox.ScrollToEnd();
        });

    void StopButton_Click(object sender, RoutedEventArgs e) => StressTestService.Stop();

    void PassButton_Click(object sender, RoutedEventArgs e)
    {
        // 通过即停烤（随后自动跳转切页，Unloaded 也会再兜一次）
        StressTestService.Stop();
        SetPassed(true);
    }

    /// <summary>服务状态变化可能在线程池线程（ProcessExit 等），封送 UI 线程刷新。</summary>
    void OnServiceStateChanged() =>
        Dispatcher.BeginInvoke(() =>
        {
            if (StressTestService.IsRunning) _tick.Start();
            else _tick.Stop();
            RefreshRunningState();
        });

    // ═══ 数据监控（SensorMonitorService 事件均在后台线程，封送 UI） ═══

    bool _cpuTempDown, _cpuPowerDown, _gpuTempDown, _gpuPowerDown;
    SensorMonitorService.SensorMap? _sensorMap;

    void OnSensorSampled(SensorMonitorService.Reading r) =>
        Dispatcher.BeginInvoke(() =>
        {
            UpdateChart(CpuTempChart, CpuTempOverlay, r.CpuTemp, ref _cpuTempDown);
            UpdateChart(CpuPowerChart, CpuPowerOverlay, r.CpuPower, ref _cpuPowerDown);
            UpdateChart(GpuTempChart, null, r.GpuTemp, ref _gpuTempDown);
            UpdateChart(GpuPowerChart, null, r.GpuPower, ref _gpuPowerDown);
        });

    /// <summary>单图更新：有值推点（收起叠层）；无值置占位——CPU 两路显示「文案 + 安装按钮」叠层，
    /// GPU 两路用图表自带占位文案（状态不变时不重复操作）。</summary>
    void UpdateChart(LineChartControl chart, UIElement? overlay, double? value, ref bool unavailable)
    {
        if (value is double v)
        {
            if (unavailable)
            {
                unavailable = false;
                if (overlay != null) overlay.Visibility = Visibility.Collapsed;
            }
            chart.PushSample(v);
        }
        else if (!unavailable)
        {
            unavailable = true;
            if (overlay != null)
            {
                chart.SetUnavailable(""); // 占位由叠层出（文案 + 按钮），图表只留网格
                overlay.Visibility = Visibility.Visible;
            }
            else
            {
                chart.SetUnavailable("传感器不可用");
            }
        }
        RefreshSensorHint();
    }

    /// <summary>传感器口径四行排版：每路一行「指标 ← 硬件 / 传感器」，不可用行注明状态。</summary>
    void RefreshSensorHint()
    {
        var m = _sensorMap;
        if (m == null) return;
        SensorHintText.Text =
            $"CPU 温度  ←  {Line(m.CpuName, m.CpuTemp, _cpuTempDown, needsDriver: true)}\n" +
            $"CPU 功耗  ←  {Line(m.CpuName, m.CpuPower, _cpuPowerDown, needsDriver: true)}\n" +
            $"GPU 温度  ←  {Line(m.GpuName, m.GpuTemp, _gpuTempDown, needsDriver: false)}\n" +
            $"GPU 功耗  ←  {Line(m.GpuName, m.GpuPower, _gpuPowerDown, needsDriver: false)}";

        static string Line(string? hw, string? sensor, bool down, bool needsDriver)
        {
            if (sensor == null) return "未找到传感器";
            string s = $"{hw} / {sensor}";
            if (down) s += needsDriver ? "（读数无效，需 PawnIO 驱动）" : "（当前不可用）";
            return s;
        }
    }

    void OnSensorsResolved(SensorMonitorService.SensorMap map) =>
        Dispatcher.BeginInvoke(() =>
        {
            _sensorMap = map;
            RefreshSensorHint();
        });

    // ═══ GPU 选择（换绑立即清两图旧曲线；采样周期内生效） ═══

    bool _gpuBoxSyncing;

    void OnGpuListReady(IReadOnlyList<SensorMonitorService.GpuOption> options, int selected) =>
        Dispatcher.BeginInvoke(() =>
        {
            _gpuBoxSyncing = true;
            GpuBox.Items.Clear();
            foreach (var g in options)
                GpuBox.Items.Add(new ComboBoxItem { Content = g.Name, Tag = g.Index });
            GpuBox.SelectedIndex = selected;
            GpuBox.Visibility = options.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
            _gpuBoxSyncing = false;
        });

    void GpuBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (_gpuBoxSyncing) return;
        if ((GpuBox.SelectedItem as ComboBoxItem)?.Tag is int index)
            SensorMonitorService.SetGpu(index);
    }

    void OnGpuChanging() =>
        Dispatcher.BeginInvoke(() =>
        {
            // 立即清两图旧曲线与降级标志（新传感器下轮采样重评估）
            GpuTempChart.Clear();
            GpuPowerChart.Clear();
            _gpuTempDown = _gpuPowerDown = false;
        });

    /// <summary>「安装 PawnIO 驱动」：静默跑官方签名安装器（-install -silent），装完重初始化 LHM Computer
    /// （Stop+Start，无需重启程序），CPU 两路随即出真实读数；失败则提示。</summary>
    async void InstallPawnIo_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button btn) return;
        btn.IsEnabled = false;
        btn.Content = "正在安装……";
        bool ok = await Task.Run(SensorMonitorService.InstallDriver);
        if (ok)
        {
            SensorMonitorService.Restart(); // 重初始化：新 Computer 直接走驱动读数（无静态缓存依赖）
        }
        else
        {
            btn.Content = "安装失败，点这里重试";
            btn.IsEnabled = true;
        }
    }

    void RefreshRunningState()
    {
        bool running = StressTestService.IsRunning;
        StartButton.IsEnabled = !running;
        StopButton.IsEnabled = running;

        if (running && StressTestService.StartedAt is DateTime t0)
        {
            _runStart = t0;
            var span = DateTime.Now - t0;
            ElapsedText.Text = $"已烤 {FormatSpan(span)}";
            RunStateText.Text = "烤机进行中…… 请留意温度与风扇状态；切走本页会自动停止烤机";
        }
        else
        {
            // running→停止 的那一拍：结算本次烤机时长
            if (_runStart is DateTime rs)
            {
                _lastRun = DateTime.Now - rs;
                _runStart = null;
            }
            ElapsedText.Text = "未在烤机";
            if (_lastRun > TimeSpan.Zero)
                RunStateText.Text = $"本次已烤 {FormatSpan(_lastRun)}；可再次开始或直接判断散热是否合格";
            else if (!RunStateText.Text.StartsWith("启动失败"))
                RunStateText.Text = "尚未烤过机；烤机会让硬件满载发热，请留意温度与风扇状态";
        }

        // 通过按钮旁的提示：未在烤 / 已烤时长，由用户自行判断（不做强限制）
        if (!Passed)
            PassHintText.Text = running ? "正在烤机" :
                _lastRun > TimeSpan.Zero ? $"本次已烤 {FormatSpan(_lastRun)}" : "尚未烤过机";
    }

    /// <summary>更新核对状态行（通过 = 绿色「✓ 已核对通过」）并通知导航圆点。</summary>
    void SetPassed(bool passed)
    {
        Passed = passed;
        if (passed)
        {
            StatusText.Text = "✓ 已核对通过";
            StatusText.Foreground = Brushes.ForestGreen;
            PassHintText.Text = "";
        }
        else
        {
            StatusText.Text = "待核对";
            StatusText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
        }
        PassChanged?.Invoke(passed);
    }

    (int W, int H) ParseResolution()
    {
        string tag = (ResolutionBox.SelectedItem as ComboBoxItem)?.Tag as string ?? "1920x1080";
        string[] parts = tag.Split('x');
        return (int.Parse(parts[0]), int.Parse(parts[1]));
    }

    static T ParseTag<T>(ComboBox box) where T : notnull
    {
        object tag = ((ComboBoxItem)box.SelectedItem).Tag;
        // Tag 在 XAML 里一律是字符串：枚举走 Enum.Parse，int 等走 Convert
        if (typeof(T).IsEnum) return (T)Enum.Parse(typeof(T), (string)tag);
        return (T)Convert.ChangeType(tag, typeof(T));
    }

    static string FormatSpan(TimeSpan s) =>
        s.TotalHours >= 1 ? $"{(int)s.TotalHours}:{s.Minutes:00}:{s.Seconds:00}" : $"{s.Minutes:00}:{s.Seconds:00}";
}
