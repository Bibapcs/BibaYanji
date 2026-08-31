using System.IO;
using LibreHardwareMonitor.Hardware;

namespace YanJi.Plugin.StressTest;

/// <summary>散热测试页数据监控：独立 LHM Computer 2 秒轮询
/// CPU Package 温度/功耗、GPU 温度/功耗 四路传感器，事件回报给页面画折线图。
/// 与 HardwareInfoService 的一过性采集互不干扰（各自开各自 Computer，用完即关）。
/// 生命周期：页面 Loaded 开、Unloaded 关；不卡 UI（全程后台线程）。
/// 降级口径：传感器未找到 / 值为 null / 值 ≤0（AMD CPU 无 PawnIO 驱动时 LHM 给的是常驻 0）
/// 一律视为「不可用」，页面显示占位文案，其余可用路照常工作。
/// PawnIO 驱动：CPU/主板类传感器需要它（Ring0）；本服务附带安装器路径/注册表检测/
/// 静默安装（-install -silent）与安装后重初始化能力，装完无需重启程序。</summary>
public static class SensorMonitorService
{
    /// <summary>一次四路读数（null = 该路不可用）。</summary>
    public record Reading(double? CpuTemp, double? CpuPower, double? GpuTemp, double? GpuPower);

    /// <summary>传感器匹配结果（诊断展示用；Name 为 null 表示该路连传感器都没找到）。</summary>
    public record SensorMap(string? CpuName, string? CpuTemp, string? CpuPower, string? GpuTemp, string? GpuPower, string? GpuName);

    /// <summary>一张可选显卡（Index = LHM 枚举序，Name = 硬件名）。</summary>
    public record GpuOption(int Index, string Name);

    /// <summary>新读数（后台线程触发，订阅方需自行封送 UI 线程）。</summary>
    public static event Action<Reading>? Sampled;

    /// <summary>传感器解析完成后触发（初次与每次 GPU 换绑后；后台线程）。</summary>
    public static event Action<SensorMap>? SensorsResolved;

    /// <summary>可选显卡列表已就绪（仅初次；后台线程）。第二参数 = 当前生效索引（独显启发式默认）。</summary>
    public static event Action<IReadOnlyList<GpuOption>, int>? GpuListReady;

    /// <summary>GPU 即将换绑（页面应立即清两图旧曲线；触发线程不定）。</summary>
    public static event Action? GpuChanging;

    static readonly object _gate = new();
    static CancellationTokenSource? _cts;
    static Task? _loop;
    // 传感器/硬件引用全部在 loop 线程写、锁内读（换绑由 loop 线程在下个采样周期应用，不跨线程摆弄）
    static IHardware? _cpu, _gpu;
    static ISensor? _cpuTemp, _cpuPower, _gpuTemp, _gpuPower;
    static IReadOnlyList<GpuOption> _gpuOptions = [];
    static int _gpuIndex = -1;        // 当前生效的显卡索引
    static int _pendingGpuIndex = -1; // 页面请求换绑的目标索引（loop 下轮应用）

    public static bool IsRunning { get { lock (_gate) return _loop is { IsCompleted: false }; } }

    /// <summary>开始轮询（幂等：已在跑则直接返回）。Computer.Open 较慢，全程后台线程。</summary>
    public static void Start()
    {
        lock (_gate)
        {
            if (IsRunning) return;
            _cts = new CancellationTokenSource();
            _loop = Task.Run(() => Loop(_cts.Token));
        }
    }

    /// <summary>停止轮询并释放 Computer（同步等退出，最多 3 秒）。</summary>
    public static void Stop()
    {
        Task? loop;
        lock (_gate)
        {
            loop = _loop;
            _loop = null;
            try { _cts?.Cancel(); } catch { /* 忽略 */ }
            _cts?.Dispose();
            _cts = null;
        }
        try { loop?.Wait(3000); } catch { /* 退出异常不影响清理 */ }
    }

    /// <summary>切换监控显卡（立即发 GpuChanging 让页面清两图旧曲线；实际换绑由轮询线程在下个
    /// 采样周期 ≤2 秒应用，不重启 Computer，CPU 两路不受影响）。索引无效/未就绪/同值则忽略。</summary>
    public static void SetGpu(int index)
    {
        lock (_gate)
        {
            if (_gpuOptions.Count == 0 || index < 0 || index >= _gpuOptions.Count || index == _gpuIndex)
                return;
            _pendingGpuIndex = index;
            _gpuIndex = index;
        }
        GpuChanging?.Invoke();
    }

