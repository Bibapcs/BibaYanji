using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using YanJi.PluginSdk;

namespace YanJi.Plugin.DiskMark;

/// <summary>「硬盘跑分」页：CrystalDiskMark 源码集成（CdmHost 子进程）对目标盘跑
/// SEQ1M Q8T1 顺序读/写 + RND4K Q1T1 随机读/写四项（原版 BibaAutoTest 内存+硬盘合并页
/// 裁掉 AIDA64 内存部分后的 CDM 单页；AIDA64 为商业软件且该部分后续改用开源方案，故不含）。
/// 成绩：JSON 落盘 %APPDATA%\YanJi\disk_result\硬盘基准.json + 追加 硬盘成绩记录.csv。
/// 通过约定同其它模块：确认完成 → PassChanged(true) → 导航圆点变绿 + 自动跳下一模块。</summary>
public partial class DiskMarkPage : UserControl, IModulePage
{
    public event Action<bool>? PassChanged;

    readonly CdmService _cdm = new();
    CdmService.CdmResult? _result;
    CancellationTokenSource? _cts;   // 非 null = 正在跑（防重入）
    bool _passed;

    public DiskMarkPage()
    {
        InitializeComponent();
        // 目标盘下拉：全部就绪的固定盘，默认系统盘
        string systemRoot = Path.GetPathRoot(Environment.SystemDirectory) ?? "C:\\";
        foreach (var d in DriveInfo.GetDrives())
        {
            if (d.DriveType != DriveType.Fixed || !d.IsReady) continue;
            DriveBox.Items.Add(new ComboBoxItem { Content = d.Name.TrimEnd('\\'), Tag = d.Name });
        }
        for (int i = 0; i < DriveBox.Items.Count; i++)
            if (((ComboBoxItem)DriveBox.Items[i]).Tag is string tag &&
                tag.Equals(systemRoot, StringComparison.OrdinalIgnoreCase))
            {
                DriveBox.SelectedIndex = i;
                break;
            }
        if (DriveBox.SelectedIndex < 0 && DriveBox.Items.Count > 0) DriveBox.SelectedIndex = 0;
        if (!CdmService.HostAvailable)
            Log("错误：未找到 CdmHost 负载（应随插件一同分发）：" + CdmService.HostExe);
        // 页面常驻机制下切走即从视觉树移除：兜底停跑分
        Unloaded += (_, _) => Cleanup();
        UpdatePassHint();
    }

    /// <summary>静态兜底：程序关闭时杀跑分进程树（插件入口 IPluginShutdown 调）。</summary>
    public static void CleanupStatic() => CdmService.KillCurrent();

    void Cleanup()
    {
        try { _cts?.Cancel(); } catch { }
        CdmService.KillCurrent();
    }

    // ═══ 跑分流程 ═══

    async void RunButton_Click(object sender, RoutedEventArgs e)
    {
        if (_cts != null) return; // 防重入
        if (!CdmService.HostAvailable)
        {
            Log("错误：未找到 CdmHost 负载（应随插件一同分发）：" + CdmService.HostExe);
            return;
        }
        string drive = (DriveBox.SelectedItem as ComboBoxItem)?.Tag as string ?? "C:\\";

        _result = null;
        ScoreSeqRead.Text = "—";
        ScoreSeqWrite.Text = "—";
        Score4KRead.Text = "—";
        Score4KWrite.Text = "—";
        DetailText.Text = "";
        TestProgress.Value = 0;
        TestProgress.IsIndeterminate = false;

        _cts = new CancellationTokenSource();
        RunButton.IsEnabled = false;
        CancelButton.IsEnabled = true;
        SetRunning();
        try
        {
            Log($"开始硬盘基准（{drive.TrimEnd('\\')} 盘，SEQ1M Q8T1 + RND4K Q1T1）");
            TestProgress.IsIndeterminate = true;
            var progress = new Progress<string>(p =>
                Dispatcher.BeginInvoke(() => PhaseText.Text = p));
            var res = await _cdm.RunAsync(drive, progress, _cts.Token);
            TestProgress.IsIndeterminate = false;
            TestProgress.Value = 100;
            _result = res;
            ScoreSeqRead.Text = res.SeqRead.ToString("0.0");
            ScoreSeqWrite.Text = res.SeqWrite.ToString("0.0");
            Score4KRead.Text = res.Rnd4KRead.ToString("0.0");
            Score4KWrite.Text = res.Rnd4KWrite.ToString("0.0");
            DetailText.Text = $"顺序 {res.SeqRead:0.0} / {res.SeqWrite:0.0} MB/s，4K 随机 {res.Rnd4KRead:0.0} / {res.Rnd4KWrite:0.0} MB/s"
                + $"（读 / 写）；成绩已保存：{res.JsonPath}";
            SetIdle("测试完成");
            Log($"完成：顺序 {res.SeqRead:0.0} / {res.SeqWrite:0.0}，4K 随机 {res.Rnd4KRead:0.0} / {res.Rnd4KWrite:0.0} MB/s（读/写）");
        }
        catch (OperationCanceledException)
        {
            SetIdle("已取消");
            Log("已取消");
        }
        catch (Exception ex)
        {
            SetIdle("出错");
            Log("错误：" + ex.Message.Split('\n')[0]);
        }
        finally
        {
            _cts.Dispose();
            _cts = null;
            RunButton.IsEnabled = true;
            CancelButton.IsEnabled = false;
            TestProgress.IsIndeterminate = false;
            PhaseText.Text = "";
            UpdatePassHint();
        }
    }

    void CancelButton_Click(object sender, RoutedEventArgs e) => _cts?.Cancel();

    void OpenResultDir_Click(object sender, RoutedEventArgs e)
    {
        Directory.CreateDirectory(CdmService.ResultDir);
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
        {
            FileName = "explorer.exe",
            Arguments = $"\"{CdmService.ResultDir}\"",
            UseShellExecute = true,
        });
    }

    // ═══ 状态行 / 通过 ═══

    void SetRunning()
    {
        _passed = false;
        StatusText.Text = "跑分中…";
        StatusText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
    }

    void SetIdle(string text)
    {
        if (_passed) return;
        StatusText.Text = text;
        StatusText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
    }

    void PassButton_Click(object sender, RoutedEventArgs e)
    {
        if (_cts != null) _cts.Cancel(); // 跑分中点通过：先取消（进程树 Kill 走 ct.Register）
        _passed = true;
        StatusText.Text = "已通过";
        StatusText.Foreground = Brushes.ForestGreen;
        PassChanged?.Invoke(true);
    }

    void UpdatePassHint()
    {
        PassHintText.Text = _cts != null ? "正在跑分"
            : _result == null ? "尚未跑过分"
            : "已有成绩，可人工判断后通过";
    }

    // ═══ 日志（LogBox 惯例：滚底 + 500 条截旧） ═══

    void Log(string message)
    {
        LogBox.AppendText($"{DateTime.Now:HH:mm:ss}  {message}\n");
        if (LogBox.LineCount > 500)
        {
            int cut = LogBox.Text.IndexOf('\n');
            if (cut > 0) LogBox.Text = LogBox.Text[(cut + 1)..];
        }
        LogBox.ScrollToEnd();
    }
}
