using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace YanJi.Plugin.KeyboardTest;

/// <summary>「键盘测试」页面：全尺寸 ANSI 104 键可视化键盘（u 单位坐标 + Canvas 自绘），
/// 按物理键实时反馈（按住 = 强调色高亮，松开 = 绿色已测试）。
/// 按键事件由 MainWindow 经 PreviewKeyDown/Up 路由进来（页面不直接订阅，避免焦点/非激活干扰）。
/// 通过约定同配置核对页：确认无故障 → PassChanged(true) → 导航圆点变绿 + 自动跳下一模块。</summary>
public partial class KeyboardTestPage : UserControl, YanJi.PluginSdk.IModulePage, YanJi.PluginSdk.IKeyHandlerPage
{
    const double U = 48;  // 1u（标准键宽）像素
    const double Gap = 6; // 键帽间隙像素

    /// <summary>一个键帽的可视元素与状态。</summary>
    class KeyVisual
    {
        public required string Label { get; init; }
        public required Border Bd { get; init; }
        public required TextBlock Tb { get; init; }
        public bool Tested { get; set; }
    }

    readonly List<KeyVisual> _all = [];
    readonly Dictionary<Key, List<KeyVisual>> _keyMap = [];
    int _testedCount;

    /// <summary>核对完成状态变化（true = 已通过，false = 重置回到待测试）；MainWindow 据此同步导航圆点并自动跳转。</summary>
    public event Action<bool>? PassChanged;

    /// <summary>当前是否已确认通过。</summary>
    public bool Passed { get; private set; }

    public KeyboardTestPage()
    {
        InitializeComponent();
        BuildKeyboard();
    }

    // ═══ 键盘生成（u 单位坐标 → Canvas 绝对定位） ═══

    void BuildKeyboard()
    {
        KeyboardCanvas.Width = 23 * U;   // 总宽 23u = 主键区 15u + 0.5 + 编辑区 3u + 0.5 + 小键盘 4u
        KeyboardCanvas.Height = 6.5 * U; // 功能行 1u + 0.5 + 主键区 5 行
        foreach (var cap in BuildLayout())
        {
            var tb = new TextBlock
            {
                Text = cap.Label,
                FontSize = cap.Label.Length > 3 ? 10 : 12,
                HorizontalAlignment = HorizontalAlignment.Center,
            };
            tb.SetResourceReference(TextBlock.ForegroundProperty, "TextBrush");
            var bd = new Border
            {
                CornerRadius = new CornerRadius(4),
                BorderThickness = new Thickness(1),
                Width = cap.W * U - Gap,
                Height = cap.H * U - Gap,
                Child = tb,
            };
            bd.SetResourceReference(Border.BackgroundProperty, "SurfaceBgBrush");
            bd.SetResourceReference(Border.BorderBrushProperty, "BorderStrongBrush");
            Canvas.SetLeft(bd, cap.X * U + Gap / 2);
            Canvas.SetTop(bd, cap.Y * U + Gap / 2);
            KeyboardCanvas.Children.Add(bd);

            var v = new KeyVisual { Label = cap.Label, Bd = bd, Tb = tb };
            _all.Add(v);
            if (!_keyMap.TryGetValue(cap.Key, out var list))
                _keyMap[cap.Key] = list = [];
            list.Add(v);
        }
        KeyProgress.Maximum = _all.Count;
        ProgressText.Text = $"已测试 0 / {_all.Count}";
    }

