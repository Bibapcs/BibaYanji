namespace YanJi.Plugin.PowerMode.Core;

public enum DevicePowerMode { Quiet, Balanced, Turbo }

/// <summary>品牌适配契约。探测不得切档；读不到真实状态时应抛异常，不能默认报告平衡。</summary>
public interface IPowerModeProvider
{
    string ProviderName { get; }
    string SupportedBrand { get; }
    bool IsSupported();
    DevicePowerMode GetCurrentMode();
    bool SetMode(DevicePowerMode mode);

    /// <summary>说明状态来源与读回限制，以及最近一次切档的失败/部分成功原因。</summary>
    string CurrentModeSource => "系统读回";
    string LastMessage => "";
}

public record PowerModeStatus(DevicePowerMode? Mode, string Source);
