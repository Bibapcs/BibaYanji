using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using YanJi.PluginSdk;
using YanJi.Services;
using YanJi.Views;

namespace YanJi;

/// <summary>主窗口外壳：顶栏（应用名 + 插件管理 + 主题开关）+ 左侧导航（验机项目，带完成状态圆点）+ 内容区。
/// 模块 = plugins/ 目录下的插件（见 AGENTS.md 插件开发文档）：启动时 PluginLoader 扫描加载，
/// 导航项按插件 Order 动态生成；页面实现 IModulePage 并触发 PassChanged(true) → 圆点变绿并
/// AdvanceToNextModule 自动跳到下一个未完成模块（全部完成时导航底部显示「全部验机项目已完成」，
/// 并跳到 Terminal 终点页）。需要物理按键的页面实现 IKeyHandlerPage（PreviewKeyDown/Up 在此统一路由，
/// 吞掉防焦点副作用）；需要关闭清理的插件实现 IPluginShutdown（Closed 时逐个调用）。</summary>
public partial class MainWindow : Window
{
    // 已加载插件（按 Order 排序）与 id → 页面映射（页面实例常驻，切导航不丢状态）
    readonly List<LoadedPlugin> _plugins;
    readonly Dictionary<string, UserControl> _pages = [];
    // 导航圆点：插件 id → (圆点, 对勾)；终点页（Terminal）不登记
    readonly Dictionary<string, (Ellipse Dot, TextBlock Check)> _navDots = [];
    // 可通过模块（非终点页）的 id 集合
    readonly HashSet<string> _passableTags = [];
    // 已完成模块的 id 集合（自动跳转与「全部完成」判定用）
    readonly HashSet<string> _doneTags = [];
    readonly HostContext _hostContext;
    bool _syncingSwitch;

    /// <summary>IHostContext 实现：模块清单在插件加载前填入，完成状态查询复用 _doneTags（不另立状态源）。</summary>
    sealed class HostContext : IHostContext
    {
        readonly Func<string, bool> _isDone;
        readonly List<PluginInfo> _modules = [];
        public HostContext(Func<string, bool> isDone) => _isDone = isDone;
        public IReadOnlyList<PluginInfo> Modules => _modules;
        public bool IsDone(string moduleId) => _isDone(moduleId);
        public event Action? DoneChanged;
        public void RaiseDoneChanged() => DoneChanged?.Invoke();
        /// <summary>启动时批量填入 / 运行时追加（导入即载），均保持按 Order 排序。</summary>
        public void AddModule(PluginInfo info)
        {
            _modules.Add(info);
            _modules.Sort((a, b) => a.Order.CompareTo(b.Order));
        }
    }

    public MainWindow()
    {
        InitializeComponent();
        ThemeManager.ApplyTitleBar(this);
        ThemeManager.ThemeChanged += SyncThemeSwitch;
        SyncThemeSwitch();

        // 先扫清单建立模块列表（终点页插件的页面构造时就要读 IHostContext.Modules），再加载创建页面
        _hostContext = new HostContext(tag => _doneTags.Contains(tag));
        foreach (var x in PluginLoader.ScanInstalled())
            _hostContext.AddModule(new PluginInfo(x.Manifest.Id, x.Manifest.Name, x.Manifest.Version,
                x.Manifest.Order, x.Manifest.Terminal));
        _plugins = PluginLoader.LoadAll(_hostContext);
        foreach (var lp in _plugins)
        {
            _pages[lp.Info.Id] = lp.Page;
            AddNavItem(lp);
            if (!lp.Info.Terminal) _passableTags.Add(lp.Info.Id);
            if (lp.Page is IModulePage mp)
                mp.PassChanged += done => OnModulePassChanged(lp.Info.Id, done);
        }

        // 键盘测试等页面的物理按键路由：Preview 隧道事件 Window 最先拿到；
        // handledEventsToo=true 保证子控件（按钮等）已标记 Handled 的按键（如 Space/Enter）也能收到
        AddHandler(PreviewKeyDownEvent, new KeyEventHandler(OnGlobalPreviewKeyDown), handledEventsToo: true);
        AddHandler(PreviewKeyUpEvent, new KeyEventHandler(OnGlobalPreviewKeyUp), handledEventsToo: true);

        Loaded += (_, _) => UpdateIcon();
        // 关窗兜底：通知各插件做清理（如散热测试停烤机进程；页面 Unloaded / App.Exit / Job 对象之外再加一层）
        Closed += (_, _) =>
        {
            foreach (var lp in _plugins)
                if (lp.Plugin is IPluginShutdown s)
                {
                    try { s.Shutdown(); }
                    catch { /* 单个插件清理失败不影响其它插件与退出 */ }
                }
        };
        // 初始选中第一个模块；没有插件时内容区显示占位提示
        if (NavList.Items.Count > 0)
            NavList.SelectedIndex = 0;
        else
            ContentHost.Content = MakeEmptyPlaceholder();
    }

