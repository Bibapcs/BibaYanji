using YanJi.Plugin.PowerMode.Core;

namespace YanJi.Plugin.PowerMode.Providers;

/// <summary>联想扩展插槽：可调研 root\wmi 的 Lenovo_VpcWmi；机型/方法/返回码需真机验证。
/// 尚未适配时必须返回 false，不得仅因 WMI 类存在就宣称支持。</summary>
public sealed class LenovoPowerModeProvider : IPowerModeProvider
{
    public string ProviderName => "联想社区适配（待实现）";
    public string SupportedBrand => "LENOVO";
    public bool IsSupported() => false;
    public DevicePowerMode GetCurrentMode() => throw new NotSupportedException("联想原厂接口尚未适配。");
    public bool SetMode(DevicePowerMode mode) => false;
}

/// <summary>戴尔扩展插槽；不同产品线的接口需分别确认，不调用未经验证的 WMI 方法。</summary>
public sealed class DellPowerModeProvider : IPowerModeProvider
{
    public string ProviderName => "戴尔社区适配（待实现）";
    public string SupportedBrand => "Dell";
    public bool IsSupported() => false;
    public DevicePowerMode GetCurrentMode() => throw new NotSupportedException("戴尔原厂接口尚未适配。");
    public bool SetMode(DevicePowerMode mode) => false;
}

/// <summary>惠普扩展插槽；OMEN 等机型的原厂接口需社区验证后接入。</summary>
public sealed class HpPowerModeProvider : IPowerModeProvider
{
    public string ProviderName => "惠普社区适配（待实现）";
    public string SupportedBrand => "HP";
    public bool IsSupported() => false;
    public DevicePowerMode GetCurrentMode() => throw new NotSupportedException("惠普原厂接口尚未适配。");
    public bool SetMode(DevicePowerMode mode) => false;
}