    /// <summary>全尺寸 ANSI 104 键布局：主键区 + F1-F12 功能行 + 编辑/方向键区 + 数字小键盘。
    /// 坐标/尺寸单位 u（标准键宽比：Backspace 2u、Tab 1.5u、Caps 1.75u、Enter 2.25u、
    /// LShift 2.25u、RShift 2.75u、Space 6.25u、底行修饰键 1.25u，小键盘 +/Enter 双高 1x2u）。</summary>
    static List<KeyCap> BuildLayout()
    {
        var caps = new List<KeyCap>(104);

        // 功能行（y=0）：Esc、F1-F12（四键一组，组间 0.5u）、PrtSc/ScrLk/Pause
        caps.Add(new KeyCap("Esc", Key.Escape, 0, 0));
        double fx = 2;
        for (int i = 1; i <= 12; i++)
        {
            caps.Add(new KeyCap($"F{i}", (Key)((int)Key.F1 + i - 1), fx, 0));
            fx += 1;
            if (i is 4 or 8) fx += 0.5; // F4 / F8 后留组间间隙
        }
        caps.Add(new KeyCap("PrtSc", Key.PrintScreen, 15.5, 0));
        caps.Add(new KeyCap("ScrLk", Key.Scroll, 16.5, 0));
        caps.Add(new KeyCap("Pause", Key.Pause, 17.5, 0));

        // 数字行（y=1.5，功能行与主键区间隔 0.5u）
        Row(caps, 1.5, 0, ("`~", Key.OemTilde, 1), ("1!", Key.D1, 1), ("2@", Key.D2, 1), ("3#", Key.D3, 1),
            ("4$", Key.D4, 1), ("5%", Key.D5, 1), ("6^", Key.D6, 1), ("7&", Key.D7, 1), ("8*", Key.D8, 1),
            ("9(", Key.D9, 1), ("0)", Key.D0, 1), ("-_", Key.OemMinus, 1), ("=+", Key.OemPlus, 1),
            ("Backspace", Key.Back, 2));
        Row(caps, 1.5, 15.5, ("Ins", Key.Insert, 1), ("Home", Key.Home, 1), ("PgUp", Key.PageUp, 1));
        Row(caps, 1.5, 19, ("Num", Key.NumLock, 1), ("/", Key.Divide, 1), ("*", Key.Multiply, 1), ("-", Key.Subtract, 1));

        // QWERTY 行（y=2.5）；小键盘 + 为双高键（y 2.5 - 4.5）
        Row(caps, 2.5, 0, ("Tab", Key.Tab, 1.5), ("Q", Key.Q, 1), ("W", Key.W, 1), ("E", Key.E, 1), ("R", Key.R, 1),
            ("T", Key.T, 1), ("Y", Key.Y, 1), ("U", Key.U, 1), ("I", Key.I, 1), ("O", Key.O, 1), ("P", Key.P, 1),
            ("[{", Key.OemOpenBrackets, 1), ("]}", Key.OemCloseBrackets, 1), (@"\|", Key.OemPipe, 1.5));
        Row(caps, 2.5, 15.5, ("Del", Key.Delete, 1), ("End", Key.End, 1), ("PgDn", Key.PageDown, 1));
        Row(caps, 2.5, 19, ("7", Key.NumPad7, 1), ("8", Key.NumPad8, 1), ("9", Key.NumPad9, 1));
        caps.Add(new KeyCap("+", Key.Add, 22, 2.5, 1, 2));

        // 主键行（y=3.5）
        Row(caps, 3.5, 0, ("Caps", Key.Capital, 1.75), ("A", Key.A, 1), ("S", Key.S, 1), ("D", Key.D, 1),
            ("F", Key.F, 1), ("G", Key.G, 1), ("H", Key.H, 1), ("J", Key.J, 1), ("K", Key.K, 1), ("L", Key.L, 1),
            (";:", Key.OemSemicolon, 1), ("'\"", Key.OemQuotes, 1), ("Enter", Key.Return, 2.25));
        Row(caps, 3.5, 19, ("4", Key.NumPad4, 1), ("5", Key.NumPad5, 1), ("6", Key.NumPad6, 1));

        // Shift 行（y=4.5）；小键盘 Enter 为双高键（y 4.5 - 6.5）
        // 注意：主 Enter 与小键盘 Enter 在 WPF 里同为 Key.Return（不暴露扫描码），按任意一个两者一起标记
        Row(caps, 4.5, 0, ("Shift", Key.LeftShift, 2.25), ("Z", Key.Z, 1), ("X", Key.X, 1), ("C", Key.C, 1),
            ("V", Key.V, 1), ("B", Key.B, 1), ("N", Key.N, 1), ("M", Key.M, 1), (",<", Key.OemComma, 1),
            (".>", Key.OemPeriod, 1), ("/?", Key.OemQuestion, 1), ("Shift", Key.RightShift, 2.75));
        caps.Add(new KeyCap("↑", Key.Up, 16.5, 4.5));
        Row(caps, 4.5, 19, ("1", Key.NumPad1, 1), ("2", Key.NumPad2, 1), ("3", Key.NumPad3, 1));
        caps.Add(new KeyCap("Enter", Key.Return, 22, 4.5, 1, 2));

        // 底行（y=5.5）
        Row(caps, 5.5, 0, ("Ctrl", Key.LeftCtrl, 1.25), ("Win", Key.LWin, 1.25), ("Alt", Key.LeftAlt, 1.25),
            ("Space", Key.Space, 6.25), ("Alt", Key.RightAlt, 1.25), ("Win", Key.RWin, 1.25),
            ("Menu", Key.Apps, 1.25), ("Ctrl", Key.RightCtrl, 1.25));
        Row(caps, 5.5, 15.5, ("←", Key.Left, 1), ("↓", Key.Down, 1), ("→", Key.Right, 1));
        Row(caps, 5.5, 19, ("0", Key.NumPad0, 2), (".", Key.Decimal, 1));

        return caps;
    }

