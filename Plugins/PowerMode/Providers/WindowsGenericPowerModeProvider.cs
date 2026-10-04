using YanJi.Plugin.PowerMode.Core;

namespace YanJi.Plugin.PowerMode.Providers;

/// <summary>只使用 Windows 预设电源策略；不改自定义参数、不创建重复电源计划。</summary>
public sealed class WindowsGenericPowerModeProvider : IPowerModeProvider
{
    internal static readonly Guid BalancedScheme = new("381b4222-f694-41f0-9685-ff5bb260df2e");
    internal static readonly Guid QuietScheme = new("a1841308-3541-4fab-bc81-f71556f20b4a");
    internal static readonly Guid PerformanceScheme = new("8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c");
    internal static readonly Guid UltimateScheme = new("e9a42b02-d5df-448d-aa00-03f14749eb61");
    internal static readonly Guid QuietOverlay = new("961cc777-2547-4f9d-8174-7d86181b8a7a");
    internal static readonly Guid TurboOverlay = new("ded574b5-45a0-4f42-8737-46345c09c238");
    readonly IWindowsPowerApi _api;
    public string ProviderName => "Windows 电源设置";
    public string SupportedBrand => "*";
    public string CurrentModeSource { get; private set; } = "Windows 系统读回";
    public string LastMessage { get; private set; } = "";

    public WindowsGenericPowerModeProvider() : this(new WindowsPowerApi()) { }
    internal WindowsGenericPowerModeProvider(IWindowsPowerApi api) => _api = api;
    public bool IsSupported() { _api.GetActiveScheme(); return true; }

    public DevicePowerMode GetCurrentMode()
    {
        var scheme = _api.GetActiveScheme();
        CurrentModeSource = "Windows 电源计划读回";
        if (scheme == QuietScheme) return DevicePowerMode.Quiet;
        if (scheme == PerformanceScheme || scheme == UltimateScheme) return DevicePowerMode.Turbo;
        var overlay = _api.GetOverlay();
        CurrentModeSource = "Windows 电源模式读回";
        if (overlay == QuietOverlay) return DevicePowerMode.Quiet;
        if (overlay == TurboOverlay) return DevicePowerMode.Turbo;
        if (scheme == BalancedScheme && (overlay == null || overlay == Guid.Empty)) return DevicePowerMode.Balanced;
        throw new InvalidOperationException("当前为自定义/未识别的 Windows 策略，无法可靠归类为三档。");
    }

    public bool SetMode(DevicePowerMode mode)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        var original = _api.GetActiveScheme();
        var originalOverlay = _api.GetOverlay();
        try
        {
            var schemes = _api.GetSchemes();
            // 现代待机机型通常只有平衡计划；使用其最佳能效/平衡/最佳性能 overlay。
            if (schemes.Any(s => s.Id == BalancedScheme))
            {
                _api.SetScheme(BalancedScheme);
                Guid overlay = mode switch
                {
                    DevicePowerMode.Quiet => QuietOverlay,
                    DevicePowerMode.Turbo => TurboOverlay,
                    _ => Guid.Empty,
                };
                if (_api.TrySetOverlay(overlay) && GetCurrentMode() == mode)
                {
                    LastMessage = "已切换到 Windows " + ModeName(mode) + "模式。";
                    return true;
                }
            }
            Guid target = mode switch
            {
                DevicePowerMode.Quiet => QuietScheme,
                DevicePowerMode.Turbo when schemes.Any(s => s.Id == UltimateScheme) => UltimateScheme,
                DevicePowerMode.Turbo => PerformanceScheme,
                _ => BalancedScheme,
            };
            if (!schemes.Any(s => s.Id == target))
                throw new InvalidOperationException("这台电脑暂时无法使用所选模式，请在系统电源设置中检查。");
            _api.SetScheme(target);
            if (mode == DevicePowerMode.Balanced && GetCurrentMode() != mode)
                throw new InvalidOperationException("未能恢复平衡模式，请在系统电源设置中检查。");
            LastMessage = "已切换到 Windows " + ModeName(mode) + "模式。";
            return true;
        }
        catch (Exception ex)
        {
            LastMessage = "Windows 电源模式切换失败：" + ex.Message;
            try
            {
                _api.SetScheme(original);
                if (originalOverlay is Guid overlay && !_api.TrySetOverlay(overlay))
                    LastMessage += " 未能恢复之前的模式，请检查系统电源设置。";
            }
            catch (Exception) { LastMessage += " 未能恢复之前的设置，请检查系统电源设置。"; }
            return false;
        }
    }

    static string ModeName(DevicePowerMode mode) => mode switch
    {
        DevicePowerMode.Quiet => "最佳能效",
        DevicePowerMode.Turbo => "最佳性能",
        _ => "平衡",
    };
}
