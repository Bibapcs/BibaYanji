using System.Text;
using System.Text.Json;

namespace ContractCaller;

public static class ResultResponseReader
{
    public const int MaxResultBytes = 16 * 1024 * 1024;
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);

    // 功能：只读取当前任务最终文件。背景：退出 0 也可能缺结果或携带坏响应。
    // 方法：同一只读句柄检查大小与读取，拒绝 BOM、坏编码、重复键、错误身份与人工语义。
    public static async Task<ResultResponse> ReadAsync(
        string taskDirectory, string executionId, int javaExitCode, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        string path = Path.Combine(taskDirectory, "result.json");
        RejectReparsePoint(taskDirectory);
        RejectReparsePoint(path);
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read,
            8192, FileOptions.Asynchronous | FileOptions.SequentialScan);
        if (stream.Length <= 0 || stream.Length > MaxResultBytes)
        {
            throw new InvalidDataException("result.json 为空或超过 16 MiB 上限。");
        }

        var bytes = new byte[(int)stream.Length];
        await stream.ReadExactlyAsync(bytes, cancellationToken);
        if (stream.ReadByte() != -1)
        {
            throw new InvalidDataException("result.json 读取期间长度发生变化。");
        }

        ResultResponse response = Parse(bytes, executionId, javaExitCode);
        cancellationToken.ThrowIfCancellationRequested();
        return response;
    }

    public static ResultResponse Parse(byte[] bytes, string executionId, int javaExitCode)
    {
        if (bytes.Length == 0 || bytes.Length > MaxResultBytes
            || bytes.AsSpan().StartsWith(new byte[] { 0xef, 0xbb, 0xbf }))
        {
            throw new InvalidDataException("结果须为不超过 16 MiB 的 UTF-8 无 BOM JSON。");
        }

        if (executionId.Length != 32 || executionId.Any(character => !IsLowerHex(character)))
        {
            throw new InvalidDataException("调用方任务身份无效。");
        }

        using JsonDocument document = JsonDocument.Parse(StrictUtf8.GetString(bytes),
            new JsonDocumentOptions { MaxDepth = 32 });
        JsonElement root = document.RootElement;
        CheckTree(root);
        Require(root.ValueKind == JsonValueKind.Object, "响应根必须是对象。");
        JsonElement schema = Property(root, "schemaVersion");
        Require(schema.ValueKind == JsonValueKind.Number && schema.TryGetInt32(out int version) && version == 1,
            "schemaVersion 必须是 JSON 整数 1。");
        string engine = Text(root, "engineVersion");
        Require(Text(root, "ruleSetVersion") == "demo-only-v1", "规则版本不支持。");
        Require(Text(root, "mode") == "result-demo", "响应模式不匹配。");
        Require(Text(root, "executionId") == executionId, "响应身份不是本次任务。");
        Require(Text(root, "captureId") == "demo-cpu-v1", "人工采集标记不匹配。");
        string status = Text(root, "executionStatus");
        Require((status == "completed" && javaExitCode == 0) || (status == "failed" && javaExitCode == 2),
            "执行状态与 Java 自然退出码不相容。");

        JsonElement sources = Array(root, "sources");
        Require(sources.GetArrayLength() == 1, "人工演示必须恰好包含一个来源。");
        JsonElement source = sources[0];
        Require(Text(source, "sourceId") == "demo" && Text(source, "kind") == "synthetic",
            "人工演示不能伪装硬件来源。");
        Require(Property(source, "required").ValueKind == JsonValueKind.False, "人工来源 required 必须为 false。");
        string sourceStatus = Text(source, "status");
        Require(sourceStatus == (status == "completed" ? "captured" : "failed"), "人工来源状态不相容。");
        EmptyArray(source, "evidencePaths");
        EmptyArray(root, "fields");

        JsonElement checks = Array(root, "checks");
        Require(checks.GetArrayLength() <= 1 && (status == "failed" || checks.GetArrayLength() == 1),
            "人工规则数量不符合契约。");
        var checkModels = new List<ResultCheck>();
        foreach (JsonElement check in checks.EnumerateArray())
        {
            Require(Text(check, "ruleId") == "demoOnly" && Text(check, "status") == "insufficientEvidence",
                "人工演示不能输出硬件通过结论。");
            EmptyArray(check, "fieldIds");
            checkModels.Add(new("demoOnly", "insufficientEvidence", [], Text(check, "reason")));
        }

        JsonElement diagnostics = Array(root, "diagnostics");
        Require(diagnostics.GetArrayLength() is >= 1 and <= 128, "诊断数量须为 1–128。");
        var diagnosticModels = new List<ResultDiagnostic>();
        foreach (JsonElement diagnostic in diagnostics.EnumerateArray())
        {
            string code = Text(diagnostic, "code");
            string severity = Text(diagnostic, "severity");
            string stage = Text(diagnostic, "stage");
            Require(severity is "info" or "warning" or "error", "未知诊断级别。");
            Require(stage is "request" or "collect" or "parse" or "check" or "output" or "demo", "未知诊断阶段。");
            JsonElement sourceId = Property(diagnostic, "sourceId");
            Require(sourceId.ValueKind == JsonValueKind.Null
                || (sourceId.ValueKind == JsonValueKind.String && sourceId.GetString() == "demo"), "诊断引用来源不存在。");
            diagnosticModels.Add(new(code, severity, stage, sourceId.GetString(), Text(diagnostic, "message")));
        }

        Require(diagnosticModels.Any(item => item.Code == "ARTIFICIAL_DEMO" && item.Severity == "info"
            && item.Stage == "demo" && item.SourceId == "demo"), "缺少有效的 ARTIFICIAL_DEMO 标记。");
        Require(status != "failed" || diagnosticModels.Any(item => item.Severity == "error"), "失败响应缺少 error 诊断。");
        return new(1, engine, "demo-only-v1", "result-demo", executionId, "demo-cpu-v1", status,
            [new("demo", "synthetic", false, sourceStatus, [])], [], checkModels.ToArray(), diagnosticModels.ToArray());
    }

    private static void CheckTree(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var keys = new HashSet<string>(StringComparer.Ordinal);
            foreach (JsonProperty property in element.EnumerateObject())
            {
                Require(keys.Add(property.Name), $"响应包含重复键：{property.Name}");
                Require(property.Name.Length <= 4096, "JSON 键名过长。");
                CheckTree(property.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            foreach (JsonElement item in element.EnumerateArray())
            {
                CheckTree(item);
            }
        }
        else if (element.ValueKind == JsonValueKind.String)
        {
            Require(element.GetString()!.Length <= 4096, "JSON 字符串过长。");
        }
    }

    private static JsonElement Property(JsonElement parent, string name)
    {
        Require(parent.ValueKind == JsonValueKind.Object && parent.TryGetProperty(name, out _), $"缺少对象属性：{name}");
        return parent.GetProperty(name);
    }

    private static string Text(JsonElement parent, string name)
    {
        JsonElement property = Property(parent, name);
        Require(property.ValueKind == JsonValueKind.String, $"{name} 必须是字符串。");
        string value = property.GetString()!;
        Require(!string.IsNullOrWhiteSpace(value), $"{name} 不能为空。");
        return value;
    }

    private static JsonElement Array(JsonElement parent, string name)
    {
        JsonElement value = Property(parent, name);
        Require(value.ValueKind == JsonValueKind.Array, $"{name} 必须是非 null 数组。");
        return value;
    }

    private static void EmptyArray(JsonElement parent, string name) =>
        Require(Array(parent, name).GetArrayLength() == 0, $"人工演示的 {name} 必须为空数组。");

    private static void RejectReparsePoint(string path) =>
        Require((File.GetAttributes(path) & FileAttributes.ReparsePoint) == 0, "任务目录及结果文件不能是重解析点。");

    private static bool IsLowerHex(char character) => character is >= '0' and <= '9' or >= 'a' and <= 'f';

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidDataException(message);
        }
    }
}
