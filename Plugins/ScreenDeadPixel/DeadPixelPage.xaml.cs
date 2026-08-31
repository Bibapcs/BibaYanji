using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using YanJi.PluginSdk.Services;

namespace YanJi.Plugin.ScreenDeadPixel;

/// <summary>「屏幕坏点」页面：选显示器 → 全屏纯色（白/红/绿/蓝/黑）坏点检测。
/// 通过约定同其它模块：确认无坏点 → PassChanged(true) → 导航圆点变绿 + 自动跳下一模块。</summary>
public partial class DeadPixelPage : UserControl, YanJi.PluginSdk.IModulePage
{
    List<DisplayInfo> _monitors = [];
    ColorTestWindow? _testWindow;

    /// <summary>核对完成状态变化（true = 已通过，false = 重新测试回到待测试）；MainWindow 据此同步导航圆点并自动跳转。</summary>
    public event Action<bool>? PassChanged;

    /// <summary>当前是否已确认通过。</summary>
    public bool Passed { get; private set; }

    public DeadPixelPage()
    {
        InitializeComponent();
        Loaded += (_, _) => RefreshMonitors();
    }

    /// <summary>枚举活动显示器填充下拉框（默认选主屏 = 坐标原点 (0,0) 的那台）。</summary>
    void RefreshMonitors()
    {
        try { _monitors = DisplayInfoService.GetDisplays(); }
        catch { _monitors = []; }

        MonitorBox.Items.Clear();
        int primary = 0;
        for (int i = 0; i < _monitors.Count; i++)
        {
            var d = _monitors[i];
            string name = d.Edid is { PanelName.Length: > 0 } e ? e.PanelName : d.GdiName;
            MonitorBox.Items.Add($"#{i + 1} {name} ({d.Width}×{d.Height})");
            if (d is { X: 0, Y: 0 }) primary = i;
        }
        if (MonitorBox.Items.Count > 0) MonitorBox.SelectedIndex = primary;
        StartButton.IsEnabled = MonitorBox.Items.Count > 0;
    }

    void StartButton_Click(object sender, RoutedEventArgs e)
    {
        if (_testWindow != null || MonitorBox.SelectedIndex < 0 || MonitorBox.SelectedIndex >= _monitors.Count)
            return;
        // 重新测试 = 回到待核对（与配置核对页「重新检测」同约定）
        SetPassed(false);
        PassButton.IsEnabled = true;

        var d = _monitors[MonitorBox.SelectedIndex];
        _testWindow = new ColorTestWindow(d.X, d.Y, d.Width, d.Height);
        _testWindow.Closed += (_, _) =>
        {
            _testWindow = null;
            StartButton.IsEnabled = true;
            TestingHint.Text = "";
        };
        StartButton.IsEnabled = false;
        TestingHint.Text = "测试进行中：按任意键或单击鼠标切换颜色，Esc 退出";
        _testWindow.Show();
    }

    void PassButton_Click(object sender, RoutedEventArgs e)
    {
        SetPassed(true);
        PassButton.IsEnabled = false;
    }

    /// <summary>更新测试状态行（通过 = 绿色「✓ 已确认通过」）并通知导航圆点/自动跳转。</summary>
    void SetPassed(bool passed)
    {
        Passed = passed;
        if (passed)
        {
            StatusText.Text = "✓ 已确认通过";
            StatusText.Foreground = Brushes.ForestGreen;
        }
        else
        {
            StatusText.Text = "待测试";
            StatusText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
        }
        PassChanged?.Invoke(passed);
    }
}
