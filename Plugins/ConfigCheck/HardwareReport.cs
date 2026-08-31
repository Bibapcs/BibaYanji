namespace YanJi.Plugin.ConfigCheck;

/// <summary>一条「项目 = 值」展示行。</summary>
public record InfoEntry(string Name, string Value);

/// <summary>一个信息分组（如 处理器 / 显卡 / 内存 / 硬盘 / 屏幕）。</summary>
public class InfoGroup(string title)
{
    public string Title { get; } = title;
    public List<InfoEntry> Entries { get; } = [];
    /// <summary>「详细信息」折叠区条目（Expander，默认收起；为空则页面不显示该区域）。</summary>
    public List<InfoEntry> Details { get; } = [];
    public void Add(string name, string value) => Entries.Add(new InfoEntry(name, value));
    public void AddDetail(string name, string value) => Details.Add(new InfoEntry(name, value));
}

/// <summary>整机硬件采集结果（分组列表 + 采集时间）。</summary>
public class HardwareReport
{
    public DateTime CollectedAt { get; } = DateTime.Now;
    public List<InfoGroup> Groups { get; } = [];
}
