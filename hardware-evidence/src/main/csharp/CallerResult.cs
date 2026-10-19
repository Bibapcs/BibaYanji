using System.Text.Json.Serialization;

namespace ContractCaller;

// 功能：保存调用方分类。背景：Java 的退出码与 Caller 自己的退出码含义不同。
// 方法：保持分类、原始退出码、任务上下文和两路日志各自独立。
public enum CallerStatus
{
    Validated,
    RequestInvalid,
    JavaFailure,
    CallerFailure,
    Timeout,
    ResultReady,
    ProtocolFailure,
    Cancelled
}

public sealed class CallerResult
{
    public string? ExecutionId { get; internal set; }
    public string? TaskDirectory { get; internal set; }
    public CallerStatus Status { get; internal set; } = CallerStatus.CallerFailure;
    public int? JavaExitCode { get; internal set; }
    public string Stdout { get; internal set; } = string.Empty;
    public string Stderr { get; internal set; } = string.Empty;
    public string? Error { get; internal set; }

    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public ResultResponse? Response { get; internal set; }

    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public bool StdoutTruncated { get; internal set; }

    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public bool StderrTruncated { get; internal set; }

    [JsonIgnore]
    public int CallerExitCode => Status switch
    {
        CallerStatus.Validated or CallerStatus.ResultReady => 0,
        CallerStatus.RequestInvalid => 2,
        _ => 1
    };

    internal void AddError(string message) =>
        Error = Error is null ? message : Error + Environment.NewLine + message;

    // 超时/取消后的停止或日志错误保留原分类；自然结束后的日志错误属于 caller_failure。
    internal void AddCallerFailure(string message)
    {
        if (Status is not (CallerStatus.Timeout or CallerStatus.Cancelled))
        {
            Status = CallerStatus.CallerFailure;
        }

        AddError(message);
    }
}
