// 功能：CPUID 采集+解码探针——按 30 §10 候选形状写 cpuid.json，并做候选B解码
// 背景：L4 切片；解码归属已由用户于 2026-10-14 冻结为候选B（C# 解码，交原证据+明确规则）
// 方法：X86Base.CpuId 采固定叶 → 原寄存器（原证据）+ decoded（cpuid-decode-b-v1）→ 临时文件关闭后提交最终名；探针独立 exe，不改 demo-args-v1 契约

using System.Runtime.Intrinsics.X86;
using System.Text;
using System.Text.Json;

namespace CpuidProbe;

// 功能：单条寄存器记录，对应 30 §10 的六键固定形状（原证据）
// 背景：leaf/subleaf 不得重复；值保留 32 位位模式
// 方法：全部为 8 位小写十六进制字符串、不带 0x
internal sealed record RegisterEntry(string Leaf, string Subleaf, string Eax, string Ebx, string Ecx, string Edx);

// 功能：诊断条目，对应 30 §12 diagnostics 候选形状
// 背景：sourceId 允许 null（非来源相关诊断）
// 方法：code 稳定标识；severity=info/warning/error；stage=request/collect/parse/check/output/demo
internal sealed record DiagnosticEntry(string Code, string Severity, string Stage, string? SourceId, string Message);

// 功能：单个解码结果，状态沿用 30 §12 字段状态枚举
// 背景：present 才有 value；invalid 保留于诊断与原寄存器，不伪造；来源失败用 unavailable
// 方法：status ∈ present / notProvided / invalid / unavailable
internal sealed record DecodedValue(string Status, string? Value);

// 功能：候选B解码结果容器——decodeRuleId 即"明确解码规则"的标识
// 背景：规则全文见 README「解码规则 cpuid-decode-b-v1」；交 Java 时原寄存器与本对象同时给出
// 方法：vendor 来自叶 00000000；brand 来自叶 80000002/3/4
internal sealed record CpuidDecoded(string DecodeRuleId, DecodedValue Vendor, DecodedValue Brand);

// 功能：source 对象，对应 30 §10 请求根键 source，并带候选B扩展键 decoded
// 背景：status=captured/notSupported/failed；后两者 registers 为空且至少一条诊断，不伪造全零寄存器
// 方法：sourceId/kind 固定 cpuid；decoded 为探针候选B扩展，正式契约未定前以此 README 为准
internal sealed record CpuidSource(string SourceId, string Kind, string Status, List<RegisterEntry> Registers, List<DiagnosticEntry> Diagnostics, CpuidDecoded Decoded);

// 功能：请求根对象，对应 30 §10 候选形状
// 背景：探针无真实 Java 命令，executionId/captureId 为探针自建的占位身份（Guid N 格式），见 README
// 方法：schemaVersion 整数 1；mode 固定 collect
internal sealed record CpuidRequest(int SchemaVersion, string Mode, string ExecutionId, string CaptureId, CpuidSource Source);