    static void Loop(CancellationToken ct)
    {
        var computer = new Computer { IsCpuEnabled = true, IsGpuEnabled = true };
        try
        {
            EnsurePawnIoService(); // 装了 PawnIO 但服务未运行（如重启后）时拉起来；未装则跳过
            computer.Open();

            // 全部带温度或功耗传感器的真实显卡（核显/独显都列，GPU 下拉框数据源）
            var gpus = computer.Hardware.Where(h =>
                h.HardwareType is HardwareType.GpuAmd or HardwareType.GpuNvidia or HardwareType.GpuIntel &&
                h.Sensors.Any(s => s.SensorType is SensorType.Temperature or SensorType.Power)).ToList();
            IReadOnlyList<GpuOption> options;
            int defaultIndex;
            lock (_gate)
            {
                _cpu = computer.Hardware.FirstOrDefault(h => h.HardwareType == HardwareType.Cpu);
                _cpuTemp = Find(_cpu, SensorType.Temperature, "Package");
                _cpuPower = Find(_cpu, SensorType.Power, "Package");
                _gpuOptions = options = gpus.Select((h, i) => new GpuOption(i, h.Name)).ToList();
                _gpuIndex = defaultIndex = SelectDefaultGpu(
                    gpus.Select(h => (h.HardwareType, h.Name)).ToList());
            }
            ApplyGpu(gpus.Count > 0 ? gpus[defaultIndex] : null);
            GpuListReady?.Invoke(options, defaultIndex);

            while (!ct.IsCancellationRequested)
            {
                // 应用待换绑的 GPU（本线程内换引用，无跨线程问题）
                int pending;
                lock (_gate) { pending = _pendingGpuIndex; _pendingGpuIndex = -1; }
                if (pending >= 0 && pending < gpus.Count)
                    ApplyGpu(gpus[pending]);

                IHardware? cpu, gpu;
                ISensor? ct2, cp, gt, gp;
                lock (_gate) { cpu = _cpu; gpu = _gpu; ct2 = _cpuTemp; cp = _cpuPower; gt = _gpuTemp; gp = _gpuPower; }
                Update(cpu);
                Update(gpu);
                // 降级口径：null / ≤0 → 该路不可用（AMD CPU 无 PawnIO 时 LHM 常驻 0，物理上不可能是真值）
                var reading = new Reading(Valid(ct2), Valid(cp), Valid(gt), Valid(gp));
                try { Sampled?.Invoke(reading); } catch { /* 订阅方异常不炸轮询 */ }
                ct.WaitHandle.WaitOne(2000); // 2 秒轮询（图表 120 秒窗口 = 60 个点）
            }
        }
        catch (OperationCanceledException) { /* 正常停止 */ }
        catch { /* LHM 打开/读取失败：静默退出（页面保持占位文案） */ }
        finally
        {
            lock (_gate)
            {
                _cpu = _gpu = null;
                _cpuTemp = _cpuPower = _gpuTemp = _gpuPower = null;
                _gpuOptions = [];
                _gpuIndex = _pendingGpuIndex = -1;
            }
            try { computer.Close(); } catch { /* 关闭失败不影响 */ }
        }
    }

    /// <summary>换绑 GPU 硬件与两路传感器引用并重新发 SensorsResolved（四行口径同步刷新）。loop 线程内调用。</summary>
    static void ApplyGpu(IHardware? gpu)
    {
        var gt = Find(gpu, SensorType.Temperature, "GPU Core", "GPU");
        var gp = Find(gpu, SensorType.Power, "Package", "Board", "GPU Core", "GPU Power", "GPU");
        string? cpuName;
        ISensor? ct, cp;
        lock (_gate)
        {
            _gpu = gpu;
            _gpuTemp = gt;
            _gpuPower = gp;
            cpuName = _cpu?.Name;
            ct = _cpuTemp;
            cp = _cpuPower;
        }
        SensorsResolved?.Invoke(new SensorMap(cpuName, ct?.Name, cp?.Name, gt?.Name, gp?.Name, gpu?.Name));
    }

