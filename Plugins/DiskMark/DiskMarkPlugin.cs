using System.Windows.Controls;
using YanJi.PluginSdk;

namespace YanJi.Plugin.DiskMark;

/// <summary>「硬盘跑分」插件入口：CrystalDiskMark 源码集成（CdmHost 子进程）对目标盘跑
/// SEQ1M Q8T1 顺序读/写 + RND4K Q1T1 随机读/写四项。
/// 实现 IPluginShutdown：程序关闭时兜底杀掉跑分进程树（连带 diskspd）。</summary>
public class DiskMarkPlugin : IYanJiPlugin, IPluginShutdown
{
    public PluginInfo Info => new("disk", "硬盘跑分", "1.0.0", 80, false);

    public IReadOnlyList<PluginCredit> Credits =>
    [
        new("CrystalDiskMark 9.0.3（hiyohiyo / Crystal Dew World）", "硬盘基准引擎（源码原封编译为 CdmHost 控制台宿主，随插件分发）", "MIT"),
        new("diskspd 2.0.20a（Microsoft，hiyohiyo 定制 fork）", "测速引擎子进程（由 CDM 引擎拉起，随插件分发）", "MIT"),
    ];

    public UserControl CreatePage(IHostContext host) => new DiskMarkPage();
    public void Shutdown() => DiskMarkPage.CleanupStatic();
}
