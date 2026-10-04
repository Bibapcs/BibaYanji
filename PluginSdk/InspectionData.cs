namespace YanJi.PluginSdk;

/// <summary>宿主内存中的本次验机快照；插件只发布结果，不互相引用程序集。UI 线程读写。</summary>
public sealed class InspectionData
{
    public string ReportId { get; } = Guid.NewGuid().ToString("N")[..12].ToUpperInvariant();
    public DateTime StartedAt { get; } = DateTime.Now;
    public HardwareSnapshot? Hardware { get; set; }
    public StressSnapshot? Stress { get; set; }
}

public record ReportEntry(string Name, string Value);
public record ReportGroup(string Title, IReadOnlyList<ReportEntry> Entries, IReadOnlyList<ReportEntry> Details);
public record HardwareSnapshot(DateTime CollectedAt, IReadOnlyList<ReportGroup> Groups);
public record StressSnapshot(DateTime StartedAt, DateTime EndedAt, string Mode, string? CpuName, string? GpuName,
    double? CpuPeakTemperature, double? CpuAveragePower, int CpuPowerSamples,
    double? GpuPeakTemperature, double? GpuAveragePower, int GpuPowerSamples);
