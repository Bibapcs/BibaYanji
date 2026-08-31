using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace YanJi.Plugin.ScreenDeadPixel;

/// <summary>屏幕坏点检测的全屏色块窗口：无边框覆盖目标屏（Show 后 SetWindowPos 用物理像素精确定位——
/// PerMonitorV2 下 WPF 的 Left/Top 是 DIP，多屏混合缩放必须用物理像素才能盖准），白→红→绿→蓝→黑 循环。
/// 按任意键或单击鼠标左键 = 下一种颜色；Esc = 退出。提示条 3 秒或首次切换后自动消失（不遮挡检查）。</summary>
public class ColorTestWindow : Window
{
    static readonly Color[] Cycle = [Colors.White, Colors.Red, Colors.Lime, Colors.Blue, Colors.Black];
    int _index;
    readonly Border _hint;
    readonly DispatcherTimer _hintTimer;

    /// <param name="x">目标屏左上角 X（物理像素）</param>
    /// <param name="y">目标屏左上角 Y（物理像素）</param>
    /// <param name="width">目标屏宽（物理像素）</param>
    /// <param name="height">目标屏高（物理像素）</param>
    public ColorTestWindow(int x, int y, int width, int height)
    {
        WindowStyle = WindowStyle.None;
        ResizeMode = ResizeMode.NoResize;
        Topmost = true;
        ShowInTaskbar = false;
        // 先按 DIP 近似放置（100% 屏正好），Loaded 里 SetWindowPos 物理像素精修
        Left = x; Top = y; Width = width; Height = height;
        Background = new SolidColorBrush(Cycle[0]);

        // 操作提示：半透明黑底白字，所有纯色下都可读；居中不持久遮挡
        _hint = new Border
        {
            Background = new SolidColorBrush(Color.FromArgb(0x99, 0, 0, 0)),
            CornerRadius = new CornerRadius(8),
            Padding = new Thickness(24, 12, 24, 12),
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
            Child = new TextBlock
            {
                Text = "按任意键或单击鼠标切换颜色，Esc 退出",
                Foreground = Brushes.White,
                FontSize = 20,
            },
        };
        Content = new Grid { Children = { _hint } };

        _hintTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
        _hintTimer.Tick += (_, _) => DismissHint();

        KeyDown += (_, e) =>
        {
            if (e.Key == Key.Escape) Close();
            else Next();
        };
        MouseLeftButtonDown += (_, _) => Next();

        Loaded += (_, _) =>
        {
            var hwnd = new System.Windows.Interop.WindowInteropHelper(this).Handle;
            SetWindowPos(hwnd, HwndTopmost, x, y, width, height, SwpShowwindow);
            _hintTimer.Start();
        };
    }

    /// <summary>切到下一种颜色（白→红→绿→蓝→黑 循环）；首次切换同时消掉提示条。</summary>
    public void Next()
    {
        DismissHint();
        _index = (_index + 1) % Cycle.Length;
        Background = new SolidColorBrush(Cycle[_index]);
    }

    void DismissHint()
    {
        _hint.Visibility = Visibility.Collapsed;
        _hintTimer.Stop();
    }

    [DllImport("user32.dll", SetLastError = true)]
    static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    static readonly IntPtr HwndTopmost = new(-1);
    const uint SwpShowwindow = 0x0040;
}
