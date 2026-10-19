using System.Text.Json;

namespace ContractCaller;

// 功能：保存通过 result-demo-v1 校验的响应。背景：人工结果不等于硬件结论。
// 方法：仅接收契约定义的来源、规则与诊断；当前 Fields 必须为空。
public sealed record ResultResponse(
    int SchemaVersion, string EngineVersion, string RuleSetVersion, string Mode,
    string ExecutionId, string CaptureId, string ExecutionStatus,
    ResultSource[] Sources, JsonElement[] Fields, ResultCheck[] Checks, ResultDiagnostic[] Diagnostics);

public sealed record ResultSource(string SourceId, string Kind, bool Required, string Status, string[] EvidencePaths);
public sealed record ResultCheck(string RuleId, string Status, string[] FieldIds, string Reason);
public sealed record ResultDiagnostic(string Code, string Severity, string Stage, string? SourceId, string Message);
