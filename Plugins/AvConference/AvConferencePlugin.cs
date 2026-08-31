using System.Windows.Controls;
using YanJi.PluginSdk;

namespace YanJi.Plugin.AvConference;

/// <summary>「影音会议」插件入口：摄像头预览 / 麦克风电平+录放 / 扬声器试音。</summary>
public class AvConferencePlugin : IYanJiPlugin
{
    public PluginInfo Info => new("av", "影音会议", "1.0.0", 40, false);

    public IReadOnlyList<PluginCredit> Credits =>
    [
        new("NAudio 2.2.1", "麦克风/扬声器（WASAPI 设备枚举、电平、录制、播放）", "MIT"),
        new("试音曲「不死のバイオレット」", "扬声器试音曲", "Copyright© 幻月遠征隊，使用已经著作权人同意"),
    ];

    public UserControl CreatePage(IHostContext host) => new AvConferencePage();
}
