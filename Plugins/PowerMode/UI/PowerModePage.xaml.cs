using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using YanJi.Plugin.PowerMode.Core;
using YanJi.PluginSdk;

namespace YanJi.Plugin.PowerMode.UI;

public partial class PowerModePage : UserControl, IModulePage
{
    readonly Func<PowerModeManager> _createManager;
    readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromSeconds(3) };
    PowerModeManager? _manager;
    PowerModeStatus? _status;
    bool _busy, _available, _passed;
    bool? _ac;
    string? _providerName;
    string? _driverUrl;
    public event Action<bool>? PassChanged;

    public PowerModePage(Func<PowerModeManager> createManager)
    {
        InitializeComponent();
        _createManager = createManager;
        Loaded += async (_, _) =>
        {
            await RefreshAsync(reprobe: true);
            if (IsLoaded) _timer.Start();
        };
        Unloaded += (_, _) => _timer.Stop();
        _timer.Tick += async (_, _) => await RefreshAsync();
    }

    async Task RefreshAsync(bool reprobe = false, DevicePowerMode? requested = null)
    {
        if (_busy) return;
        _busy = true;
        UpdateButtons();
        try
        {
            var result = await Task.Run(() =>
            {
                _manager ??= _createManager();
                if (reprobe || !_available) _manager.Refresh();
                bool? applied = requested is DevicePowerMode mode ? _manager.SetMode(mode) : null;
                return (Status: _manager.ReadStatus(), Ac: SystemPowerHelper.IsAcConnected(), Applied: applied);
            });
            _available = true;
            // 外部软件/热键或供电变化也会撤回已确认的准备状态。
            if (_status?.Mode != result.Status.Mode || _ac != result.Ac
                || _providerName != _manager!.CurrentProvider.ProviderName || requested != null)
                SetPassed(false);
            _status = result.Status;
            _ac = result.Ac;
            _providerName = _manager!.CurrentProvider.ProviderName;
            RenderStatus();
            if (result.Applied is bool applied)
            {
                ResultText.Text = _manager!.LastMessage;
                ResultText.SetResourceReference(TextBlock.ForegroundProperty, applied ? "AccentBrush" : "ErrorBrush");
                Log(ResultText.Text);
            }
            else if (reprobe)
            {
                ResultText.Text = "检测完成。请选择模式，确认后继续验机。";
                ResultText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
                Log("已重新检测供电和性能模式。");
            }
        }
        catch (Exception)
        {
            _available = false;
            _status = null;
            SetPassed(false);
            CurrentModeText.Text = "当前模式：未知";
            SourceText.Text = "请点击重新检测，或在系统电源设置中查看。";
            SourceText.Visibility = Visibility.Visible;
            ResultText.Text = "暂时无法检测性能模式，请重试。";
            ResultText.SetResourceReference(TextBlock.ForegroundProperty, "ErrorBrush");
            Log(ResultText.Text);
        }
        finally { _busy = false; UpdateButtons(); }
    }

    void RenderStatus()
    {
        var manager = _manager!;
        IdentityText.Text = $"当前品牌：{manager.Identity.Manufacturer}\n主机型号：{manager.Identity.Model}";
        SupplyText.Text = "供电状态：" + (_ac switch
        { true => "已插电", false => "电池供电（请插电后开启增强模式）", null => "无法确认，请检查电源连接" });
        SupplyText.SetResourceReference(TextBlock.ForegroundProperty, _ac == true ? "TextSecondaryBrush" : "ErrorBrush");
        ProviderText.Text = "模式控制：" + (manager.IsGeneric
            ? "Windows 电源设置" : "原厂性能模式 + Windows 电源设置");
        CurrentModeText.Text = (manager.IsGeneric ? "当前模式：" : "当前显示模式：")
            + (_status?.Mode is DevicePowerMode mode ? ModeName(mode) : "无法确认");
        SourceText.Text = _status?.Mode == null
            ? "请尝试选择一种模式，或在系统电源设置中查看。"
            : "部分电脑无法确认原厂模式，请在电脑自带的控制中心核对。";
        SourceText.Visibility = !manager.IsGeneric || _status?.Mode == null ? Visibility.Visible : Visibility.Collapsed;
        SupportText.Text = manager.IsGeneric
            ? "本机使用 Windows 电源模式。需要调整风扇或整机性能时，请使用电脑自带的控制中心。"
            : "电脑自带的控制中心也可以切换模式，同时使用时可能改变这里的设置。";
        _driverUrl = FindDriverUrl(manager.Identity.Manufacturer);
        DriverButton.Visibility = _driverUrl == null ? Visibility.Collapsed : Visibility.Visible;
        foreach (var button in new[] { QuietButton, BalancedButton, TurboButton })
            button.SetResourceReference(Button.BorderBrushProperty,
                button.Tag?.ToString() == _status?.Mode?.ToString() ? "AccentBrush" : "BorderBrush");
    }

    void UpdateButtons()
    {
        RefreshButton.IsEnabled = !_busy;
        QuietButton.IsEnabled = BalancedButton.IsEnabled = !_busy && _available;
        TurboButton.IsEnabled = !_busy && _available && _ac == true;
        PassButton.IsEnabled = !_busy && _available && !_passed && _status?.Mode != null
            && (_status.Mode != DevicePowerMode.Turbo || _ac == true);
    }

    async void Mode_Click(object sender, RoutedEventArgs e)
    {
        if (sender is Button button && Enum.TryParse<DevicePowerMode>(button.Tag?.ToString(), out var mode))
        {
            SetPassed(false);
            ResultText.Text = "正在切换至" + ModeName(mode) + "…";
            await RefreshAsync(requested: mode);
        }
    }

    async void Refresh_Click(object sender, RoutedEventArgs e)
    {
        SetPassed(false);
        await RefreshAsync(reprobe: true);
    }

    async void Pass_Click(object sender, RoutedEventArgs e)
    {
        await RefreshAsync(); // 确认前再读一次，避免确认已经被外部软件改变的状态。
        if (PassButton.IsEnabled) { SetPassed(true); UpdateButtons(); }
    }

    void SetPassed(bool passed)
    {
        bool changed = _passed != passed;
        _passed = passed;
        StatusText.Text = passed ? "✓ 已确认通过" : "待确认";
        if (passed) StatusText.Foreground = Brushes.ForestGreen;
        else StatusText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
        if (changed) PassChanged?.Invoke(passed);
    }

    static string ModeName(DevicePowerMode mode) => mode switch
    { DevicePowerMode.Quiet => "静音", DevicePowerMode.Turbo => "增强", _ => "平衡" };

    static string? FindDriverUrl(string brand)
    {
        (string Brand, string Url)[] links =
        [
            ("ASUS", "https://www.asus.com/support/"),
            ("LENOVO", "https://pcsupport.lenovo.com/"),
            ("Dell", "https://www.dell.com/support/home/"),
            ("HP", "https://support.hp.com/"),
            ("Hewlett", "https://support.hp.com/"),
        ];
        return links.FirstOrDefault(p => brand.Contains(p.Brand, StringComparison.OrdinalIgnoreCase)).Url;
    }

    void SystemSettings_Click(object sender, RoutedEventArgs e) => Open("ms-settings:powersleep");
    void Driver_Click(object sender, RoutedEventArgs e) { if (_driverUrl != null) Open(_driverUrl); }

    void Open(string target)
    {
        try { Process.Start(new ProcessStartInfo(target) { UseShellExecute = true }); }
        catch (Exception ex)
        {
            ResultText.Text = "无法打开：" + ex.Message;
            ResultText.SetResourceReference(TextBlock.ForegroundProperty, "ErrorBrush");
            Log(ResultText.Text);
        }
    }

    void Log(string message)
    {
        if (LogBox.Text.Length > 16000) LogBox.Text = LogBox.Text[^8000..];
        LogBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}\n");
        LogBox.ScrollToEnd();
    }
}
