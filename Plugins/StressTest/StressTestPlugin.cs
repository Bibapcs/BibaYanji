using System.Windows.Controls;
using YanJi.PluginSdk;

namespace YanJi.Plugin.StressTest;

/// <summary>「散热测试」插件入口：CPU 单烤 prime95 / GPU 单烤 FurMark / 双烤 + 四路传感器监控。
/// 实现 IPluginShutdown：程序关闭时兜底停掉烤机进程。</summary>
public class StressTestPlugin : IYanJiPlugin, IPluginShutdown
{
    public PluginInfo Info => new("stress", "散热测试", "1.0.0", 50, false);

    public IReadOnlyList<PluginCredit> Credits =>
    [
        new("Prime95（GIMPS, George Woltman）", "CPU 烤机（按公开源码编译，见 prime95-build/）", "GIMPS freeware（随附 license.txt）"),
        new("FurMark（Python 版, StanislavPetrovV）", "GPU 烤机渲染原型（C# / OpenTK 重实现，着色器与贴图随附其许可）", "MIT"),
        new("OpenTK 4.x", "GPU 烤机的 OpenGL 窗口（GLFW）与绑定", "MIT"),
        new("StbImageSharp", "GPU 烤机贴图解码（stb_image 的 C# 移植）", "公有领域"),
        new("LibreHardwareMonitor", "传感器监控（温度/功耗采集）", "MPL-2.0"),
        new("PawnIO（namazso）", "CPU/主板传感器 Ring0 内核驱动（官方签名安装器随附 Tools/pawnio/，用户点击后才安装）", "GPL-2.0（带 IOCTL 通信例外）"),
    ];

    public UserControl CreatePage(IHostContext host) => new StressTestPage();
    public void Shutdown() => StressTestService.Stop();
}