// 功能：探针入口——校验输出目录、执行采集与解码、提交 cpuid.json、打印摘要
// 背景：退出码 0=已采集并提交；1=采集或落盘失败（含最终名已占用）；2=参数/目录不合法
// 方法：参数校验 → Collect → 临时文件 → File.Move 提交 → 控制台 UTF-8 摘要
internal static class Program
{
    // 解码规则标识：README 中 v1 全文为准；改规则必须升 id，不静默改语义
    private const string DecodeRuleId = "cpuid-decode-b-v1";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };

    private static int Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;

        if (args.Length != 1 || string.IsNullOrWhiteSpace(args[0]) || !Path.IsPathFullyQualified(args[0]))
        {
            Console.Error.WriteLine("用法：CpuidProbe <输出目录绝对路径>");
            return 2;
        }

        string outputDirectory = args[0];
        try
        {
            Directory.CreateDirectory(outputDirectory);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"输出目录不可用：{ex.GetType().Name}: {ex.Message}");
            return 2;
        }

        string finalPath = Path.Combine(outputDirectory, "cpuid.json");
        if (File.Exists(finalPath))
        {
            Console.Error.WriteLine($"拒绝覆盖：{finalPath} 已存在（与契约拒绝复用旧文件一致）");
            return 1;
        }

        CpuidSource source = Collect();
        CpuidRequest request = new(
            SchemaVersion: 1,
            Mode: "collect",
            ExecutionId: Guid.NewGuid().ToString("N"),
            CaptureId: Guid.NewGuid().ToString("N"),
            Source: source);

        string json = JsonSerializer.Serialize(request, JsonOptions) + "\n";
        string tempPath = Path.Combine(outputDirectory, "cpuid.json.tmp");
        try
        {
            File.WriteAllText(tempPath, json, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            File.Move(tempPath, finalPath);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"落盘失败：{ex.GetType().Name}: {ex.Message}");
            return 1;
        }

        Console.WriteLine($"status={source.Status}");
        Console.WriteLine($"registers={source.Registers.Count}");
        Console.WriteLine($"vendor={source.Decoded.Vendor.Status} brand={source.Decoded.Brand.Status}");
        Console.WriteLine($"output={finalPath}");
        return source.Status == "captured" ? 0 : 1;
    }

    // 功能：采集 30 §10 候选规定的固定叶，并做候选B解码
    // 背景：保留 00000000 与 80000000 的 subleaf=00000000；扩展最大叶允许时读 80000002/3/4
    // 方法：X86Base.IsSupported 守卫；最大叶按无符号语义比较；原整数先留底再同时生成原证据与解码
    private static CpuidSource Collect()
    {
        var raw = new List<(string Leaf, (int Eax, int Ebx, int Ecx, int Edx) R)>();
        var diagnostics = new List<DiagnosticEntry>();
        bool brandLeavesPresent = false;

        if (!X86Base.IsSupported)
        {
            diagnostics.Add(new DiagnosticEntry("cpuid.notSupported", "error", "collect", "cpuid",
                "当前平台不支持 X86Base.CpuId 内在函数"));
            return new CpuidSource("cpuid", "cpuid", "notSupported", [], diagnostics, UnavailableDecoded());
        }

        try
        {
            Add(raw, 0x00000000, 0);
            var maxExtended = X86Base.CpuId(unchecked((int)0x80000000), 0);
            raw.Add(("80000000", maxExtended));
            // 无符号语义比较：EAX 作为 uint 与 0x80000004 比，避免符号位误判
            if ((uint)maxExtended.Eax >= 0x80000004u)
            {
                Add(raw, unchecked((int)0x80000002), 0);
                Add(raw, unchecked((int)0x80000003), 0);
                Add(raw, unchecked((int)0x80000004), 0);
                brandLeavesPresent = true;
            }
            else
            {
                diagnostics.Add(new DiagnosticEntry("cpuid.brandLeavesAbsent", "info", "collect", "cpuid",
                    "扩展最大叶不足 80000004，品牌叶未采集（来源仍可 captured，规则证据不足）"));
            }
        }
        catch (Exception ex)
        {
            diagnostics.Add(new DiagnosticEntry("cpuid.collectFailed", "error", "collect", "cpuid",
                $"采集异常：{ex.GetType().Name}: {ex.Message}"));
            return new CpuidSource("cpuid", "cpuid", "failed", [], diagnostics, UnavailableDecoded());
        }

        var registers = raw.Select(t => Entry(t.R, t.Leaf, "00000000")).ToList();
        var decoded = Decode(raw, brandLeavesPresent, diagnostics);
        return new CpuidSource("cpuid", "cpuid", "captured", registers, diagnostics, decoded);
    }

    // 功能：候选B解码，规则 cpuid-decode-b-v1（与 README 全文一致）
    // 背景：厂商=叶00000000 按 EBX/EDX/ECX 拼 12 字节；品牌=叶80000002/3/4 顺序、每叶 EAX/EBX/ECX/EDX 拼 48 字节
    // 方法：各寄存器低字节先出；删末尾连续 00；剩余含 00 或非 ASCII(>0x7F) 判 invalid；空串或全 ASCII 空白(U+0009-U+000D/U+0020) 判 notProvided；品牌叶缺失判 notProvided
    private static CpuidDecoded Decode(List<(string Leaf, (int Eax, int Ebx, int Ecx, int Edx) R)> raw, bool brandLeavesPresent, List<DiagnosticEntry> diagnostics)
    {
        DecodedValue vendor = new("notProvided", null);
        var leaf0 = raw.Find(t => t.Leaf == "00000000");
        if (leaf0 != default)
        {
            var bytes = new byte[12];
            WriteLowByteFirst(bytes, 0, leaf0.R.Ebx);
            WriteLowByteFirst(bytes, 4, leaf0.R.Edx);
            WriteLowByteFirst(bytes, 8, leaf0.R.Ecx);
            vendor = DecodeAscii(bytes);
            if (vendor.Status == "invalid")
            {
                diagnostics.Add(new DiagnosticEntry("cpuid.vendorInvalid", "warning", "collect", "cpuid",
                    "厂商叶解码含内部 00 或非 ASCII，判 invalid；原寄存器已保留"));
            }
        }

        DecodedValue brand = new("notProvided", null);
        if (brandLeavesPresent)
        {
            var bytes = new byte[48];
            int offset = 0;
            foreach (var leaf in new[] { "80000002", "80000003", "80000004" })
            {
                var t = raw.Find(x => x.Leaf == leaf);
                WriteLowByteFirst(bytes, offset, t.R.Eax);
                WriteLowByteFirst(bytes, offset + 4, t.R.Ebx);
                WriteLowByteFirst(bytes, offset + 8, t.R.Ecx);
                WriteLowByteFirst(bytes, offset + 12, t.R.Edx);
                offset += 16;
            }
            brand = DecodeAscii(bytes);
            if (brand.Status == "invalid")
            {
                diagnostics.Add(new DiagnosticEntry("cpuid.brandInvalid", "warning", "collect", "cpuid",
                    "品牌串解码含内部 00 或非 ASCII，判 invalid；原寄存器已保留"));
            }
            else if (brand.Status == "notProvided")
            {
                diagnostics.Add(new DiagnosticEntry("cpuid.brandNotProvided", "info", "collect", "cpuid",
                    "品牌串为空或全 ASCII 空白，判 notProvided（来源仍可 captured）"));
            }
        }

        return new CpuidDecoded(DecodeRuleId, vendor, brand);
    }

    // 功能：按 v1 收尾规则把字节串判为 present/notProvided/invalid
    // 背景：删末尾连续 00 后再查内部 00；非 ASCII 一律 invalid，不做编码猜测
    // 方法：先裁尾 00 → 空则 notProvided → 逐字节校验 → 全空白则 notProvided → 否则 present（值原样保留，不做规范化）
    private static DecodedValue DecodeAscii(byte[] bytes)
    {
        int length = bytes.Length;
        while (length > 0 && bytes[length - 1] == 0x00) length--;
        if (length == 0) return new DecodedValue("notProvided", null);

        for (int i = 0; i < length; i++)
        {
            if (bytes[i] == 0x00 || bytes[i] > 0x7F) return new DecodedValue("invalid", null);
        }

        string value = Encoding.ASCII.GetString(bytes, 0, length);
        bool allWhitespace = true;
        foreach (char c in value)
        {
            if (!IsAsciiWhitespace(c)) { allWhitespace = false; break; }
        }
        return allWhitespace ? new DecodedValue("notProvided", null) : new DecodedValue("present", value);
    }

    // 功能：ASCII 空白判定，与 30 §11 规范化空白集合一致
    // 背景：仅 U+0009—U+000D 与 U+0020
    // 方法：显式枚举，不用 char.IsWhiteSpace（含非 ASCII 空白）
    private static bool IsAsciiWhitespace(char c)
        => c is '\u0009' or '\u000A' or '\u000B' or '\u000C' or '\u000D' or '\u0020';

    // 功能：寄存器按低字节先出写入字节数组
    // 背景：v1 规则的字节序；x86 寄存器小端语义
    // 方法：b0=bit0-7 … b3=bit24-31，强转 byte 不丢位
    private static void WriteLowByteFirst(byte[] bytes, int offset, int value)
    {
        bytes[offset] = (byte)(value & 0xFF);
        bytes[offset + 1] = (byte)((value >> 8) & 0xFF);
        bytes[offset + 2] = (byte)((value >> 16) & 0xFF);
        bytes[offset + 3] = (byte)((value >> 24) & 0xFF);
    }

    // 功能：来源失败时的解码占位
    // 背景：§12 unavailable 表示上游失败导致不可用；不伪造 present/invalid
    // 方法：vendor/brand 均 unavailable、值为 null，规则 id 照常给出
    private static CpuidDecoded UnavailableDecoded()
        => new(DecodeRuleId, new DecodedValue("unavailable", null), new DecodedValue("unavailable", null));

    // 功能：按叶/子叶采一条原始寄存器
    // 背景：subleaf 固定 0；leaf 已格式化为 8 位小写 hex 处在此统一
    // 方法：直接调 X86Base.CpuId，保留整数值供解码与原证据共用
    private static void Add(List<(string Leaf, (int Eax, int Ebx, int Ecx, int Edx) R)> raw, int leaf, int subleaf)
        => raw.Add((Hex(leaf), X86Base.CpuId(leaf, subleaf)));

    // 功能：把 CpuId 返回值组装为六键记录
    // 背景：int 寄存器按 uint 保 32 位位模式
    // 方法：四个寄存器各自转 8 位小写 hex
    private static RegisterEntry Entry((int Eax, int Ebx, int Ecx, int Edx) r, string leaf, string subleaf)
        => new(leaf, subleaf, Hex(r.Eax), Hex(r.Ebx), Hex(r.Ecx), Hex(r.Edx));

    // 功能：32 位值转 8 位小写十六进制字符串
    // 背景：契约要求不带 0x、小写、保位模式
    // 方法：强转 uint 后 x8 格式化，负值（高位为 1）不丢位
    private static string Hex(int value) => ((uint)value).ToString("x8");
}
