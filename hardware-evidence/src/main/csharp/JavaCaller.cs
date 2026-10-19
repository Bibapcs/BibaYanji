using System.Diagnostics;
using System.Text;

namespace ContractCaller;

public static class JavaCaller
{
    private static readonly TimeSpan NaturalExitLimit = TimeSpan.FromSeconds(10);
    private static readonly TimeSpan CleanupLimit = TimeSpan.FromSeconds(3);
    private const int MaxLogBytes = 1024 * 1024;
    private sealed record CapturedLog(string Text, bool Truncated);

    // 功能：准备任务、启动 Java、采集日志并分类。背景：方法返回值不能代替 OS 进程退出码。
    // 方法：逐项传参，在等待前启动双管道异步读取；自然退出才保存 ExitCode。
    public static Task<CallerResult> RunAsync(string[] args) => RunCoreAsync(args, false, CancellationToken.None);

    public static Task<CallerResult> RunResultAsync(string[] args, CancellationToken cancellationToken = default) =>
        RunCoreAsync(args, true, cancellationToken);

    private static async Task<CallerResult> RunCoreAsync(string[] args, bool resultDemo, CancellationToken cancellationToken)
    {
        var result = new CallerResult();
        CallerConfiguration configuration;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            configuration = CallerConfiguration.FromArguments(args, resultDemo);
            (result.ExecutionId, result.TaskDirectory) = configuration.CreateTaskDirectory(exclusive: resultDemo);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            MarkCancelled(result);
            return result;
        }
        catch (Exception exception)
        {
            result.AddCallerFailure(Describe("配置或任务目录准备失败", exception));
            return result;
        }

        using var process = new Process();
        using var readCancellation = new CancellationTokenSource();
        Task<CapturedLog>? stdoutTask = null;
        Task<CapturedLog>? stderrTask = null;
        bool started = false;
        bool naturalExitObserved = false;

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            process.StartInfo = CreateStartInfo(configuration, result.ExecutionId!, result.TaskDirectory!, resultDemo);
            if (!process.Start())
            {
                throw new InvalidOperationException("Process.Start 未成功创建本次子进程。");
            }

            started = true;
            var elapsed = Stopwatch.StartNew();
            stdoutTask = ReadLogAsync(process.StandardOutput, resultDemo, readCancellation.Token);
            ObserveLateFault(stdoutTask);
            stderrTask = ReadLogAsync(process.StandardError, resultDemo, readCancellation.Token);
            ObserveLateFault(stderrTask);

            // 从启动后计时；读取的启动开销也计入十秒自然结束期限。
            TimeSpan remaining = NaturalExitLimit - elapsed.Elapsed;
            using var exitCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            if (remaining <= TimeSpan.Zero)
            {
                exitCancellation.Cancel();
            }
            else
            {
                exitCancellation.CancelAfter(remaining);
            }

            try
            {
                await process.WaitForExitAsync(exitCancellation.Token);
                naturalExitObserved = true;
                result.JavaExitCode = process.ExitCode;
                result.Status = result.JavaExitCode switch
                {
                    0 => CallerStatus.Validated,
                    2 => CallerStatus.RequestInvalid,
                    _ => CallerStatus.JavaFailure
                };
            }
            catch (OperationCanceledException) when (exitCancellation.IsCancellationRequested)
            {
                if (cancellationToken.IsCancellationRequested)
                {
                    MarkCancelled(result);
                }
                else
                {
                    result.Status = CallerStatus.Timeout;
                    result.JavaExitCode = null;
                    result.AddError("Java 未在启动后的 10 秒期限内自然结束；超时收尾退出码不作为 Java 自然退出码。");
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            MarkCancelled(result);
        }
        catch (Exception exception)
        {
            result.AddCallerFailure(Describe(started ? "启动后读取或等待失败" : "启动失败", exception));
        }

        if (started && !naturalExitObserved)
        {
            await StopOwnedProcessAsync(process, result);
        }

        await CollectLogsAsync(process, stdoutTask, stderrTask, readCancellation, result);
        if (resultDemo && result.Status != CallerStatus.Timeout)
        {
            if (cancellationToken.IsCancellationRequested)
            {
                MarkCancelled(result);
            }
            else if (result.Status is CallerStatus.Validated or CallerStatus.RequestInvalid)
            {
                await ReceiveResultAsync(result, cancellationToken);
            }
        }
        return result;
    }