    /// <summary>独显启发式（口径与交接文档一致）：
    /// NVIDIA 一律独显；AMD 名含 "RX"（Radeon RX 系列）独显、以 "Graphics" 结尾的 APU 核显命名
    /// （8060S/780M 等）核显、其余独显；Intel 名含 "Arc" 独显、"UHD"/"Iris Xe" 及其余按核显。</summary>
    internal static bool IsDiscreteGpu(HardwareType type, string name) => type switch
    {
        HardwareType.GpuNvidia => true,
        HardwareType.GpuAmd =>
            name.Contains("RX", StringComparison.OrdinalIgnoreCase) ||
            !name.TrimEnd().EndsWith("Graphics", StringComparison.OrdinalIgnoreCase),
        HardwareType.GpuIntel => name.Contains("Arc", StringComparison.OrdinalIgnoreCase),
        _ => false,
    };

    /// <summary>默认显卡索引：多张独显取第一张；无独显退回第一张卡。</summary>
    internal static int SelectDefaultGpu(IReadOnlyList<(HardwareType Type, string Name)> gpus)
    {
        for (int i = 0; i < gpus.Count; i++)
            if (IsDiscreteGpu(gpus[i].Type, gpus[i].Name)) return i;
        return 0;
    }

    // ═══ PawnIO 驱动：检测 / 静默安装 / 服务拉起（CPU/主板类传感器的前提） ═══

    /// <summary>官方签名安装器（namazso 分发，GPL-2.0 带 IOCTL 独立模块例外），随输出随附，离线可装。</summary>
    public static string InstallerExe =>
        Path.Combine(AppContext.BaseDirectory, "Tools", "pawnio", "PawnIO_setup.exe");

    public static bool HasInstaller => File.Exists(InstallerExe);

    /// <summary>PawnIO 是否已安装（直接查注册表卸载项——LHM 的 PawnIo.IsInstalled 是静态缓存，
    /// 进程内首次访问后不再刷新，装完驱动想即时判断必须自己查注册表）。</summary>
    public static bool IsPawnIoInstalled()
    {
        try
        {
            using var key = Microsoft.Win32.Registry.LocalMachine
                .OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO");
            return key?.GetValue("DisplayVersion") != null;
        }
        catch { return false; }
    }

    /// <summary>静默安装 PawnIO（官方安装器自定义参数：-install -silent；卸载对应 -uninstall -silent，
    /// 安装/卸载均无需重启）。本程序已是管理员运行，直接起安装器即可。
    /// 返回是否安装成功；成功后调用方应 Restart() 重初始化 LHM Computer。</summary>
    public static bool InstallDriver()
    {
        try
        {
            using var p = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = InstallerExe,
                Arguments = "-install -silent",
                WorkingDirectory = Path.GetDirectoryName(InstallerExe)!,
                UseShellExecute = false,
                CreateNoWindow = true,
            })!;
            if (!p.WaitForExit(120_000)) { try { p.Kill(); } catch { } return false; }
            return p.ExitCode == 0 && IsPawnIoInstalled();
        }
        catch { return false; }
    }

    /// <summary>装完驱动后重初始化（Stop + Start，幂等；LHM 重新 Open 即走驱动读数，无需重启程序）。</summary>
    public static void Restart()
    {
        Stop();
        Start();
    }

    /// <summary>已安装但服务未运行时拉起（PawnIO 服务 StartType=Manual，重启系统后不会自动运行；
    /// LHM 只开设备不拉服务）。sc start 已在运行时返回「已启动」错误码，无害忽略。</summary>
    static void EnsurePawnIoService()
    {
        if (!IsPawnIoInstalled()) return;
        try
        {
            using var p = System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = "sc.exe",
                Arguments = "start PawnIO",
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
            })!;
            p.WaitForExit(15_000);
        }
        catch { /* 拉不起来就算了：传感器按不可用降级 */ }
    }

    static void Update(IHardware? hw)
    {
        if (hw == null) return;
        try { hw.Update(); } catch { /* 单次更新失败用旧值 */ }
    }

    static double? Valid(ISensor? s) =>
        s?.Value is float v && v > 0 ? v : null;

    /// <summary>按关键词序优先匹配（每个关键词取第一个名字包含它的传感器），全不中退回该类第一个。</summary>
    static ISensor? Find(IHardware? hw, SensorType type, params string[] keywords)
    {
        if (hw == null) return null;
        var all = hw.Sensors.Where(s => s.SensorType == type).ToList();
        foreach (string k in keywords)
        {
            var hit = all.FirstOrDefault(s => s.Name.Contains(k, StringComparison.OrdinalIgnoreCase));
            if (hit != null) return hit;
        }
        return all.FirstOrDefault();
    }
}
