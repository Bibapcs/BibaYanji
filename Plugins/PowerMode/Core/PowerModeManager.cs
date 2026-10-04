using YanJi.Plugin.PowerMode.Providers;

namespace YanJi.Plugin.PowerMode.Core;

/// <summary>只负责注册、品牌匹配与调度，不知道任何品牌的驱动或档位编码。</summary>
public sealed class PowerModeManager
{
    readonly List<(IPowerModeProvider Provider, int Priority)> _providers = [];
    readonly Func<bool?> _acConnected;
    public SystemIdentity Identity { get; }
    public IPowerModeProvider CurrentProvider { get; private set; }
    public List<string> ProbeMessages { get; } = [];
    public string LastMessage { get; private set; } = "";
    public bool IsGeneric => CurrentProvider.SupportedBrand == "*";

    public PowerModeManager(SystemIdentity? identity = null, IPowerModeProvider? fallback = null,
        Func<bool?>? acConnected = null)
    {
        Identity = identity ?? SystemPowerHelper.GetIdentity();
        _acConnected = acConnected ?? SystemPowerHelper.IsAcConnected;
        CurrentProvider = fallback ?? new WindowsGenericPowerModeProvider();
        Register(CurrentProvider, int.MinValue);
    }

    /// <summary>较高优先级先探测，同优先级按注册顺序；仅在页面串行操作队列中调用。</summary>
    public void Register(IPowerModeProvider provider, int priority = 100)
    {
        ArgumentNullException.ThrowIfNull(provider);
        if (!_providers.Any(p => ReferenceEquals(p.Provider, provider)))
            _providers.Add((provider, priority));
    }

    public void Refresh()
    {
        ProbeMessages.Clear();
        foreach (var (provider, _) in _providers.OrderByDescending(p => p.Priority))
        {
            if (provider.SupportedBrand != "*" && !Identity.Manufacturer.Contains(
                    provider.SupportedBrand, StringComparison.OrdinalIgnoreCase)) continue;
            try
            {
                if (provider.IsSupported()) { CurrentProvider = provider; return; }
                ProbeMessages.Add($"{provider.ProviderName}：接口不可用或尚未适配。");
            }
            catch (Exception ex) { ProbeMessages.Add($"{provider.ProviderName}：{ex.Message}"); }
        }
        throw new InvalidOperationException("未检测到可用的性能调度接口。" + string.Join(" ", ProbeMessages));
    }

    public PowerModeStatus ReadStatus()
    {
        try { return new(CurrentProvider.GetCurrentMode(), CurrentProvider.CurrentModeSource); }
        catch (Exception ex) { return new(null, ex.Message); }
    }

    public bool SetMode(DevicePowerMode mode)
    {
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        if (mode == DevicePowerMode.Turbo && _acConnected() != true)
        {
            LastMessage = "请先插好电源，再开启增强模式。";
            return false;
        }
        try
        {
            bool applied = CurrentProvider.SetMode(mode);
            LastMessage = CurrentProvider.LastMessage;
            if (LastMessage.Length == 0) LastMessage = applied ? "模式切换已完成。" : "电脑未能切换模式，请重试。";
            return applied;
        }
        catch (Exception ex) { LastMessage = "切换失败：" + ex.Message; return false; }
    }
}
