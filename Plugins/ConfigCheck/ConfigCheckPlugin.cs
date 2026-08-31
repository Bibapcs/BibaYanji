using System.Windows.Controls;
using YanJi.PluginSdk;

namespace YanJi.Plugin.ConfigCheck;

/// <summary>「配置核对」插件入口：进页自动采集 CPU/显卡/内存/硬盘/网卡/屏幕，人工对照订单核对。</summary>
public class ConfigCheckPlugin : IYanJiPlugin
{
    public PluginInfo Info => new("config", "配置核对", "1.0.0", 10, false);

    public IReadOnlyList<PluginCredit> Credits =>
    [
        new("LibreHardwareMonitor", "硬件信息采集库（源码项目引用）", "MPL-2.0"),
        new("CrystalDiskInfo", "硬盘信息采集的实现参考（SMART 直读思路）", "MIT"),
    ];

    public UserControl CreatePage(IHostContext host) => new ConfigCheckPage();
}
