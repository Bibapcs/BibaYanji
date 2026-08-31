using System.Diagnostics;
using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Windows;

namespace YanJi.Plugin.StressTest;

/// <summary>散热测试进程管理：prime95（CPU 烤机）与 FurMark（GPU 烤机）的起停与兜底清理。
/// 关键安全约束：任何路径都不能留下后台烤机进程——
/// ① 停止按钮杀进程树；② 页面 Unloaded（切走/关窗）自动停止；③ 子进程挂进 Kill-On-Close 的
/// Windows Job 对象，即使本程序崩溃/被杀，系统也会连座杀掉烤机进程。</summary>
public static class StressTestService
{
    /// <summary>烤机模式。</summary>
    public enum StressMode { Cpu, Gpu, Both }

    /// <summary>FFT 预设（简明自定义口径，非官方对话框的缓存自适应预设）。</summary>
    public enum FftPreset { Blend, SmallHot, LargeMem }

    /// <summary>FurMark 启动参数（ShowWindow=false 时传 --hidden，窗口创建即不可见、渲染循环照常）。</summary>
    public record FurMarkSettings(int Width, int Height, bool Fullscreen, int Msaa, bool ShowWindow = true);

    /// <summary>prime95 启动参数（Threads = 用户要的烤机线程数，内部换算 TortureCores + TortureHyperthreading；
    /// ShowWindow=false 时 prime.txt 写 [Windows] HideIcon=1/TrayIcon=0 且以隐藏窗口样式启动，实测无任何可见窗口）。</summary>
    public record Prime95Settings(int Threads, FftPreset Fft, bool ShowWindow = true);

    static readonly object _gate = new();
    static Process? _prime95;
    static Process? _furmark;
    static DateTime? _prime95Started, _furmarkStarted; // 各工具本次起点（停止日志算运行时长）
    static bool _stopping;      // 我们主动停止中（Exited 事件据此区分「它自己死的」）
    static IntPtr _job; // Kill-On-Close Job（句柄随进程退出自动关闭 → OS 连座杀子进程）
    static bool _exitHooked;

    /// <summary>最近一次成功开始烤机的时刻（页面计时器读）。</summary>
    public static DateTime? StartedAt { get; private set; }

    /// <summary>当前是否在烤（任一进程存活）。</summary>
    public static bool IsRunning
    {
        get { lock (_gate) return Alive(_prime95) || Alive(_furmark); }
    }

    /// <summary>起停状态变化（页面订阅刷新按钮/状态行；调用线程不定，页面需封送 UI 线程）。</summary>
    public static event Action? StateChanged;

    /// <summary>烤机事件日志（HH:mm:ss 前缀完整行；可能在线程池线程触发，订阅方封送 UI）。</summary>
    public static event Action<string>? Logged;

    static void Log(string msg)
    {
        try { Logged?.Invoke($"{DateTime.Now:HH:mm:ss}  {msg}"); } catch { /* 订阅方异常不炸服务 */ }
    }

    public static string Prime95Exe => Path.Combine(AppContext.BaseDirectory, "Tools", "prime95", "prime95.exe");
    public static string FurMarkExe => Path.Combine(AppContext.BaseDirectory, "Tools", "furmark", "FurMark.exe");
    public static bool HasPrime95 => File.Exists(Prime95Exe);
    public static bool HasFurMark => File.Exists(FurMarkExe);

    // ═══ CPU 信息（WMI，缓存一次） ═══

    static (int Physical, int Logical)? _cores;

    /// <summary>物理核心数 / 逻辑处理器数（WMI Win32_Processor 求和；失败回退 Environment.ProcessorCount）。</summary>
    public static (int Physical, int Logical) CoreCounts
    {
        get
        {
            if (_cores == null)
            {
                int phys = 0, logical = 0;
                try
                {
                    using var searcher = new ManagementObjectSearcher(@"root\cimv2",
                        "SELECT NumberOfCores, NumberOfLogicalProcessors FROM Win32_Processor");
                    foreach (ManagementObject mo in searcher.Get())
                    {
                        phys += Convert.ToInt32(mo["NumberOfCores"]);
                        logical += Convert.ToInt32(mo["NumberOfLogicalProcessors"]);
                    }
                }
                catch { /* WMI 失败走回退 */ }
                int fallback = Environment.ProcessorCount;
                _cores = (phys > 0 ? phys : fallback, logical > 0 ? logical : fallback);
            }
            return _cores.Value;
        }
    }

