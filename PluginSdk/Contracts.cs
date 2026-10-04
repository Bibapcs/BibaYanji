using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace YanJi.PluginSdk;

/// <summary>插件元数据（与插件目录里 plugin.json 一致）。Order 决定导航顺序（小的在前）；
/// Terminal = 终点页（无「确认通过」、不参与通过状态流、导航不配状态圆点）。</summary>
public record PluginInfo(string Id, string Name, string Version, int Order, bool Terminal);

/// <summary>宿主提供给插件的上下文（目前主要给「验机结束」类插件读全模块清单与完成状态）。</summary>
public interface IHostContext
{
    /// <summary>当前已加载的全部模块（含终点页），按导航顺序。</summary>
    IReadOnlyList<PluginInfo> Modules { get; }
    /// <summary>某模块当前是否已通过（终点页恒为 false）。</summary>
    bool IsDone(string moduleId);
    /// <summary>任一模块通过状态变化后触发（UI 线程）。</summary>
    event Action? DoneChanged;
    /// <summary>本次验机共享快照；默认 null 兼容未提供报告能力的旧宿主。</summary>
    InspectionData? Inspection => null;
}

/// <summary>插件用到的开源组件/素材致谢（名称/用途/协议），在「插件管理」窗口随插件展示——
/// 致谢跟着模块走，删掉模块其致谢也随之消失。</summary>
public record PluginCredit(string Name, string Usage, string License);

/// <summary>验机模块插件入口。插件程序集里必须有且仅有一个公共无参实现类（plugin.json 的 entryType 指定）。</summary>
public interface IYanJiPlugin
{
    /// <summary>插件信息；需与 plugin.json 一致（宿主以 plugin.json 为准校验）。</summary>
    PluginInfo Info { get; }
    /// <summary>本模块用到的开源组件/素材致谢（没有用到的第三方组件就保持空）。
    /// 默认空集合，第三方插件不实现也能编译。</summary>
    IReadOnlyList<PluginCredit> Credits => [];
    /// <summary>创建模块页面（宿主调用一次，实例常驻，切导航不丢状态）。</summary>
    UserControl CreatePage(IHostContext host);
}

/// <summary>「可通过」模块的页面实现此接口：确认通过 → PassChanged(true)，重新检测/重置 → PassChanged(false)。
/// 宿主据此同步导航圆点并自动跳转到下一个未完成模块。终点页不实现本接口。</summary>
public interface IModulePage
{
    event Action<bool>? PassChanged;
}

/// <summary>需要接收物理按键的页面实现此接口（如键盘测试）。宿主用 PreviewKeyDown/Up 隧道事件统一路由，
/// 仅当本页面为当前页时转发；返回 true 表示吞掉该按键（防焦点副作用）。</summary>
public interface IKeyHandlerPage
{
    bool HandleKey(Key key, bool down);
}

/// <summary>需要在程序关闭时做清理的插件实现此接口（如散热测试停烤机进程）。
/// 宿主在窗口 Closed 时逐个调用。</summary>
public interface IPluginShutdown
{
    void Shutdown();
}
