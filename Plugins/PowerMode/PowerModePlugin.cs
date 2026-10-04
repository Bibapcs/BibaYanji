using System.Windows.Controls;
using YanJi.Plugin.PowerMode.Core;
using YanJi.Plugin.PowerMode.Providers;
using YanJi.Plugin.PowerMode.UI;
using YanJi.PluginSdk;

namespace YanJi.Plugin.PowerMode;

public sealed class PowerModePlugin : IYanJiPlugin
{
    public PluginInfo Info => new("power", "性能模式", "1.0.0", 45, false);
    public IReadOnlyList<PluginCredit> Credits =>
    [
        new("Linux asus-wmi / G-Helper", "原厂接口编号与档位映射的公开协议参考（未分发原程序）", "GPL-2.0 / GPL-3.0"),
    ];
    public UserControl CreatePage(IHostContext host) => new PowerModePage(CreateManager);

    // 品牌适配只在此注册；页面与调度核心无需为新增品牌修改。
    static PowerModeManager CreateManager()
    {
        var manager = new PowerModeManager();
        manager.Register(new AsusPowerModeProvider());
        manager.Register(new LenovoPowerModeProvider());
        manager.Register(new DellPowerModeProvider());
        manager.Register(new HpPowerModeProvider());
        return manager;
    }
}