    /// <summary>物理内存总量（MB，WMI Win32_ComputerSystem；失败给 8192）。</summary>
    public static long TotalMemoryMB
    {
        get
        {
            try
            {
                using var searcher = new ManagementObjectSearcher(@"root\cimv2",
                    "SELECT TotalPhysicalMemory FROM Win32_ComputerSystem");
                foreach (ManagementObject mo in searcher.Get())
                    return Convert.ToInt64(mo["TotalPhysicalMemory"]) / 1048576;
            }
            catch { /* 失败给保守默认 */ }
            return 8192;
        }
    }

    // ═══ 起停 ═══

    /// <summary>按模式启动烤机。已在烤会先停再起。工具缺失抛 FileNotFoundException（页面转成状态行提示）。</summary>
    public static void Start(StressMode mode, Prime95Settings p95, FurMarkSettings fm)
    {
        if (mode is StressMode.Cpu or StressMode.Both) EnsureTool(Prime95Exe);
        if (mode is StressMode.Gpu or StressMode.Both) EnsureTool(FurMarkExe);

        lock (_gate)
        {
            StopLocked();
            HookExitLocked();
            EnsureJobLocked();

            Log($"开始烤机：{ModeName(mode)}");
            if (mode is StressMode.Cpu or StressMode.Both)
            {
                _prime95 = WatchExit(StartPrime95(p95), "Prime95");
                _prime95Started = DateTime.Now;
                Log($"Prime95 已启动（{p95.Threads} 线程，{FftName(p95.Fft)}，界面{(p95.ShowWindow ? "显示" : "隐藏")}）");
            }
            if (mode is StressMode.Gpu or StressMode.Both)
            {
                _furmark = WatchExit(StartFurMark(fm), "FurMark");
                _furmarkStarted = DateTime.Now;
                Log($"FurMark 已启动（{fm.Width}×{fm.Height}，{MsaaName(fm.Msaa)}，界面{(fm.ShowWindow ? "显示" : "隐藏")}）");
            }
            StartedAt = DateTime.Now;
        }
        StateChanged?.Invoke();
    }

    static string ModeName(StressMode mode) => mode switch
    {
        StressMode.Cpu => "CPU 单烤",
        StressMode.Gpu => "GPU 单烤",
        _ => "双烤",
    };

    static string FftName(FftPreset fft) => fft switch
    {
        FftPreset.SmallHot => "小 FFT（高热）",
        FftPreset.LargeMem => "大 FFT（压内存）",
        _ => "均衡 Blend",
    };

    static string MsaaName(int msaa) => msaa > 0 ? $"{msaa}x MSAA" : "无 MSAA";

    /// <summary>停止全部烤机进程（杀进程树，等退出）。任何时刻调用都安全。</summary>
    public static void Stop()
    {
        lock (_gate)
        {
            StopLocked();
            StartedAt = null;
        }
        StateChanged?.Invoke();
    }

    static void StopLocked()
    {
        _stopping = true; // 主动停止：Exited 事件不再记「意外退出」
        try
        {
            KillTree(ref _prime95, "Prime95", _prime95Started);
            KillTree(ref _furmark, "FurMark", _furmarkStarted);
            _prime95Started = _furmarkStarted = null;
        }
        finally { _stopping = false; }
    }

    static void KillTree(ref Process? proc, string name, DateTime? started)
    {
        var p = proc;
        proc = null;
        if (p == null) return;
        bool wasAlive = false;
        try { wasAlive = !p.HasExited; } catch { /* 状态不明按已退出处理 */ }
        try { if (wasAlive) p.Kill(entireProcessTree: true); } catch { /* 已退出/权限问题忽略 */ }
        try { p.WaitForExit(5000); } catch { /* 超时不等 */ }
        p.Dispose();
        if (wasAlive && started is DateTime t0)
            Log($"{name} 已停止（运行 {FormatDuration(DateTime.Now - t0)}）");
    }

