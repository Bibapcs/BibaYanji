using Microsoft.Win32;
using System.Management;
using System.Text;
using YanJi.PluginSdk.Services;

namespace YanJi.Plugin.ConfigCheck;

/// <summary>硬件配置采集：优先 LibreHardwareMonitor（开源，源码项目引用），
/// 它拿不到的字段退回 WMI（System.Management，Windows 自带）/ 注册表 / EDID 补充。
/// 采集较耗时，调用方应放后台线程（Task.Run）；分组独立容错，保留可用结果并在明细标记来源失败。</summary>
public static class HardwareInfoService
{
    const string Cimv2 = @"root\cimv2";

    /// <summary>采集进度（Step 从 0 起，Total 为总阶段数）。</summary>
    public record CollectProgress(int Step, int Total, string Stage);

    /// <summary>采集整机硬件信息；在阶段边界协作取消，不能中断正在执行的 Windows 原生调用。</summary>
    public static HardwareReport Collect(IProgress<CollectProgress>? progress = null,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var report = new HardwareReport();
        const int total = 10;

        void BeginStage(int step, string title)
        {
            cancellationToken.ThrowIfCancellationRequested();
            progress?.Report(new(step, total, title));
            cancellationToken.ThrowIfCancellationRequested();
        }

        // LibreHardwareMonitor 整体只 Open 一次，快照分给各分组用
        BeginStage(0, "处理器（CPU）");
        var lhm = LhmService.Collect(cancellationToken);
        cancellationToken.ThrowIfCancellationRequested();

        report.Groups.Add(SafeGroup("处理器（CPU）", g => CollectCpu(g, lhm)));
        BeginStage(1, "显卡（GPU）");
        report.Groups.Add(SafeGroup("显卡（GPU）", g => CollectGpu(g, lhm)));
        BeginStage(2, "内存");
        report.Groups.Add(SafeGroup("内存", g => CollectMemory(g, lhm)));
        BeginStage(3, "硬盘");
        report.Groups.Add(SafeGroup("硬盘", g => CollectDisks(g, lhm)));
        BeginStage(4, "网卡");
        report.Groups.Add(SafeGroup("网卡", CollectNics));
        BeginStage(5, "屏幕");
        report.Groups.Add(SafeGroup("屏幕", CollectDisplays));
        BeginStage(6, "电池");
        report.Groups.Add(SafeGroup("电池", g => CollectBattery(g, lhm)));
        BeginStage(7, "BIOS");
        report.Groups.Add(SafeGroup("BIOS", g => CollectBios(g, lhm)));
        BeginStage(8, "整机");
        report.Groups.Add(SafeGroup("整机", g => CollectSystem(g, lhm)));
        BeginStage(9, "主板");
        report.Groups.Add(SafeGroup("主板", g => CollectBoard(g, lhm)));

        var errors = new List<string>();
        if (!string.IsNullOrWhiteSpace(lhm.Error)) errors.Add(lhm.Error.Trim());
        errors.AddRange(lhm.Errors.Where(e => !string.IsNullOrWhiteSpace(e)).Select(e => e.Trim()));
        if (errors.Count > 0)
        {
            var g = new InfoGroup("采集诊断");
            g.Add("LHM", "部分采集失败，已保留可用结果");
            g.AddDetail("数据来源", "LhmService.Snapshot.Error / Errors；失败不表示硬件不存在。");
            foreach (string error in errors.Distinct()) g.AddDetail("LHM 失败", error);
            report.Groups.Add(g);
        }
        cancellationToken.ThrowIfCancellationRequested();
        return report;
    }

