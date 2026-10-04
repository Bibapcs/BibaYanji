using YanJi.Plugin.PowerMode.Core;

namespace YanJi.Plugin.PowerMode.Providers;

/// <summary>基座之上的一个参考适配器；品牌协议与方案名称不泄露到 Core/UI。</summary>
public sealed class AsusPowerModeProvider : IPowerModeProvider
{
    internal const uint PerformanceDevice = 0x00120075, VivoDevice = 0x00110019;
    readonly IAsusAcpi _acpi;
    readonly IWindowsPowerApi _windows;
    readonly WindowsGenericPowerModeProvider _generic;
    uint _device;
    public string ProviderName => "原厂性能模式 + Windows 电源设置";
    public string SupportedBrand => "ASUS";
    public string CurrentModeSource { get; private set; } = "原厂电源计划推断（不是固件档位读回）";
    public string LastMessage { get; private set; } = "";

    public AsusPowerModeProvider() : this(new AsusAcpi(), new WindowsPowerApi()) { }
    internal AsusPowerModeProvider(IAsusAcpi acpi, IWindowsPowerApi windows)
    {
        _acpi = acpi;
        _windows = windows;
        _generic = new(windows);
    }

    public bool IsSupported()
    {
        _device = _acpi.Supports(PerformanceDevice) ? PerformanceDevice
            : _acpi.Supports(VivoDevice) ? VivoDevice : 0;
        return _device != 0;
    }

    public DevicePowerMode GetCurrentMode()
    {
        Guid active = _windows.GetActiveScheme();
        foreach (var mode in Enum.GetValues<DevicePowerMode>())
        {
            var plan = FindPlan(mode);
            if (plan != null && plan.Id == active)
            {
                // 部分机型 DSTS 恒定返回能力位，不能将其误当成当前档位。
                CurrentModeSource = "原厂电源计划推断（不是固件档位读回；请结合原厂控制中心核对）";
                return mode;
            }
        }
        var genericMode = _generic.GetCurrentMode();
        CurrentModeSource = _generic.CurrentModeSource + "；仅代表 Windows 策略，原厂固件档位无法确认";
        return genericMode;
    }

    public bool SetMode(DevicePowerMode mode)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        if (_device == 0 && !IsSupported())
        { LastMessage = "暂时无法切换原厂模式，请重新检测或使用电脑自带的控制中心。"; return false; }
        var plan = FindPlan(mode);
        int value = mode switch { DevicePowerMode.Quiet => 2, DevicePowerMode.Turbo => 1, _ => 0 };
        if (_device == VivoDevice && value != 0) value = 3 - value;
        if (!_acpi.Set(_device, value))
        { LastMessage = "电脑未能切换原厂模式，Windows 电源设置未改变。请在原厂控制中心重试。"; return false; }
        // 硬件和系统策略分别确认。部分成功不伪装为全成功，也不盲目回滚未知固件档位。
        try
        {
            if (plan != null) _windows.SetScheme(plan.Id);
            else if (!_generic.SetMode(mode))
            {
                LastMessage = "原厂模式已收到切换请求，但 Windows 电源模式切换失败。请检查系统电源设置。";
                return false;
            }
            LastMessage = "已发送模式切换请求，并更新 Windows 电源设置。请在电脑自带的控制中心确认实际模式。";
            return true;
        }
        catch (Exception)
        {
            LastMessage = "原厂模式已收到切换请求，但 Windows 电源模式切换失败。请检查系统电源设置。";
            return false;
        }
    }

    PowerScheme? FindPlan(DevicePowerMode mode)
    {
        string name = mode switch { DevicePowerMode.Quiet => "Silent", DevicePowerMode.Turbo => "Turbo", _ => "Performance" };
        return _windows.GetSchemes().FirstOrDefault(p => p.Name.Equals(name, StringComparison.OrdinalIgnoreCase));
    }
}