    /// <summary>挂 Exited 事件：非预期退出（崩溃/被外部杀）记日志并刷新状态；我们主动 Kill 的不记。</summary>
    static Process WatchExit(Process p, string displayName)
    {
        p.EnableRaisingEvents = true;
        p.Exited += (_, _) => OnProcessExited(p, displayName);
        return p;
    }

    static void OnProcessExited(Process p, string name)
    {
        int code;
        try { code = p.ExitCode; } catch { code = -1; }
        bool expected;
        lock (_gate)
        {
            expected = _stopping;
            if (ReferenceEquals(_prime95, p)) { _prime95 = null; _prime95Started = null; }
            else if (ReferenceEquals(_furmark, p)) { _furmark = null; _furmarkStarted = null; }
            if (!expected && !Alive(_prime95) && !Alive(_furmark)) StartedAt = null;
        }
        if (!expected)
        {
            Log($"{name} 意外退出（代码 {code}）");
            StateChanged?.Invoke();
        }
    }

    /// <summary>运行时长：「38 秒」/「12 分 34 秒」/「1 小时 2 分」。</summary>
    static string FormatDuration(TimeSpan s) =>
        s.TotalHours >= 1 ? $"{(int)s.TotalHours} 小时 {s.Minutes} 分"
        : s.TotalMinutes >= 1 ? $"{(int)s.TotalMinutes} 分 {s.Seconds} 秒"
        : $"{s.Seconds} 秒";

    static bool Alive(Process? p)
    {
        if (p == null) return false;
        try { return !p.HasExited; } catch { return false; }
    }

    static void EnsureTool(string path)
    {
        if (!File.Exists(path))
            throw new FileNotFoundException("烤机工具缺失：" + path, path);
    }

    // ═══ prime95（-t 即启动烤机；配置写 exe 同目录 prime.txt，机制见交接文档） ═══

    static Process StartPrime95(Prime95Settings settings)
    {
        string dir = Path.GetDirectoryName(Prime95Exe)!;
        WritePrimeTxt(Path.Combine(dir, "prime.txt"), settings);
        var p = Process.Start(new ProcessStartInfo
        {
            FileName = Prime95Exe,
            Arguments = "-t",
            WorkingDirectory = dir,
            UseShellExecute = false,
            // 隐藏模式双保险之一：wShowWindow=SW_HIDE 成为 prime95 的 orig_cmdShow，
            // -t 分支的 ShowWindow(orig_cmdShow) 即 SW_HIDE（源码 Prime95.cpp 已核对）
            WindowStyle = settings.ShowWindow ? ProcessWindowStyle.Normal : ProcessWindowStyle.Hidden,
        })!;
        AssignToJob(p);
        return p;
    }

    /// <summary>写 prime.txt：TortureCores（烤机 worker 数，上限物理核数；超出部分靠 TortureHyperthreading 吃满
    /// 逻辑线程）+ FFT 预设 + StressTester/UsePrimenet（跳过首次运行向导、不联网）。</summary>
    static void WritePrimeTxt(string path, Prime95Settings settings)
    {
        var (phys, logical) = CoreCounts;
        int threads = Math.Clamp(settings.Threads, 1, logical);
        // 线程数 ≤ 物理核：按核数起 worker、关超线程；> 物理核：worker 拉满物理核、开超线程吃满逻辑线程
        int cores = Math.Min(threads, phys);
        bool ht = threads > phys;

        // FFT 预设：均衡 Blend 4–4096K 用大内存；小 FFT 全 in-place（TortureMem=8）发热最大；大 FFT 压内存
        long mem70 = Math.Min((long)(TotalMemoryMB * 0.7), (long)(TotalMemoryMB * 0.9));
        var (minFft, maxFft, memMB) = settings.Fft switch
        {
            FftPreset.SmallHot => (4, 64, 8),
            FftPreset.LargeMem => (1024, 4096, (int)Math.Max(1024, mem70)),
            _ => (4, 4096, (int)Math.Max(1024, mem70)),
        };

        File.WriteAllLines(path,
        [
            "; 由「笔吧验机」散热测试模块生成（prime95 启动时读取，勿手工编辑）",
            "StressTester=1",
            "UsePrimenet=0",
            $"TortureCores={cores}",
            $"TortureHyperthreading={(ht ? 1 : 0)}",
            $"MinTortureFFT={minFft}",
            $"MaxTortureFFT={maxFft}",
            $"TortureMem={memMB}",
            "TortureTime=3",
            // 隐藏模式双保险之二：HideIcon=1 → SetWindowPlacement(SW_HIDE)（窗口从未显示）；
            // TrayIcon 默认 1，两种模式都显式关（隐藏模式不留托盘图标，显示模式也只留任务栏窗口）
            "[Windows]",
            $"HideIcon={(settings.ShowWindow ? 0 : 1)}",
            "TrayIcon=0",
        ]);
    }