    /// <summary>分组独立容错，保留已取得的数据；取消异常继续向调用方传播。</summary>
    static InfoGroup SafeGroup(string title, Action<InfoGroup> fill)
    {
        var g = new InfoGroup(title);
        try { fill(g); }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            g.Add("采集结果", "获取失败：" + ex.Message);
            g.AddDetail("分组失败", ex.Message);
        }
        if (g.Entries.Count == 0) g.Add("采集结果", "未取得可用信息（可能未安装、来源不可用或固件未填写）");
        return g;
    }

    /// <summary>多子项分隔：第 2 个及以后的子项前插一个空行。</summary>
    static void Separator(InfoGroup g, int index)
    {
        if (index > 0) g.Add("", "");
    }

    // ═══ 固件 / 整机 / 主板 ═══

    static void CollectBios(InfoGroup g, LhmService.Snapshot lhm)
    {
        g.AddDetail("数据来源", "LHM SMBios.Bios（Type 0），固件提供的厂商、版本和发布日期；非实时检测。");
        var bios = lhm.Smbios?.Bios;
        if (bios == null) { g.AddDetail("SMBIOS", "未取得 BIOS 固件记录"); return; }
        AddText(g, "厂商", bios.Vendor);
        AddText(g, "版本", bios.Version);
        if (bios.Date is DateTime date && date > DateTime.MinValue && date < DateTime.MaxValue)
            g.Add("发布日期", date.ToString("yyyy-MM-dd"));
    }

    static void CollectSystem(InfoGroup g, LhmService.Snapshot lhm)
    {
        g.AddDetail("数据来源", "LHM SMBios.System（Type 1），整机厂商填写的静态标识，未填写或占位值不展示。");
        var system = lhm.Smbios?.System;
        if (system == null) { g.AddDetail("SMBIOS", "未取得整机固件记录"); return; }
        AddText(g, "厂商", system.ManufacturerName);
        AddText(g, "型号", system.ProductName);
        AddText(g, "版本", system.Version);
        AddText(g, "产品系列（Family）", system.Family);
        AddText(g, "序列号", system.SerialNumber);
    }

    static void CollectBoard(InfoGroup g, LhmService.Snapshot lhm)
    {
        g.AddDetail("数据来源", "LHM SMBios.Board（Type 2），主板厂商填写的静态标识，未填写或占位值不展示。");
        var board = lhm.Smbios?.Board;
        if (board == null) { g.AddDetail("SMBIOS", "未取得主板固件记录"); return; }
        AddText(g, "厂商", board.ManufacturerName);
        AddText(g, "型号", board.ProductName);
        AddText(g, "版本", board.Version);
        AddText(g, "序列号", board.SerialNumber);
    }

    // ═══ CPU ═══

    static void CollectCpu(InfoGroup g, LhmService.Snapshot lhm)
    {
        g.AddDetail("数据来源", "型号优先 LHM CPU（CPUID）；核心/线程来自 Win32_Processor。WMI 与 SMBIOS 按各自记录列出，不与 LHM 按索引合并。");
        var names = lhm.Cpus.Select(CleanText).Where(n => n.Length > 0).ToList();
        for (int i = 0; i < names.Count; i++)
        {
            Separator(g, i);
            g.Add(names.Count > 1 ? $"型号 #{i + 1}" : "型号", names[i]);
        }

        var cpus = WmiQuery(Cimv2,
            "SELECT Name, NumberOfCores, NumberOfLogicalProcessors FROM Win32_Processor", g);
        for (int i = 0; i < cpus.Count; i++)
        {
            var mo = cpus[i];
            string tag = cpus.Count > 1 ? $" #{i + 1}" : "";
            string name = CleanText(GetStr(mo, "Name"));
            if (names.Count == 0)
            {
                Separator(g, i);
                if (name.Length > 0) g.Add("型号（WMI）" + tag, name);
            }
            if (name.Length > 0) g.AddDetail("WMI 型号" + tag, name);
            uint cores = GetU32(mo, "NumberOfCores");
            uint threads = GetU32(mo, "NumberOfLogicalProcessors");
            string counts = $"{(KnownNumber(cores) ? $"{cores} 核" : "核心未知")} / {(KnownNumber(threads) ? $"{threads} 线程" : "线程未知")}";
            g.Add("核心 / 线程（WMI）" + tag, counts + (name.Length > 0 ? $"（{name}）" : ""));
        }

        g.AddDetail("SMBIOS 来源", "LHM SMBios.Processors（Type 4）；以下为固件声明，非实时频率或当前可用核心数，各记录不与 CPUID/WMI 按索引关联。");
        var processors = lhm.Smbios?.Processors;
        if (processors == null || processors.Length == 0)
            g.AddDetail("SMBIOS", "未取得处理器固件记录");
        else
            for (int i = 0; i < processors.Length; i++)
            {
                var p = processors[i];
                string tag = $"SMBIOS 处理器 {i + 1} · ";
                AddText(g, tag + "型号/版本", p.Version, true);
                AddText(g, tag + "厂商", p.ManufacturerName, true);
                AddText(g, tag + "插槽名", p.SocketDesignation, true);
                AddText(g, tag + "插槽类型", EnumText(p.Socket), true);
                AddText(g, tag + "处理器类型", EnumText(p.ProcessorType), true);
                if (KnownNumber(p.CoreCount)) g.AddDetail(tag + "固件核心数", p.CoreCount.ToString());
                if (KnownNumber(p.CoreEnabled)) g.AddDetail(tag + "固件启用核心数", p.CoreEnabled.ToString());
                if (KnownNumber(p.ThreadCount)) g.AddDetail(tag + "固件线程数", p.ThreadCount.ToString());
                if (KnownNumber(p.MaxSpeed)) g.AddDetail(tag + "固件最大速度", $"{p.MaxSpeed} MHz");
            }
    }

    // ═══ GPU ═══

    static void CollectGpu(InfoGroup g, LhmService.Snapshot lhm)
    {
        g.AddDetail("数据来源", "型号/驱动为 Win32_VideoController；显存依次用名称匹配的 LHM GPU Memory Total（NVIDIA NVAPI、AMD ADL、Intel D3D）、注册表 qwMemorySize、WMI AdapterRAM（32 位，仅估算）。名称相等或互含仅作启发式匹配，LHM 原记录独立保留；WMI 无记录或失败时仍显示 LHM 型号/显存。");
        var gpus = WmiQuery(Cimv2,
            "SELECT Name, AdapterRAM, PNPDeviceID, DriverVersion FROM Win32_VideoController", g);
        // 显存精度优先级：LHM「GPU Memory Total」（NVAPI/ADL 直读）> 注册表 qwMemorySize > WMI AdapterRAM（uint32，>4GB 溢出，仅兜底）
        var regVram = ReadRegistryVram();
        for (int i = 0; i < gpus.Count; i++)
        {
            var mo = gpus[i];
            Separator(g, i);
            string name = CleanText(GetStr(mo, "Name"));
            string tag = gpus.Count > 1 ? $" #{i + 1}" : "";
            g.Add("型号" + tag, name.Length > 0 ? name : "无法读取");

            var lhmGpu = FindByName(lhm.Gpus.Select(x => (x.Name, x)), name)?.Obj;
            if (lhmGpu?.VramMB is double vramMB && KnownCapacity(vramMB))
            {
                g.Add("显存" + tag, FormatSize((long)(vramMB * 1024 * 1024)));
            }
            else
            {
                long? vram = MatchRegistryVram(regVram, name, GetStr(mo, "PNPDeviceID"));
                if (vram is > 0)
                    g.Add("显存" + tag, FormatSize(vram.Value));
                else
                {
                    ulong wmi = GetU64(mo, "AdapterRAM");
                    g.Add("显存" + tag, KnownNumber(wmi) ? $"约 {FormatSize((long)wmi)}（WMI 估算值）" : "无法读取");
                }
            }

            string drv = CleanText(GetStr(mo, "DriverVersion"));
            if (drv.Length > 0) g.Add("驱动版本" + tag, drv);
        }
        // 独立保留 LHM 记录，WMI 枚举中途失败或名称未匹配也不会遮蔽已取得的 GPU
        for (int i = 0; i < lhm.Gpus.Count; i++)
        {
            var lg = lhm.Gpus[i];
            string tag = lhm.Gpus.Count > 1 ? $" #{i + 1}" : "";
            AddText(g, "LHM 型号" + tag, lg.Name, true);
            if (lg.VramMB is double lhmVramMB && KnownCapacity(lhmVramMB))
                g.AddDetail("LHM 显存" + tag, FormatSize((long)(lhmVramMB * 1024 * 1024)));
            if (gpus.Count == 0)
            {
                Separator(g, i);
                AddText(g, "型号" + tag, lg.Name);
                if (lg.VramMB is double fallbackVramMB && KnownCapacity(fallbackVramMB))
                    g.Add("显存" + tag, FormatSize((long)(fallbackVramMB * 1024 * 1024)));
            }
        }
    }

    /// <summary>显卡类注册表项里的准确显存（HardwareInformation.qwMemorySize，REG_QWORD）。</summary>
    record VramEntry(string MatchingDeviceId, string DriverDesc, long Bytes);

    static List<VramEntry> ReadRegistryVram()
    {
        var list = new List<VramEntry>();
        try
        {
            using var klass = Registry.LocalMachine.OpenSubKey(
                @"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}");
            if (klass == null) return list;
            foreach (var sub in klass.GetSubKeyNames())
            {
                try
                {
                    using var key = klass.OpenSubKey(sub);
                    if (key?.GetValue("HardwareInformation.qwMemorySize") is not long bytes || bytes <= 0)
                        continue;
                    list.Add(new VramEntry(
                        ((key.GetValue("MatchingDeviceId") as string) ?? "").ToLowerInvariant(),
                        (key.GetValue("DriverDesc") as string) ?? "", bytes));
                }
                catch (Exception ex) when (ex is not OperationCanceledException) { /* 单个子键失败跳过 */ }
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException) { /* 无权限/键不存在 → 退回 WMI 估算 */ }
        return list;
    }

    static long? MatchRegistryVram(List<VramEntry> entries, string name, string pnpId)
    {
        // 先按 PNPDeviceID 包含 MatchingDeviceId 匹配（如 pci\ven_10de&dev_2520）
        string pnp = pnpId.ToLowerInvariant();
        foreach (var e in entries)
            if (e.MatchingDeviceId.Length > 0 && pnp.Contains(e.MatchingDeviceId))
                return e.Bytes;
        // 再按驱动描述 == WMI 名称
        foreach (var e in entries)
            if (e.DriverDesc.Length > 0 && e.DriverDesc.Equals(name, StringComparison.OrdinalIgnoreCase))
                return e.Bytes;
        return null;
    }

    // ═══ 内存 ═══

    static void CollectMemory(InfoGroup g, LhmService.Snapshot lhm)
    {
        g.AddDetail("数据来源", "总容量优先 LHM /ram 总量（Windows 物理内存口径），退回 Win32_PhysicalMemory.Capacity 求和；单条容量来自 LHM SPD 或 WMI，不使用 SMBIOS 容量编码推算或求和。");
        g.AddDetail("来源对应", "LHM SPD、WMI、SMBIOS 每种来源独立编号，不按索引硬匹配；固件/WMI 配置速度非实时测频。");
        var sticks = WmiQuery(Cimv2,
            "SELECT Capacity, Speed, ConfiguredClockSpeed, Manufacturer, PartNumber, DeviceLocator FROM Win32_PhysicalMemory", g);
        var installed = sticks.Where(mo => KnownNumber(GetU64(mo, "Capacity"))).ToList();

        // 总容量：LHM Windows 物理内存总量优先，退回 WMI 求和
        if (lhm.MemoryTotalGB is double totalGB && KnownCapacity(totalGB))
            g.Add("总容量", FormatSize((long)(totalGB * 1073741824.0)));
        else
        {
            ulong total = 0;
            foreach (var mo in installed) total += GetU64(mo, "Capacity");
            if (total > 0) g.Add("总容量", FormatSize((long)total));
        }

        // ConfiguredClockSpeed 是 WMI 报告的配置速度，不是实时频率
        var actual = installed.Select(mo => GetU32(mo, "ConfiguredClockSpeed"))
            .Where(v => KnownNumber(v)).Distinct().ToList();
        g.Add("配置速度（WMI）", actual.Count > 0
            ? string.Join(" / ", actual.Select(v => $"{v} MT/s"))
            : "无法读取");

        // ── 详细信息（折叠区，默认收起）：各来源单独列出 ──
        int slots = QueryTotalSlots(g);
        int spdCount = lhm.Dimms.Count(d => KnownCapacity(d.CapacityGB));
        int stickCount = spdCount > 0 ? spdCount : installed.Count;
        if (stickCount > 0)
            g.AddDetail("插槽占用", slots >= stickCount ? $"{stickCount} / {slots}" : $"{stickCount} 根（总槽数未知）");
        g.AddDetail("插槽来源", "已安装数量仅计容量有效的 LHM SPD 记录，缺失时用容量有效的 WMI 记录；总槽数为 Win32_PhysicalMemoryArray.MemoryDevices 求和，板载内存和来源不全可能影响口径。");

        g.AddDetail("SPD 来源", "LHM DIMM 直读（需 PawnIO 驱动）；名称含厂商/料号，以下不与 WMI/SMBIOS 合并。");
        for (int i = 0; i < lhm.Dimms.Count; i++)
        {
            var d = lhm.Dimms[i];
            string cap = d.CapacityGB is double capacityGB && KnownCapacity(capacityGB)
                ? FormatSize((long)(capacityGB * 1073741824.0)) : "容量未知";
            string name = CleanText(d.Name);
            g.AddDetail($"SPD 记录 {i + 1}", cap + (name.Length > 0 ? "　" + name : ""));
        }

        // SPD 有结果时也保留 WMI 每条记录的有效明细
        g.AddDetail("WMI 来源", "Win32_PhysicalMemory：容量、最大速度 Speed、配置速度 ConfiguredClockSpeed、厂商、料号、DeviceLocator；非直接 SPD 读数。");
        for (int i = 0; i < sticks.Count; i++)
        {
            var mo = sticks[i];
            ulong capacity = GetU64(mo, "Capacity");
            var sb = new StringBuilder(KnownNumber(capacity) ? FormatSize((long)capacity) : "容量未知（不计安装数量）");
            uint speed = GetU32(mo, "Speed");
            uint cfg = GetU32(mo, "ConfiguredClockSpeed");
            if (KnownNumber(speed)) sb.Append($"　最大 {speed} MT/s");
            if (KnownNumber(cfg)) sb.Append($"　配置 {cfg} MT/s");
            string vendorPart = $"{CleanText(GetStr(mo, "Manufacturer"))} {CleanText(GetStr(mo, "PartNumber"))}".Trim();
            if (vendorPart.Length > 0) sb.Append($"　{vendorPart}");
            string loc = CleanText(GetStr(mo, "DeviceLocator"));
            g.AddDetail(loc.Length > 0 ? $"WMI 记录 {i + 1}（{loc}）" : $"WMI 记录 {i + 1}", sb.ToString());
        }

        g.AddDetail("SMBIOS 来源", "LHM SMBios.MemoryDevices（Type 17）；类型/标识/速度/电压均为固件声明，非实时检测。Size=0 的空槽跳过，其余仅列固件记录（容量未校验、不计安装数量）；不解释特殊容量编码，也不换算或求和。");
        var devices = lhm.Smbios?.MemoryDevices;
        if (devices == null || devices.Length == 0)
            g.AddDetail("SMBIOS", "未取得内存固件记录");
        else
            for (int i = 0; i < devices.Length; i++)
            {
                var d = devices[i];
                if (d.Size == 0) continue;
                string tag = $"SMBIOS 内存记录 {i + 1} · ";
                AddText(g, tag + "类型", EnumText(d.Type), true);
                AddText(g, tag + "厂商", d.ManufacturerName, true);
                AddText(g, tag + "料号", d.PartNumber, true);
                AddText(g, tag + "序列号", d.SerialNumber, true);
                AddText(g, tag + "Bank", d.BankLocator, true);
                AddText(g, tag + "DeviceLocator", d.DeviceLocator, true);
                if (KnownNumber(d.ConfiguredVoltage)) g.AddDetail(tag + "配置电压", $"{d.ConfiguredVoltage} mV");
                if (KnownNumber(d.Speed)) g.AddDetail(tag + "最大速度", $"{d.Speed} MT/s");
                if (KnownNumber(d.ConfiguredSpeed)) g.AddDetail(tag + "配置速度", $"{d.ConfiguredSpeed} MT/s");
            }
    }

    /// <summary>内存插槽总数（Win32_PhysicalMemoryArray.MemoryDevices 求和）；查询失败保留已取得的结果。</summary>
    static int QueryTotalSlots(InfoGroup g)
    {
        int sum = 0;
        foreach (var mo in WmiQuery(Cimv2, "SELECT MemoryDevices FROM Win32_PhysicalMemoryArray", g))
        {
            uint count = GetU32(mo, "MemoryDevices");
            if (KnownNumber(count) && count <= int.MaxValue - sum) sum += (int)count;
        }
        return sum;
    }

    // ═══ 硬盘 ═══

    static void CollectDisks(InfoGroup g, LhmService.Snapshot lhm)
    {
        g.AddDetail("数据来源", "LHM StorageDevice / DiskInfoToolkit 提供型号、容量和 SMART；不可用时退回 Win32_DiskDrive，WMI 无 SMART 则保持未知。盘符仍按物理盘号 Index / StorageDeviceNumber 经 WMI 分区关联。");
        // 每块盘四行：容量　(盘符)　型号（一行；盘符可多个，无盘符不显示括号）+ 健康度 + 通电时间
        // + 0E 介质与数据完整性错误（SMART，拿不到显示「未知」）；盘变多行后恢复盘间空行（与 GPU/屏幕一致）
        if (lhm.Disks.Count > 0)
        {
            // 盘符按物理盘号关联：LHM StorageDeviceNumber = Windows 物理盘号 = Win32_DiskDrive.Index；
            // 对应不上（DeviceNumber 缺失或 WMI 无记录）就不显示括号，不报错
            var letters = QueryDiskLetters(g);
            for (int i = 0; i < lhm.Disks.Count; i++)
            {
                Separator(g, i);
                var d = lhm.Disks[i];
                string tag = lhm.Disks.Count > 1 ? $" #{i + 1}" : "";
                string cap = d.SizeBytes is > 0 ? FormatSize((long)d.SizeBytes.Value) : "容量未知";
                g.Add("硬盘" + tag, cap + FormatLetters(
                    d.DeviceNumber is uint n ? letters.GetValueOrDefault((int)n) : null)
                    + $"　{(d.Name.Length > 0 ? d.Name : "未知型号")}");
                g.Add("健康度" + tag, d.HealthPercent is int hp ? $"{hp} %" : "未知");
                g.Add("通电时间" + tag, FormatHours(d.PowerOnHours));
                g.Add("介质与数据完整性错误" + tag, d.MediaErrors is long me ? $"{me} 次" : "未知");
            }
            return;
        }

        // WMI 兜底通道没有 SMART：容量/型号照列，健康度/通电时间/0E「未知」（不强行反推）；
        // 本通道本来就有 Win32_DiskDrive.Index，直接关联盘符
        var disks = WmiQuery(Cimv2, "SELECT Model, Size, Index FROM Win32_DiskDrive", g);
        var wmiLetters = QueryDiskLetters(g);
        for (int i = 0; i < disks.Count; i++)
        {
            Separator(g, i);
            var mo = disks[i];
            string tag = disks.Count > 1 ? $" #{i + 1}" : "";
            ulong size = GetU64(mo, "Size");
            string cap = size > 0 ? FormatSize((long)size) : "容量未知";
            string model = GetStr(mo, "Model");
            g.Add("硬盘" + tag, cap + FormatLetters(wmiLetters.GetValueOrDefault((int)GetU32(mo, "Index")))
                + $"　{(model.Length > 0 ? model : "未知型号")}");
            g.Add("健康度" + tag, "未知");
            g.Add("通电时间" + tag, "未知");
            g.Add("介质与数据完整性错误" + tag, "未知");
        }
    }

    /// <summary>盘符列表 → 「　(C: D:)」片段；null 或空列表返回空串（不显示括号）。</summary>
    static string FormatLetters(List<string>? letters) =>
        letters is { Count: > 0 } ? $"　({string.Join(" ", letters)})" : "";

    /// <summary>物理盘 Index → 盘符列表（如 0 → [C:];同一盘多盘符都收）。关联链：
    /// Win32_DiskDrive → Win32_DiskDriveToDiskPartition → Win32_LogicalDiskToPartition → Win32_LogicalDisk；
    /// 任何一步失败返回已有结果（最坏空表，调用方不显示括号）。</summary>
    static Dictionary<int, List<string>> QueryDiskLetters(InfoGroup g)
    {
        var map = new Dictionary<int, List<string>>();
        try
        {
            // 物理盘 DeviceID（\\.\PHYSICALDRIVE0）→ Index
            var diskIdToIndex = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            foreach (var mo in WmiQuery(Cimv2, "SELECT DeviceID, Index FROM Win32_DiskDrive"))
                diskIdToIndex[GetStr(mo, "DeviceID")] = (int)GetU32(mo, "Index");

            // 分区 DeviceID（Disk #0, Partition #1）→ 物理盘 Index
            var partToDisk = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            foreach (var mo in WmiQuery(Cimv2, "SELECT Antecedent, Dependent FROM Win32_DiskDriveToDiskPartition"))
            {
                string partId = ExtractPathDeviceId(GetStr(mo, "Dependent"));
                if (partId.Length > 0 &&
                    diskIdToIndex.TryGetValue(ExtractPathDeviceId(GetStr(mo, "Antecedent")), out int idx))
                    partToDisk[partId] = idx;
            }

            // 分区 → 逻辑盘符（C:）
            foreach (var mo in WmiQuery(Cimv2, "SELECT Antecedent, Dependent FROM Win32_LogicalDiskToPartition"))
            {
                string letter = ExtractPathDeviceId(GetStr(mo, "Dependent"));
                if (letter.Length > 0 &&
                    partToDisk.TryGetValue(ExtractPathDeviceId(GetStr(mo, "Antecedent")), out int idx))
                {
                    if (!map.TryGetValue(idx, out var list)) map[idx] = list = [];
                    list.Add(letter);
                }
            }
            foreach (var list in map.Values) list.Sort(StringComparer.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            g.AddDetail("WMI 盘符关联失败", ex.Message + "（保留已取得的盘符，硬盘信息继续显示）");
        }
        return map;
    }

    /// <summary>从 WMI 对象路径（形如 \\机器\root\cimv2:Win32_DiskDrive.DeviceID="\\\\.\\PHYSICALDRIVE0"）
    /// 取出 DeviceID 原值（去 \\ 转义）；取不到返回空串。</summary>
    static string ExtractPathDeviceId(string objectPath)
    {
        const string marker = "DeviceID=\"";
        int i = objectPath.LastIndexOf(marker, StringComparison.OrdinalIgnoreCase);
        if (i < 0) return "";
        i += marker.Length;
        int j = objectPath.IndexOf('"', i);
        return j < 0 ? "" : objectPath[i..j].Replace("\\\\", "\\");
    }

    /// <summary>通电时间：「N 小时」，≥24 小时附约天数。</summary>
    static string FormatHours(long? hours) =>
        hours is not > 0 ? "未知" : hours >= 24 ? $"{hours} 小时（约 {hours / 24} 天）" : $"{hours} 小时";

    // ═══ 网卡 ═══

    static void CollectNics(InfoGroup g)
    {
        g.AddDetail("数据来源", "Win32_NetworkAdapter，PhysicalAdapter=True；包含未连接的物理网卡，名称保持 Windows 原值。");
        // 全部物理网卡（含未连接），口径与设备管理器一致；名称不做清洗，原样显示
        var nics = WmiQuery(Cimv2,
            "SELECT Name FROM Win32_NetworkAdapter WHERE PhysicalAdapter = True", g);
        for (int i = 0; i < nics.Count; i++)
        {
            string tag = nics.Count > 1 ? $" #{i + 1}" : "";
            string name = GetStr(nics[i], "Name");
            g.Add("网卡" + tag, name.Length > 0 ? name : "未知型号");
        }
    }

    // ═══ 屏幕 ═══

    static void CollectDisplays(InfoGroup g)
    {
        g.AddDetail("数据来源", "DisplayInfoService：分辨率/刷新率来自 Windows 显示模式，尺寸/面板标识/色度坐标来自 EDID；色域为 EDID 推算的容积比，非覆盖率或仪器实测。");
        var displays = DisplayInfoService.GetDisplays();
        for (int i = 0; i < displays.Count; i++)
        {
            Separator(g, i);
            var d = displays[i];
            string tag = displays.Count > 1 ? $" #{i + 1}" : "";
            g.Add("分辨率" + tag, d.Width > 0 ? $"{d.Width} × {d.Height}" : "无法读取");
            g.Add("刷新率" + tag, d.Frequency > 0 ? $"{d.Frequency} Hz" : "无法读取");

            var e = d.Edid;
            g.Add("尺寸" + tag, e is { DiagonalInches: > 0 }
                ? $"{e.DiagonalInches:0.0} 英寸（{e.WidthCm} × {e.HeightCm} cm）"
                : "无法读取");

            string mfgProd = e != null ? $"{e.Manufacturer} {e.ProductCode}".Trim() : "";
            g.Add("面板" + tag, e != null && e.PanelName.Length > 0
                ? (mfgProd.Length > 0 ? $"{e.PanelName}（{mfgProd}）" : e.PanelName)
                : "无法读取");

            // 注意：由 EDID 色度坐标算的是色域「容积比」而非覆盖率（顶点可超出标准色域，比值可 >100%）
            g.Add("色域" + tag, e is { SrgbVolume: > 0 }
                ? $"≈ {e.SrgbVolume:0}% sRGB / {e.P3Volume:0}% DCI-P3（EDID 容积比）"
                : "无法读取");
        }
    }

    // ═══ 电池 ═══

    /// <summary>root\WMI：电池类驱动注册的 WMI 类所在命名空间（BatteryStaticData 等）。</summary>
    const string WmiRoot = @"root\WMI";

    static void CollectBattery(InfoGroup g, LhmService.Snapshot lhm)
    {
        g.AddDetail("数据来源", "LHM 电池容量为电池类驱动 IOCTL；root\\WMI 的 BatteryStaticData / BatteryFullChargedCapacity / BatteryCycleCount 按相同 InstanceName 关联，循环次数仅由 WMI 提供。LHM、Win32_Battery 与 root\\WMI 不按索引合并。");
        for (int i = 0; i < lhm.Batteries.Count; i++)
        {
            Separator(g, i);
            var battery = lhm.Batteries[i];
            string tag = lhm.Batteries.Count > 1 ? $" #{i + 1}" : "";
            AddText(g, "电池（LHM）" + tag, battery.Name);
            double? designed = KnownCapacity(battery.DesignedMWh) ? battery.DesignedMWh : null;
            double? full = KnownCapacity(battery.FullChargedMWh) ? battery.FullChargedMWh : null;
            g.Add("设计容量（LHM）" + tag, designed is > 0 ? $"{designed:N0} mWh" : "无法读取");
            g.Add("完全充电容量（LHM）" + tag, full is > 0 ? $"{full:N0} mWh" : "无法读取");
            if (designed is > 0 && full is > 0)
            {
                double health = Math.Clamp(full.Value / designed.Value * 100.0, 0.0, 100.0);
                g.Add("损耗（LHM）" + tag, $"{100.0 - health:0.#} %（健康度 {health:0.#} %）");
            }
            else
                g.Add("损耗（LHM）" + tag, "无法读取");
        }

        var wmiBatteries = WmiQuery(Cimv2, "SELECT Name FROM Win32_Battery", g);
        for (int i = 0; i < wmiBatteries.Count; i++)
            AddText(g, $"Win32_Battery 记录 {i + 1}", GetStr(wmiBatteries[i], "Name"), true);
        var statics = QueryRootWmiBattery(g, "BatteryStaticData", "DesignedCapacity");
        var fulls = QueryRootWmiBattery(g, "BatteryFullChargedCapacity", "FullChargedCapacity");
        var cycles = QueryRootWmiBattery(g, "BatteryCycleCount", "CycleCount");
        var instances = statics.Keys.Concat(fulls.Keys).Concat(cycles.Keys).Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        Action<string, string> addWmi = lhm.Batteries.Count > 0 ? g.AddDetail : g.Add;
        for (int i = 0; i < instances.Count; i++)
        {
            string instance = instances[i];
            string tag = $"（WMI #{i + 1}）";
            g.AddDetail("WMI 电池实例" + tag, instance);
            uint designed = statics.GetValueOrDefault(instance);
            uint full = fulls.GetValueOrDefault(instance);
            uint cycle = cycles.GetValueOrDefault(instance);
            addWmi("设计容量" + tag, KnownNumber(designed) ? $"{designed:N0} mWh" : "无法读取");
            addWmi("完全充电容量" + tag, KnownNumber(full) ? $"{full:N0} mWh" : "无法读取");
            if (KnownNumber(designed) && KnownNumber(full))
            {
                double health = Math.Clamp((double)full / designed * 100.0, 0.0, 100.0);
                addWmi("损耗" + tag, $"{100.0 - health:0.#} %（健康度 {health:0.#} %）");
            }
            else
                addWmi("损耗" + tag, "无法读取");
            g.Add("循环次数" + tag, cycles.ContainsKey(instance) && cycle != uint.MaxValue ? $"{cycle} 次" : "无法读取");
        }
        if (cycles.Count == 0) g.Add("循环次数（WMI）", "无法读取，未用容量反推");
        if (lhm.Batteries.Count == 0 && instances.Count == 0)
            g.Add("电池", wmiBatteries.Count > 0 ? "已检测到，容量无法读取" : "未取得电池信息（可能无电池或来源不可用）");
    }

    /// <summary>root\WMI 电池类按 InstanceName 取属性；失败在分组明细标记，并保留已取记录。</summary>
    static Dictionary<string, uint> QueryRootWmiBattery(InfoGroup g, string className, string prop)
    {
        var values = new Dictionary<string, uint>(StringComparer.OrdinalIgnoreCase);
        foreach (var mo in WmiQuery(WmiRoot, $"SELECT InstanceName, {prop} FROM {className}", g))
        {
            string instance = GetStr(mo, "InstanceName");
            if (instance.Length == 0)
            {
                g.AddDetail("WMI 电池记录", $"{className} 缺少 InstanceName，未关联该记录");
                continue;
            }
            try
            {
                if (mo[prop] is object value) values[instance] = Convert.ToUInt32(value);
                else g.AddDetail("WMI 电池记录", $"{className}.{prop} 为空，未作为零值关联");
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            { g.AddDetail("WMI 电池记录", $"{className}.{prop} 读取失败：{ex.Message}"); }
        }
        return values;
    }

    // ═══ 通用辅助 ═══

    /// <summary>在 (名称, 对象) 序列里按名称匹配（相等或互相包含，忽略大小写）。</summary>
    static (string Name, T Obj)? FindByName<T>(IEnumerable<(string Name, T Obj)> items, string name)
    {
        if (CleanText(name).Length == 0) return null;
        foreach (var it in items)
            if (CleanText(it.Name).Length > 0 && it.Name.Equals(name, StringComparison.OrdinalIgnoreCase))
                return it;
        foreach (var it in items)
            if (CleanText(it.Name).Length > 0 && (name.Contains(it.Name, StringComparison.OrdinalIgnoreCase) ||
                it.Name.Contains(name, StringComparison.OrdinalIgnoreCase)))
                return it;
        return null;
    }

    static List<ManagementObject> WmiQuery(string scope, string wql, InfoGroup? g = null)
    {
        var rows = new List<ManagementObject>();
        try
        {
            using var searcher = new ManagementObjectSearcher(scope, wql);
            using var results = searcher.Get();
            foreach (ManagementObject mo in results) rows.Add(mo);
        }
        catch (Exception ex) when (ex is not OperationCanceledException && g != null)
        {
            g.AddDetail("WMI 查询失败", $"{scope}：{wql}；{ex.Message}（保留已取得的数据）");
        }
        return rows;
    }

    static string CleanText(string? value)
    {
        string text = value?.Trim().Trim('\0').Trim() ?? "";
        string key = text.TrimEnd('.').ToLowerInvariant();
        if (key is "" or "unknown" or "other" or "none" or "n/a" or "na" or "not applicable"
            or "not specified" or "not available" or "not present" or "undefined" or "invalid"
            or "default" or "default string" or "oem" or "to be filled by oem" or "to be filled by o.e.m"
            or "system manufacturer" or "system product name" or "system version" or "system serial number"
            or "system family" or "base board manufacturer" or "base board product name" or "base board version"
            or "base board serial number" or "chassis serial number" or "serial number" or "serialnumber"
            or "未指定" or "未知" or "无") return "";
        if (text.All(c => c == '0') || (text.Length >= 4 && text.All(c => c is 'f' or 'F'))
            || text.All(c => c == '-')) return "";
        return text;
    }

    static void AddText(InfoGroup g, string name, string? value, bool detail = false)
    {
        string text = CleanText(value);
        if (text.Length == 0) return;
        if (detail) g.AddDetail(name, text);
        else g.Add(name, text);
    }

    static string EnumText<T>(T value) where T : struct, Enum =>
        Enum.IsDefined(value) && Convert.ToUInt64(value) != 0 ? CleanText(value.ToString()) : "";

    static bool KnownNumber(ulong value) => value > 0 && value < long.MaxValue
        && value != ushort.MaxValue && value != uint.MaxValue;

    static bool KnownCapacity(double? value) => value is > 0 and < uint.MaxValue && value.Value != ushort.MaxValue;

    static string GetStr(ManagementObject mo, string prop)
    {
        try { return mo[prop]?.ToString()?.Trim() ?? ""; }
        catch (Exception ex) when (ex is not OperationCanceledException) { return ""; }
    }

    static uint GetU32(ManagementObject mo, string prop)
    {
        try { return Convert.ToUInt32(mo[prop]); }
        catch (Exception ex) when (ex is not OperationCanceledException) { return 0; }
    }

    static ulong GetU64(ManagementObject mo, string prop)
    {
        try { return Convert.ToUInt64(mo[prop]); }
        catch (Exception ex) when (ex is not OperationCanceledException) { return 0; }
    }

    /// <summary>字节数 → GB/TB（1024 进制，与 Windows 口径一致）。</summary>
    static string FormatSize(long bytes)
    {
        double gb = bytes / 1073741824.0;
        return gb >= 1024 ? $"{gb / 1024:0.0#} TB" : $"{gb:0.#} GB";
    }
}