    /// <summary>按插件生成导航项并按 Order 有序插入（ListBoxItem.Tag = 插件 id；
    /// 终点页用 Collapsed 占位圆点保持文字对齐）。启动批量与「导入即载」共用。</summary>
    void AddNavItem(LoadedPlugin lp)
    {
        var dot = new Ellipse { Width = 10, Height = 10, StrokeThickness = 1.5, Fill = Brushes.Transparent };
        dot.SetResourceReference(Shape.StrokeProperty, "TextSecondaryBrush");
        var check = new TextBlock
        {
            Text = "✓", Foreground = Brushes.White, FontSize = 8, FontWeight = FontWeights.Bold,
            Margin = new Thickness(0, -1, 0, 0),
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            Visibility = Visibility.Collapsed,
        };
        if (lp.Info.Terminal)
            dot.Visibility = Visibility.Collapsed; // 终点页不配状态圆点（占位对齐）
        var dotGrid = new Grid { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 8, 0) };
        dotGrid.Children.Add(dot);
        dotGrid.Children.Add(check);
        var panel = new StackPanel { Orientation = Orientation.Horizontal };
        panel.Children.Add(dotGrid);
        panel.Children.Add(new TextBlock { Text = lp.Info.Name });
        var newItem = new ListBoxItem { Tag = lp.Info.Id, Content = panel };
        int insertAt = NavList.Items.Count;
        for (int i = 0; i < NavList.Items.Count; i++)
            if (NavList.Items[i] is ListBoxItem ex && ex.Tag is string exId
                && _plugins.FirstOrDefault(p => p.Info.Id == exId) is { } exLp
                && exLp.Info.Order > lp.Info.Order)
            { insertAt = i; break; }
        NavList.Items.Insert(insertAt, newItem);
        if (!lp.Info.Terminal)
            _navDots[lp.Info.Id] = (dot, check);
    }

    /// <summary>导入后立即加载新插件（免重启）：登记页面/导航/通过订阅/HostContext 模块清单，
    /// 并选中它。已加载或加载失败返回 false。注意：默认加载上下文运行时不卸载程序集，
    /// 之后要删除/覆盖该插件仍走「待处理操作 + 自动重启」。</summary>
    public bool TryLoadPluginAtRuntime(string id)
    {
        if (_pages.ContainsKey(id)) return false;
        var hit = PluginLoader.ScanInstalled().FirstOrDefault(x => x.Manifest.Id == id);
        if (hit.Manifest == null) return false;
        var lp = PluginLoader.LoadOne(hit.Manifest, hit.Dir, _hostContext);
        if (lp == null) return false;
        _plugins.Add(lp);
        _plugins.Sort((a, b) => a.Info.Order.CompareTo(b.Info.Order));
        _pages[lp.Info.Id] = lp.Page;
        _hostContext.AddModule(lp.Info);
        AddNavItem(lp);
        if (!lp.Info.Terminal) _passableTags.Add(lp.Info.Id);
        if (lp.Page is IModulePage mp)
            mp.PassChanged += done => OnModulePassChanged(lp.Info.Id, done);
        // 首次插入时 NavList 从无选中项：选中它
        if (NavList.SelectedIndex < 0)
            NavList.SelectedItem = NavList.Items.Cast<object>().FirstOrDefault(
                i => i is ListBoxItem li && li.Tag as string == id);
        return true;
    }

    /// <summary>重启程序（删除/覆盖已加载插件后调用）：插件 dll 被默认加载上下文锁定，
    /// 只能等新进程在加载前执行待处理操作。新进程继承当前提升权限，不再弹 UAC。</summary>
    public static void RestartApp()
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = Environment.ProcessPath!,
                UseShellExecute = true,
            });
        }
        catch (Exception ex)
        {
            PluginLoader.Log("自动重启失败：" + ex.Message);
            return;
        }
        Application.Current.Shutdown();
    }

    static UIElement MakeEmptyPlaceholder() => new TextBlock
    {
        Text = "未安装任何验机模块。\n点右上角「插件管理」导入 zip 插件包，或重新运行安装向导勾选需要的模块。",
        TextWrapping = TextWrapping.Wrap,
        HorizontalAlignment = HorizontalAlignment.Center,
        VerticalAlignment = VerticalAlignment.Center,
        TextAlignment = TextAlignment.Center,
    };

    void OnNavSelect(object sender, SelectionChangedEventArgs e)
    {
        // 按 ListBoxItem.Tag（插件 id）取页面实例（常驻，切换不丢状态）
        if (NavList.SelectedItem is ListBoxItem item && item.Tag is string tag && _pages.TryGetValue(tag, out var page))
            ContentHost.Content = page;
    }

    /// <summary>模块页面通过态变化：圆点同步 + 通过时自动跳转下一未完成模块；回退时撤掉「全部完成」提示。</summary>
    void OnModulePassChanged(string tag, bool done)
    {
        SetNavDone(tag, done);
        if (done)
        {
            _doneTags.Add(tag);
            AdvanceToNextModule();
        }
        else
        {
            _doneTags.Remove(tag);
            AllDoneHint.Visibility = Visibility.Collapsed;
        }
        // 推送给关注完成状态的插件（验机结束页的清单实时跟随）
        _hostContext.RaiseDoneChanged();
    }

    /// <summary>自动跳转：选中当前项之后第一个未完成的模块；没有下一个时跳到终点页，
    /// 全部可通过模块完成时显示总结提示。</summary>
    void AdvanceToNextModule()
    {
        for (int i = NavList.SelectedIndex + 1; i < NavList.Items.Count; i++)
        {
            if (NavList.Items[i] is ListBoxItem item && item.Tag is string tag
                && _passableTags.Contains(tag) && !_doneTags.Contains(tag))
            {
                NavList.SelectedIndex = i;
                return;
            }
        }
        // 后面没有未完成模块了：跳到终点页（若已安装）
        for (int i = NavList.SelectedIndex + 1; i < NavList.Items.Count; i++)
        {
            if (NavList.Items[i] is ListBoxItem item && item.Tag is string tag && !_passableTags.Contains(tag))
            {
                NavList.SelectedIndex = i;
                break;
            }
        }
        if (_passableTags.Count > 0 && _passableTags.All(_doneTags.Contains))
            AllDoneHint.Visibility = Visibility.Visible;
    }

    /// <summary>导航项完成态：true = 绿色实心圆点 + ✓；false = 灰色空心（重新检测/重置时还原）。
    /// 还原用 SetResourceReference 恢复 DynamicResource——直接赋画刷会覆盖动态绑定，主题切换后不变色。</summary>
    void SetNavDone(string tag, bool done)
    {
        if (!_navDots.TryGetValue(tag, out var v)) return;
        v.Dot.Fill = done ? Brushes.ForestGreen : Brushes.Transparent;
        if (done) v.Dot.Stroke = Brushes.ForestGreen;
        else v.Dot.SetResourceReference(Shape.StrokeProperty, "TextSecondaryBrush");
        v.Check.Visibility = done ? Visibility.Visible : Visibility.Collapsed;
    }

    // ═══ 物理按键路由（键盘测试等实现 IKeyHandlerPage 的页面）═══

    void OnGlobalPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (ContentHost.Content is not IKeyHandlerPage kp || e.IsRepeat) return;
        Key key = e.Key == Key.System ? e.SystemKey : e.Key; // Alt 族走 SystemKey
        if (key is Key.ImeProcessed or Key.None) return;
        // 吞掉：防 Space/Enter 触发焦点按钮、Tab 移焦点、方向键滚动页面
        if (kp.HandleKey(key, true)) e.Handled = true;
    }

    void OnGlobalPreviewKeyUp(object sender, KeyEventArgs e)
    {
        if (ContentHost.Content is not IKeyHandlerPage kp) return;
        Key key = e.Key == Key.System ? e.SystemKey : e.Key;
        if (key is Key.ImeProcessed or Key.None) return;
        if (kp.HandleKey(key, false)) e.Handled = true;
    }

    void OnPluginManagerClick(object sender, RoutedEventArgs e)
    {
        new PluginManagerWindow(_plugins, TryLoadPluginAtRuntime) { Owner = this }.ShowDialog();
    }

    /// <summary>窗口图标取 exe 内嵌图标（Win32 提取，多尺寸 ico 在各 DPI 下都清晰）。</summary>
    void UpdateIcon()
    {
        try
        {
            using var sysIcon = System.Drawing.Icon.ExtractAssociatedIcon(
                Process.GetCurrentProcess().MainModule!.FileName);
            if (sysIcon is not null)
            {
                Icon = System.Windows.Interop.Imaging.CreateBitmapSourceFromHIcon(
                    sysIcon.Handle,
                    System.Windows.Int32Rect.Empty,
                    System.Windows.Media.Imaging.BitmapSizeOptions.FromEmptyOptions());
            }
        }
        catch { /* 图标加载失败不影响功能 */ }
    }

    void OnThemeSwitch(object sender, RoutedEventArgs e)
    {
        if (!_syncingSwitch)
            ThemeManager.Set(ThemeSwitch.IsChecked == true);
    }

    void SyncThemeSwitch()
    {
        _syncingSwitch = true;
        ThemeSwitch.IsChecked = ThemeManager.IsDark;
        _syncingSwitch = false;
    }
}