    private static ProcessStartInfo CreateStartInfo(
        CallerConfiguration configuration, string executionId, string taskDirectory, bool resultDemo)
    {
        var info = new ProcessStartInfo
        {
            FileName = configuration.JavaExecutable,
            WorkingDirectory = taskDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false),
            StandardErrorEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false)
        };

        info.ArgumentList.Add("-cp");
        info.ArgumentList.Add(resultDemo
            ? configuration.ClassDirectory + Path.PathSeparator + Path.Combine(configuration.ClassDirectory, "lib", "*")
            : configuration.ClassDirectory);
        info.ArgumentList.Add(resultDemo ? "cn.bibapcs.evidence.cli.ResultCommand" : "DemoCommand");
        info.ArgumentList.Add(resultDemo ? "result-demo" : "demo");
        info.ArgumentList.Add("--execution-id");
        info.ArgumentList.Add(executionId);
        info.ArgumentList.Add("--out");
        info.ArgumentList.Add(taskDirectory);
        return info;
    }

    // 功能：停止本次拥有的子进程。背景：超时并不保证停止成功，不能按进程名批量杀进程。
    // 方法：仅使用本次 Process，记录 Kill 和有界确认等待的实际结果，不重试。
    private static async Task StopOwnedProcessAsync(Process process, CallerResult result)
    {
        try
        {
            if (process.HasExited)
            {
                result.AddError("收尾时观察到本次进程已结束，未请求 Kill；原分类保留。");
                return;
            }

            process.Kill(entireProcessTree: true);
        }
        catch (Exception exception)
        {
            result.AddCallerFailure(Describe("请求停止本次进程失败；不能确认其已停止", exception));
        }

        using var stopCancellation = new CancellationTokenSource(CleanupLimit);
        try
        {
            await process.WaitForExitAsync(stopCancellation.Token);
            result.AddError("收尾等待已确认本次进程退出；未验证所有后代进程的退出状态。");
        }
        catch (OperationCanceledException) when (stopCancellation.IsCancellationRequested)
        {
            result.AddCallerFailure("停止后的 3 秒确认等待超时；不能确认本次进程已停止。");
        }
        catch (Exception exception)
        {
            result.AddCallerFailure(Describe("停止后的确认等待失败", exception));
        }
    }

    // 功能：有界收齐两路日志。背景：进程退出后，继承管道的后代仍可能阻止 EOF。
    // 方法：两路共同最多等待三秒；超限取消并关闭读端，保留已完整取得的一路。
    private static async Task CollectLogsAsync(
        Process process,
        Task<CapturedLog>? stdoutTask,
        Task<CapturedLog>? stderrTask,
        CancellationTokenSource readCancellation,
        CallerResult result)
    {
        var tasks = new List<Task<CapturedLog>>(2);
        if (stdoutTask is not null)
        {
            tasks.Add(stdoutTask);
        }

        if (stderrTask is not null)
        {
            tasks.Add(stderrTask);
        }

        if (tasks.Count == 0)
        {
            return;
        }

        Task<CapturedLog[]> allLogs = Task.WhenAll(tasks);
        ObserveLateFault(allLogs);
        try
        {
            await allLogs.WaitAsync(CleanupLimit);
        }
        catch (TimeoutException)
        {
            result.AddCallerFailure("两路日志收尾超过 3 秒；未完成一路返回空文本，日志可能不完整。");
            try
            {
                readCancellation.Cancel();
            }
            catch (Exception exception)
            {
                result.AddCallerFailure(Describe("取消日志读取失败", exception));
            }

            CloseReader(process, isStdout: true, result);
            CloseReader(process, isStdout: false, result);
        }
        catch (Exception exception)
        {
            result.AddCallerFailure(Describe("日志读取失败，日志可能不完整", exception));
        }

        CapturedLog stdout = CompletedLog(stdoutTask, "stdout", result);
        CapturedLog stderr = CompletedLog(stderrTask, "stderr", result);
        result.Stdout = stdout.Text;
        result.Stderr = stderr.Text;
        result.StdoutTruncated = stdout.Truncated;
        result.StderrTruncated = stderr.Truncated;
    }

    private static CapturedLog CompletedLog(Task<CapturedLog>? task, string label, CallerResult result)
    {
        if (task is null)
        {
            return new(string.Empty, false);
        }

        if (task.IsCompletedSuccessfully)
        {
            return task.GetAwaiter().GetResult();
        }

        if (task.IsFaulted)
        {
            result.AddCallerFailure(Describe($"{label} 读取任务异常", task.Exception!.Flatten()));
        }
        else if (task.IsCanceled)
        {
            result.AddCallerFailure($"{label} 读取已取消；文本不完整，返回空文本。");
        }
        else
        {
            result.AddCallerFailure($"{label} 读取尚未结束；返回空文本，后续异常由观察器消费。");
        }

        return new(string.Empty, false);
    }

    // 功能：限制保存量但持续消费管道。背景：截断前无限读取会消耗无界内存。
    // 方法：新接口按原始字节保留每路 1 MiB，原演示保留其原有限日志行为。
    private static async Task<CapturedLog> ReadLogAsync(StreamReader reader, bool bounded, CancellationToken token)
    {
        if (!bounded)
        {
            return new(await reader.ReadToEndAsync(token), false);
        }

        using var saved = new MemoryStream();
        var buffer = new byte[8192];
        bool truncated = false;
        int count;
        while ((count = await reader.BaseStream.ReadAsync(buffer, token)) != 0)
        {
            int keep = Math.Min(count, MaxLogBytes - (int)saved.Length);
            saved.Write(buffer, 0, keep);
            truncated |= keep != count;
        }

        return new(Encoding.UTF8.GetString(saved.GetBuffer(), 0, (int)saved.Length), truncated);
    }

    private static async Task ReceiveResultAsync(CallerResult result, CancellationToken token)
    {
        try
        {
            token.ThrowIfCancellationRequested();
            if (result.Status == CallerStatus.RequestInvalid
                && !File.Exists(Path.Combine(result.TaskDirectory!, "result.json")))
            {
                return;
            }

            ResultResponse response = await ResultResponseReader.ReadAsync(
                result.TaskDirectory!, result.ExecutionId!, result.JavaExitCode!.Value, token);
            token.ThrowIfCancellationRequested();
            result.Response = response;
            result.Status = response.ExecutionStatus == "completed" ? CallerStatus.ResultReady : CallerStatus.RequestInvalid;
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested)
        {
            MarkCancelled(result);
        }
        catch (Exception exception)
        {
            result.Response = null;
            result.Status = CallerStatus.ProtocolFailure;
            result.AddError(Describe("结果校验失败", exception));
        }
    }

    private static void MarkCancelled(CallerResult result)
    {
        bool alreadyCancelled = result.Status == CallerStatus.Cancelled;
        result.Status = CallerStatus.Cancelled;
        result.JavaExitCode = null;
        result.Response = null;
        if (!alreadyCancelled)
        {
            result.AddError("本次任务已取消；不接收结果，收尾退出码不作为自然退出码。");
        }
    }

    private static void CloseReader(Process process, bool isStdout, CallerResult result)
    {
        try
        {
            if (isStdout)
            {
                process.StandardOutput.Dispose();
            }
            else
            {
                process.StandardError.Dispose();
            }
        }
        catch (Exception exception)
        {
            result.AddCallerFailure(Describe($"关闭 {(isStdout ? "stdout" : "stderr")} 读端失败", exception));
        }
    }

    // 有界等待结束后仍未完成的任务也注册故障观察，不用无限 await 把超时变成挂起。
    private static void ObserveLateFault(Task task)
    {
        _ = task.ContinueWith(
            faulted => { _ = faulted.Exception; },
            CancellationToken.None,
            TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private static string Describe(string action, Exception exception) =>
        $"{action}：{exception.GetType().Name}: {exception.Message}";
}
