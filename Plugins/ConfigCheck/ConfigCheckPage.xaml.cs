using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using YanJi.PluginSdk.Services;

namespace YanJi.Plugin.ConfigCheck;

/// <summary>「配置核对」页面：进入页面即后台自动采集硬件信息（LibreHardwareMonitor + WMI + EDID）→
/// 分组卡片展示 → 人工逐项核对后点「确认无误，通过」。通过后状态行变绿并通知 MainWindow 把导航圆点变绿；
/// 「重新检测」会清空通过态重新采集。</summary>
public partial class ConfigCheckPage : UserControl, YanJi.PluginSdk.IModulePage
{
    bool _running;
    bool _autoStarted; // 页面常驻机制下 Loaded 可能重入（切走再切回），自动检测只触发一次

    /// <summary>核对完成状态变化（true = 已通过，false = 重新检测回到待核对）；MainWindow 据此同步导航圆点。</summary>
    public event Action<bool>? PassChanged;

    /// <summary>当前是否已核对通过。</summary>
    public bool Passed { get; private set; }

    public ConfigCheckPage()
    {
        InitializeComponent();
        // 进入页面即自动检测一次（手动再次采集走「重新检测」按钮）
        Loaded += (_, _) =>
        {
            if (_autoStarted) return;
            _autoStarted = true;
            StartDetect();
        };
    }

    void RunButton_Click(object sender, RoutedEventArgs e) => StartDetect();

    async void StartDetect()
    {
        if (_running) return;
        _running = true;
        SetPassed(false);
        RunButton.IsEnabled = false;
        PassButton.IsEnabled = false;
        DetectProgress.Value = 0;
        ResultsHost.ItemsSource = null;
        try
        {
            // WMI/LHM/EDID 采集放后台线程，进度经 IProgress 回报（不卡 UI）
            var progress = new Progress<HardwareInfoService.CollectProgress>(p =>
            {
                DetectProgress.Value = 100.0 * p.Step / p.Total;
                PhaseText.Text = $"正在采集：{p.Stage}（{p.Step + 1}/{p.Total}）";
            });
            var report = await Task.Run(() => HardwareInfoService.Collect(progress));
            ResultsHost.ItemsSource = report.Groups;
            DetectProgress.Value = 100;
            PhaseText.Text = $"检测完成（{report.CollectedAt:HH:mm:ss}），请逐项核对下方配置信息";
            PassButton.IsEnabled = true;
        }
        catch (Exception ex)
        {
            PhaseText.Text = "检测出错：" + ex.Message;
        }
        finally
        {
            RunButton.IsEnabled = true;
            _running = false;
        }
    }

    void PassButton_Click(object sender, RoutedEventArgs e)
    {
        SetPassed(true);
        PassButton.IsEnabled = false;
        PhaseText.Text = "已确认通过；如更换硬件或需复查，可点「重新检测」";
    }

    /// <summary>更新核对状态行（通过 = 绿色「✓ 已核对通过」）并通知导航圆点。</summary>
    void SetPassed(bool passed)
    {
        Passed = passed;
        if (passed)
        {
            StatusText.Text = "✓ 已核对通过";
            StatusText.Foreground = Brushes.ForestGreen;
        }
        else
        {
            StatusText.Text = "待核对";
            StatusText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
        }
        PassChanged?.Invoke(passed);
    }
}
