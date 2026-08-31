using System.Windows.Controls;
using YanJi.PluginSdk;

namespace YanJi.Plugin.ScreenDeadPixel;

/// <summary>「屏幕坏点」插件入口：全屏纯色循环检测（白→红→绿→蓝→黑）。</summary>
public class ScreenDeadPixelPlugin : IYanJiPlugin
{
    public PluginInfo Info => new("screen", "屏幕坏点", "1.0.0", 30, false);
    public UserControl CreatePage(IHostContext host) => new DeadPixelPage();
}
