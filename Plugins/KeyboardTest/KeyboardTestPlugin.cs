using System.Windows.Controls;
using YanJi.PluginSdk;

namespace YanJi.Plugin.KeyboardTest;

/// <summary>「键盘测试」插件入口：全尺寸 104 键可视化键盘，按键经宿主 PreviewKey 路由（IKeyHandlerPage）。</summary>
public class KeyboardTestPlugin : IYanJiPlugin
{
    public PluginInfo Info => new("keyboard", "键盘测试", "1.0.0", 20, false);
    public UserControl CreatePage(IHostContext host) => new KeyboardTestPage();
}