    static void Row(List<KeyCap> caps, double y, double x, params (string Label, Key Key, double W)[] keys)
    {
        foreach (var (label, key, w) in keys)
        {
            caps.Add(new KeyCap(label, key, x, y, w));
            x += w;
        }
    }

    // ═══ 按键反馈（MainWindow 路由） ═══

    /// <summary>处理一次物理按键（down=true 按下 / false 抬起）。返回是否有对应键位（没有则事件不吞）。
    /// 调用方（MainWindow）已过滤 IsRepeat，长按连发不会刷到这里。</summary>
    public bool HandleKey(Key key, bool down)
    {
        if (!_keyMap.TryGetValue(key, out var visuals)) return false;
        Log(visuals[0].Label, down);
        foreach (var v in visuals)
        {
            if (down)
            {
                SetPressed(v);
            }
            else
            {
                if (!v.Tested) { v.Tested = true; _testedCount++; }
                SetTested(v);
            }
        }
        if (!down) UpdateProgress();
        return true;
    }

    // ═══ 按键历史 Log ═══

    const int MaxLogLines = 500;

    /// <summary>追加一条按键日志（时间戳 + 键名 + 动作），自动滚底；超上限删最旧一条。</summary>
    void Log(string label, bool down)
    {
        LogBox.AppendText($"{DateTime.Now:HH:mm:ss.fff}  {label}  {(down ? "按下" : "松开")}\r\n");
        if (LogBox.LineCount > MaxLogLines)
        {
            int cut = LogBox.Text.IndexOf('\n');
            if (cut >= 0) LogBox.Text = LogBox.Text[(cut + 1)..];
        }
        LogBox.ScrollToEnd();
    }

    /// <summary>按住 = 强调色高亮（取当前主题令牌，深浅色自适应）。</summary>
    void SetPressed(KeyVisual v)
    {
        v.Bd.Background = (Brush)FindResource("AccentBrush");
        v.Bd.BorderBrush = (Brush)FindResource("AccentBrush");
        v.Tb.Foreground = Brushes.White;
    }

    /// <summary>已测试 = 绿色实心（深浅色均可读）。</summary>
    void SetTested(KeyVisual v)
    {
        v.Bd.Background = Brushes.ForestGreen;
        v.Bd.BorderBrush = Brushes.ForestGreen;
        v.Tb.Foreground = Brushes.White;
    }

    /// <summary>未测试 = 默认底色（恢复 DynamicResource 绑定，主题切换跟随）。</summary>
    void SetNormal(KeyVisual v)
    {
        v.Bd.SetResourceReference(Border.BackgroundProperty, "SurfaceBgBrush");
        v.Bd.SetResourceReference(Border.BorderBrushProperty, "BorderStrongBrush");
        v.Tb.SetResourceReference(TextBlock.ForegroundProperty, "TextBrush");
    }

    void UpdateProgress()
    {
        KeyProgress.Value = _testedCount;
        ProgressText.Text = $"已测试 {_testedCount} / {_all.Count}";
    }

    // ═══ 按钮与通过态 ═══

    void ResetButton_Click(object sender, RoutedEventArgs e)
    {
        foreach (var v in _all)
        {
            v.Tested = false;
            SetNormal(v);
        }
        _testedCount = 0;
        UpdateProgress();
        LogBox.Clear();
        SetPassed(false);
        PassButton.IsEnabled = true;
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