    // ═══ FurMark（C# / OpenTK 重实现，参数即原版语义） ═══

    static Process StartFurMark(FurMarkSettings settings)
    {
        string dir = Path.GetDirectoryName(FurMarkExe)!;
        string args = $"--width {settings.Width} --height {settings.Height} --msaa {settings.Msaa}"
                    + (settings.Fullscreen ? " --fullscreen" : "")
                    + (settings.ShowWindow ? "" : " --hidden"); // 隐藏模式：窗口创建即不可见（StartVisible=false）
        var p = Process.Start(new ProcessStartInfo
        {
            FileName = FurMarkExe,
            Arguments = args,
            WorkingDirectory = dir,
            UseShellExecute = false,
        })!;
        AssignToJob(p);
        return p;
    }

    // ═══ 兜底清理：退出钩子 + Kill-On-Close Job ═══

    /// <summary>启动前清理本工具目录下此前残留的烤机进程（按完整路径匹配，不误杀用户别的同名程序）。</summary>
    static void KillStaleByPath(string exeName, string expectPath)
    {
        foreach (var p in Process.GetProcessesByName(exeName))
        {
            try
            {
                if (string.Equals(p.MainModule?.FileName, expectPath, StringComparison.OrdinalIgnoreCase))
                {
                    p.Kill(entireProcessTree: true);
                    p.WaitForExit(3000);
                }
            }
            catch { /* 访问被拒/已退出：跳过 */ }
            finally { p.Dispose(); }
        }
    }

    static void HookExitLocked()
    {
        if (_exitHooked) return;
        _exitHooked = true;
        // 正常退出 / 未处理异常终止时尽力清理（硬崩溃由 Job 对象兜底）
        AppDomain.CurrentDomain.ProcessExit += (_, _) => StopLocked();
        if (Application.Current != null)
            Application.Current.Exit += (_, _) => StopLocked();
    }

    static void EnsureJobLocked()
    {
        if (_job == IntPtr.Zero)
        {
            _job = CreateJobObject(IntPtr.Zero, null);
            if (_job == IntPtr.Zero) return; // Job 创建失败仍靠 Kill/钩子兜底
            var info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(_job, JobObjectInfoType.ExtendedLimitInformation,
                ref info, (uint)Marshal.SizeOf<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>());
        }
        // 顺手清理上次异常退出残留的同名进程（prime95 有单实例互斥，残留会让新实例静默退出）
        KillStaleByPath("prime95", Prime95Exe);
        KillStaleByPath("FurMark", FurMarkExe);
    }

    static void AssignToJob(Process p)
    {
        if (_job == IntPtr.Zero) return;
        try { AssignProcessToJobObject(_job, p.Handle); } catch { /* 加入失败仍靠 Kill/钩子兜底 */ }
    }

    // ═══ Job Object P/Invoke ═══

    const int JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateJobObject(IntPtr lpJobAttributes, string? lpName);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetInformationJobObject(IntPtr hJob, int infoClass,
        ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION lpJobObjectInfo, uint cbJobObjectInfoLength);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AssignProcessToJobObject(IntPtr hJob, IntPtr hProcess);

    static class JobObjectInfoType { public const int ExtendedLimitInformation = 9; }

    [StructLayout(LayoutKind.Sequential)]
    struct IO_COUNTERS
    {
        public ulong ReadOperationCount, WriteOperationCount, OtherOperationCount;
        public ulong ReadTransferCount, WriteTransferCount, OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct JOBOBJECT_BASIC_LIMIT_INFORMATION
    {
        public long PerProcessUserTimeLimit, PerJobUserTimeLimit;
        public int LimitFlags;
        public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize;
        public int ActiveProcessLimit;
        public UIntPtr Affinity;
        public int PriorityClass, SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        public IO_COUNTERS IoInfo;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }
}
