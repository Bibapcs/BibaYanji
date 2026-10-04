using YanJi.PluginSdk;

namespace YanJi.Plugin.StressTest;

/// <summary>仅统计本轮负载运行期间的有效样本，功耗为样本均值，不宣称稳态功耗。</summary>
internal sealed class StressReportSummary(DateTime startedAt, StressTestService.StressMode mode)
{
    double? _cpuPeak, _gpuPeak;
    double _cpuPower, _gpuPower;
    int _cpuCount, _gpuCount;

    public void Add(DateTime sampledAt, SensorMonitorService.Reading reading)
    {
        if (sampledAt < startedAt) return;
        if (mode is StressTestService.StressMode.Cpu or StressTestService.StressMode.Both)
        {
            Peak(ref _cpuPeak, reading.CpuTemp);
            Sum(ref _cpuPower, ref _cpuCount, reading.CpuPower);
        }
        if (mode is StressTestService.StressMode.Gpu or StressTestService.StressMode.Both)
        {
            Peak(ref _gpuPeak, reading.GpuTemp);
            Sum(ref _gpuPower, ref _gpuCount, reading.GpuPower);
        }
    }

    public StressSnapshot Finish(DateTime endedAt, SensorMonitorService.SensorMap? map) => new(
        startedAt, endedAt, mode switch
        {
            StressTestService.StressMode.Cpu => "CPU 单烤",
            StressTestService.StressMode.Gpu => "GPU 单烤",
            _ => "双烤"
        }, map?.CpuName, map?.GpuName, _cpuPeak, _cpuCount > 0 ? _cpuPower / _cpuCount : null, _cpuCount,
        _gpuPeak, _gpuCount > 0 ? _gpuPower / _gpuCount : null, _gpuCount);

    static void Peak(ref double? peak, double? value)
    {
        if (value is double v && double.IsFinite(v) && v > 0) peak = Math.Max(peak ?? v, v);
    }

    static void Sum(ref double sum, ref int count, double? value)
    {
        if (value is double v && double.IsFinite(v) && v > 0) { sum += v; count++; }
    }
}
