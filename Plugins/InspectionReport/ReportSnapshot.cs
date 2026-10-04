using System.IO;
using System.Text;
using System.Text.Json;
using Microsoft.Win32;
using YanJi.PluginSdk;

namespace YanJi.Plugin.InspectionReport;

internal record ModuleStatus(string Name, string Status, bool Passed);

/// <summary>页面、摘要和 PDF 使用同一份冻结的内容；不触发硬件采集或修改通过状态。</summary>
internal record ReportSnapshot(string Id, DateTime GeneratedAt, DateTime? StartedAt, string Machine,
    IReadOnlyList<ModuleStatus> Modules, HardwareSnapshot? Hardware, IReadOnlyList<ReportEntry> Performance)
{
    public const string Notice = "本报告记录软件读取的数据与使用者的人工确认，仅供验机留存，不是第三方检测认证。未确认不代表故障；未安装或未测试项目不作通过判断。";
    static readonly (string Id, string Name)[] BuiltInModules =
    [
        ("config", "配置核对"), ("keyboard", "键盘测试"), ("screen", "屏幕坏点"),
        ("av", "影音会议"), ("stress", "散热测试"), ("disk", "硬盘跑分")
    ];

    public static ReportSnapshot Capture(IHostContext host)
    {
        var modules = host.Modules.Where(m => !m.Terminal)
            .Select(m => new ModuleStatus(m.Name, host.IsDone(m.Id) ? "✓ 人工确认通过" : "未确认", host.IsDone(m.Id))).ToList();
        foreach (var (id, name) in BuiltInModules)
            if (!host.Modules.Any(m => m.Id == id)) modules.Add(new(name, "未安装 / 未加载", false));

        var entries = new List<ReportEntry>();
        if (host.Modules.Any(m => m.Id == "stress"))
        {
            if (host.Inspection?.Stress is { } s)
            {
                entries.Add(new("最近一轮烤机", $"{s.Mode} · {(s.EndedAt - s.StartedAt).TotalSeconds:0} 秒 · {s.StartedAt:yyyy-MM-dd HH:mm:ss} 至 {s.EndedAt:HH:mm:ss}"));
                entries.Add(new("监控硬件", $"CPU：{s.CpuName ?? "未知"}；GPU：{s.GpuName ?? "未知"}"));
                entries.Add(new("CPU 峰值温度 / 平均功耗", $"{Metric(s.CpuPeakTemperature, "°C")} / {Metric(s.CpuAveragePower, "W")}（功耗有效样本 {s.CpuPowerSamples}）"));
                entries.Add(new("GPU 峰值温度 / 平均功耗", $"{Metric(s.GpuPeakTemperature, "°C")} / {Metric(s.GpuAveragePower, "W")}（功耗有效样本 {s.GpuPowerSamples}）"));
                entries.Add(new("统计口径", "负载运行期间的有效采样；均值包含升温过程，不代表稳定功耗；未参与负载或不可读的指标显示无法读取。"));
            }
            else entries.Add(new("散热测试摘要", "未测试 / 无本次烤机数据"));
        }
        else entries.Add(new("散热测试摘要", "未安装"));

        if (host.Modules.Any(m => m.Id == "disk"))
            entries.AddRange(ReadDiskResult(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "YanJi", "disk_result", "硬盘基准.json"), host.Inspection?.StartedAt));
        else entries.Add(new("硬盘跑分", "未安装"));

        string machine;
        try
        {
            const string bios = @"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\BIOS";
            machine = string.Join(" ", new[] { Registry.GetValue(bios, "SystemManufacturer", null), Registry.GetValue(bios, "SystemProductName", null) }
                .Select(v => v?.ToString()?.Trim()).Where(v => !string.IsNullOrEmpty(v)));
            if (machine.Length == 0) machine = "型号无法读取";
        }
        catch { machine = "型号无法读取"; }
        return new(host.Inspection?.ReportId ?? "未提供", DateTime.Now, host.Inspection?.StartedAt, machine,
            modules, host.Inspection?.Hardware, entries);
    }

    internal static IReadOnlyList<ReportEntry> ReadDiskResult(string path, DateTime? sessionStart)
    {
        if (!File.Exists(path)) return [new("硬盘跑分", "未测试 / 无已保存成绩")];
        try
        {
            if (new FileInfo(path).Length > 64 * 1024) throw new InvalidDataException("成绩文件过大");
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var root = doc.RootElement;
            double Score(string name)
            {
                double value = root.GetProperty(name).GetDouble();
                if (!double.IsFinite(value) || value < 0) throw new InvalidDataException("成绩数值无效");
                return value;
            }
            double sr = Score("seq_read"), sw = Score("seq_write"), rr = Score("rnd4k_read"), rw = Score("rnd4k_write");
            string source = "已保存成绩（来源、盘符和测试时间未记录，无法确认属于本机本次验机）";
            if (root.TryGetProperty("tested_at", out var time) && time.TryGetDateTimeOffset(out var testedAt))
            {
                string drive = root.TryGetProperty("drive", out var d) ? d.GetString() ?? "未知盘符" : "未知盘符";
                string origin = root.TryGetProperty("machine_name", out var m) ? m.GetString() ?? "未知设备" : "未知设备";
                bool current = sessionStart.HasValue && testedAt.LocalDateTime >= sessionStart.Value &&
                    testedAt <= DateTimeOffset.Now && origin == Environment.MachineName;
                source = $"{(current ? "本次已保存成绩" : "历史 / 外部成绩，供参考")} · {drive} · {testedAt.LocalDateTime:yyyy-MM-dd HH:mm:ss} · {origin}";
            }
            return [new("硬盘成绩来源", source),
                new("SEQ1M Q8T1 读 / 写", $"{sr:0.0} / {sw:0.0} MB/s"),
                new("RND4K Q1T1 读 / 写", $"{rr:0.0} / {rw:0.0} MB/s"),
                new("跑分参数", "CrystalDiskMark 引擎；1 GiB 随机数据，5 轮测量 + 1 轮预热；与模块人工确认状态分别记录。")];
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or UnauthorizedAccessException or JsonException or InvalidOperationException or KeyNotFoundException or FormatException)
        {
            return [new("硬盘跑分", "已保存成绩无法读取：" + ex.Message)];
        }
    }

    static string Metric(double? value, string unit) => value.HasValue ? $"{value:0.0} {unit}" : "无法读取";

    public string Overview => $"机器：{Machine}\n报告编号：{Id}\n生成时间：{GeneratedAt:yyyy-MM-dd HH:mm:ss}\n" +
        $"本次验机开始：{(StartedAt.HasValue ? StartedAt.Value.ToString("yyyy-MM-dd HH:mm:ss") : "未知")}\n" +
        $"CPU：{HardwareValue("处理器", "型号")}\nGPU：{HardwareValue("显卡", "型号")}\n内存：{HardwareValue("内存", "总容量")}";

    string HardwareValue(string group, string field) => Hardware == null ? "未采集" :
        string.Join("；", Hardware.Groups.Where(g => g.Title.Contains(group)).SelectMany(g => g.Entries)
            .Where(e => e.Name.StartsWith(field)).Select(e => e.Value)) is { Length: > 0 } value ? value : "无法读取";

    public string ToSummary()
    {
        var text = new StringBuilder("笔吧验机 · 验机报告\n").AppendLine(Overview).AppendLine();
        foreach (var m in Modules) text.AppendLine($"{m.Name}：{m.Status}");
        text.AppendLine();
        if (Hardware is { } h)
        {
            text.AppendLine($"硬件采集时间：{h.CollectedAt:yyyy-MM-dd HH:mm:ss}");
            foreach (var g in h.Groups)
            {
                text.AppendLine($"【{g.Title}】");
                foreach (var e in g.Entries.Concat(g.Details)) text.AppendLine($"{e.Name}：{e.Value}");
            }
        }
        else text.AppendLine("硬件配置：未采集（请先进入配置核对页面）");
        foreach (var e in Performance) text.AppendLine($"{e.Name}：{e.Value}");
        return text.AppendLine().Append(Notice).ToString();
    }
}
