using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using ContractCaller;

if (args.Length != 4)
{
    throw new ArgumentException("用法：Caller.Tests <hardware-evidence根> <java.exe> <真实class根> <故障class根>");
}

string module = Path.GetFullPath(args[0]);
string java = args[1];
string classes = args[2];
string fixtureClasses = args[3];
string work = Path.Combine(module, "out", "result-tests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(work);
string identity = "0123456789abcdef0123456789abcdef";
byte[] sample = File.ReadAllBytes(Path.Combine(module, "contracts", "fixtures", "result-demo-success.json"));
var checks = new List<object>();

void Check(string name, bool condition, object? evidence = null)
{
    checks.Add(new { name, passed = condition, evidence });
    if (!condition)
    {
        throw new InvalidOperationException("FAIL: " + name);
    }
}

byte[] Mutate(Action<JsonNode> change)
{
    JsonNode node = JsonNode.Parse(sample)!;
    change(node);
    return Encoding.UTF8.GetBytes(node.ToJsonString());
}

void Reject(string name, byte[] bytes, int exitCode = 0)
{
    try
    {
        ResultResponseReader.Parse(bytes, identity, exitCode);
    }
    catch (Exception error) when (error is InvalidDataException or JsonException or DecoderFallbackException)
    {
        Check(name, true, error.GetType().Name + ": " + error.Message);
        return;
    }
    Check(name, false);
}

ResultResponse response = ResultResponseReader.Parse(sample, identity, 0);
Check("Shared success sample has artificial semantics", response.ExecutionStatus == "completed"
    && response.Fields.Length == 0 && response.Checks[0].Status == "insufficientEvidence");
byte[] failed = Mutate(node =>
{
    node["executionStatus"] = "failed";
    node["sources"]![0]!["status"] = "failed";
    node["checks"] = new JsonArray();
    node["diagnostics"]!.AsArray().Add(new JsonObject
    {
        ["code"] = "TEST_ERROR", ["severity"] = "error", ["stage"] = "demo", ["sourceId"] = null, ["message"] = "人工失败"
    });
});
Check("Valid controlled failure", ResultResponseReader.Parse(failed, identity, 2).ExecutionStatus == "failed");
Check("Harmless optional keys accepted", ResultResponseReader.Parse(Mutate(node => node["optional"] = "extension"), identity, 0).ExecutionId == identity);
foreach (string key in new[] { "schemaVersion", "engineVersion", "ruleSetVersion", "mode", "executionId", "captureId", "executionStatus", "sources", "fields", "checks", "diagnostics" })
{
    Reject("Missing required root key: " + key, Mutate(node => node.AsObject().Remove(key)));
}
foreach (string key in new[] { "sources", "fields", "checks", "diagnostics" })
{
    Reject("Null collection: " + key, Mutate(node => node[key] = null));
}
Reject("Wrong identity", Mutate(node => node["executionId"] = "ffffffffffffffffffffffffffffffff"));
Reject("Unsupported version", Mutate(node => node["schemaVersion"] = 2));
Reject("Version string", Mutate(node => node["schemaVersion"] = "1"));
Reject("Version boolean", Mutate(node => node["schemaVersion"] = true));
Reject("Version decimal", Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(sample).Replace("\"schemaVersion\": 1", "\"schemaVersion\": 1.0")));
Reject("Unknown mode", Mutate(node => node["mode"] = "collect"));
Reject("Wrong artificial capture marker", Mutate(node => node["captureId"] = identity));
Reject("Unknown rule set", Mutate(node => node["ruleSetVersion"] = "other"));
Reject("Empty engine version", Mutate(node => node["engineVersion"] = " "));
Reject("Unknown execution status", Mutate(node => node["executionStatus"] = "partial"));
Reject("Hardware source cannot replace synthetic", Mutate(node => node["sources"]![0]!["kind"] = "cpuid"));
Reject("Synthetic source cannot become required", Mutate(node => node["sources"]![0]!["required"] = true));
Reject("Required is strictly boolean", Mutate(node => node["sources"]![0]!["required"] = "false"));
Reject("Extra source", Mutate(node => node["sources"]!.AsArray().Add(node["sources"]![0]!.DeepClone())));
Reject("Wrong source status", Mutate(node => node["sources"]![0]!["status"] = "failed"));
Reject("Artificial source has no evidence paths", Mutate(node => node["sources"]![0]!["evidencePaths"] = new JsonArray("../outside")));
Reject("Artificial fields must be empty", Mutate(node => node["fields"]!.AsArray().Add(new JsonObject { ["fieldId"] = "fake" })));
Reject("Artificial result cannot assert hardware pass", Mutate(node => node["checks"]![0]!["status"] = "noConflictFound"));
Reject("Unknown rule", Mutate(node => node["checks"]![0]!["ruleId"] = "cpuNameCrossSource"));
Reject("No artificial rule on success", Mutate(node => node["checks"] = new JsonArray()));
Reject("Rule cannot cite fake fields", Mutate(node => node["checks"]![0]!["fieldIds"] = new JsonArray("fake")));
Reject("Rule reason cannot be null", Mutate(node => node["checks"]![0]!["reason"] = null));
Reject("Missing artificial diagnostic", Mutate(node => node["diagnostics"]![0]!["code"] = "OTHER"));
Reject("Unknown diagnostic severity", Mutate(node => node["diagnostics"]![0]!["severity"] = "success"));
Reject("Unknown diagnostic stage", Mutate(node => node["diagnostics"]![0]!["stage"] = "other"));
Reject("Dangling source reference", Mutate(node => node["diagnostics"]![0]!["sourceId"] = "smbios"));
Reject("Invalid artificial marker severity", Mutate(node => node["diagnostics"]![0]!["severity"] = "warning"));
Reject("Failed without error diagnostic", Mutate(node => { node["executionStatus"] = "failed"; node["sources"]![0]!["status"] = "failed"; }), 2);
Reject("Success response conflicts with exit 2", sample, 2);
Reject("Failure response conflicts with exit 0", failed);
Reject("Other exit code cannot carry accepted response", sample, 7);
Reject("BOM", new byte[] { 0xef, 0xbb, 0xbf }.Concat(sample).ToArray());
Reject("Invalid UTF-8", Encoding.ASCII.GetBytes("{\"bad\":\"").Concat(new byte[] { 0xff }).Concat(Encoding.ASCII.GetBytes("\"}")).ToArray());
Reject("Broken JSON", Encoding.UTF8.GetBytes("{broken"));
Reject("Root must be object", Encoding.UTF8.GetBytes("[]"));
Reject("Trailing JSON", sample.Concat(Encoding.UTF8.GetBytes("{}" )).ToArray());
Reject("Duplicate root key", Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(sample).Replace("\"schemaVersion\": 1,", "\"schemaVersion\": 1, \"schemaVersion\": 1,")));
Reject("Escaped duplicate key", Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(sample).Replace("\"schemaVersion\": 1,", "\"schemaVersion\": 1, \"schema\\u0056ersion\": 1,")));
Reject("Duplicate nested key", Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(sample).Replace("\"sourceId\": \"demo\", \"kind\"", "\"sourceId\": \"demo\", \"sourceId\": \"demo\", \"kind\"")));
Reject("Duplicate in ignored extension", Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(sample).Replace("\"schemaVersion\": 1,", "\"schemaVersion\": 1, \"extension\": {\"x\": 1, \"x\": 2},")));
Reject("Excessive depth", Encoding.UTF8.GetBytes(new string('[', 33) + "0" + new string(']', 33)));
Reject("String limit", Mutate(node => node["engineVersion"] = new string('x', 4097)));
Reject("Too many diagnostics", Mutate(node => { for (int i = 0; i < 128; i++) node["diagnostics"]!.AsArray().Add(node["diagnostics"]![0]!.DeepClone()); }));

string fileTests = Path.Combine(work, "files");
Directory.CreateDirectory(fileTests);
async Task RejectFile(string name)
{
    try
    {
        await ResultResponseReader.ReadAsync(fileTests, identity, 0);
    }
    catch (Exception error) when (error is IOException or InvalidDataException or JsonException or DecoderFallbackException)
    {
        Check(name, true, error.Message);
        return;
    }
    Check(name, false);
}
await RejectFile("Missing final response");
File.WriteAllBytes(Path.Combine(fileTests, "result.tmp"), sample);
await RejectFile("Temporary file is never accepted");
string finalPath = Path.Combine(fileTests, "result.json");
using (var oversized = File.Create(finalPath)) { oversized.SetLength(ResultResponseReader.MaxResultBytes + 1L); }
await RejectFile("File size rejected before allocating response buffer");
File.WriteAllBytes(finalPath, sample);
Check("Only current final file is read", (await ResultResponseReader.ReadAsync(fileTests, identity, 0)).ExecutionId == identity);

object Brief(CallerResult result) => new
{
    result.Status, result.JavaExitCode, result.ExecutionId, result.TaskDirectory, result.Error,
    stdoutLength = result.Stdout.Length, stderrLength = result.Stderr.Length,
    result.StdoutTruncated, result.StderrTruncated, accepted = result.Response is not null
};
var realArgs = new[] { java, classes, Path.Combine(work, "中文 runs 目录") };
CallerResult first = await JavaCaller.RunResultAsync(realArgs);
Check("Real Java and C# result interface", first.Status == CallerStatus.ResultReady && first.JavaExitCode == 0
    && first.Response?.ExecutionId == first.ExecutionId && first.Stderr.Length == 0 && first.Error is null, Brief(first));
Check("Final file committed and temporary absent", File.Exists(Path.Combine(first.TaskDirectory!, "result.json"))
    && !File.Exists(Path.Combine(first.TaskDirectory!, "result.tmp")));
CallerResult second = await JavaCaller.RunResultAsync(realArgs);
Check("Repeated real tasks stay isolated", second.Status == CallerStatus.ResultReady && first.ExecutionId != second.ExecutionId
    && first.TaskDirectory != second.TaskDirectory && File.Exists(Path.Combine(first.TaskDirectory!, "result.json")), Brief(second));
CallerResult oldDemo = await JavaCaller.RunAsync(realArgs);
Check("Original demo contract preserved", oldDemo.Status == CallerStatus.Validated && oldDemo.JavaExitCode == 0
    && oldDemo.Response is null && Directory.GetFileSystemEntries(oldDemo.TaskDirectory!).Length == 0
    && oldDemo.Stdout.StartsWith("REQUEST_VALIDATED"), Brief(oldDemo));

async Task<JsonDocument> RunCli(bool structured)
{
    using var process = new Process
    {
        StartInfo = new ProcessStartInfo(Path.Combine(module, "src", "main", "csharp", "bin", "Release", "net10.0", "Caller.exe"))
        {
            UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
        }
    };
    if (structured) process.StartInfo.ArgumentList.Add("--result-demo");
    foreach (string argument in realArgs) process.StartInfo.ArgumentList.Add(argument);
    process.Start();
    Task<string> stdout = process.StandardOutput.ReadToEndAsync();
    Task<string> stderr = process.StandardError.ReadToEndAsync();
    try { await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(15)); }
    catch { if (!process.HasExited) process.Kill(entireProcessTree: true); throw; }
    string text = await stdout;
    Check(structured ? "CLI structured entry and exit" : "CLI legacy entry and exit", process.ExitCode == 0 && await stderr == "");
    return JsonDocument.Parse(text[text.IndexOf('{')..]);
}
using (JsonDocument cli = await RunCli(true))
{
    Check("CLI exposes only validated structured response", cli.RootElement.GetProperty("status").GetString() == "result_ready"
        && cli.RootElement.GetProperty("response").GetProperty("mode").GetString() == "result-demo");
}
using (JsonDocument cli = await RunCli(false))
{
    Check("CLI legacy output remains seven fields", cli.RootElement.GetProperty("status").GetString() == "validated"
        && cli.RootElement.EnumerateObject().Count() == 7 && !cli.RootElement.TryGetProperty("response", out _));
}

using (var cancelled = new CancellationTokenSource())
{
    cancelled.Cancel();
    CallerResult result = await JavaCaller.RunResultAsync(realArgs, cancelled.Token);
    Check("Cancellation before directory or process creation", result.Status == CallerStatus.Cancelled
        && result.ExecutionId is null && result.JavaExitCode is null && result.Response is null, Brief(result));
}
foreach ((string scenario, CallerStatus expected, int exitCode) in new[]
{
    ("missing", CallerStatus.ProtocolFailure, 0), ("wrong-identity", CallerStatus.ProtocolFailure, 0),
    ("bad-json", CallerStatus.ProtocolFailure, 0), ("exit-mismatch", CallerStatus.ProtocolFailure, 2),
    ("failed", CallerStatus.RequestInvalid, 2), ("exit-other", CallerStatus.JavaFailure, 7),
    ("pressure", CallerStatus.ResultReady, 0)
})
{
    CallerResult result = await JavaCaller.RunResultAsync([java, fixtureClasses, Path.Combine(work, scenario)]);
    Check("Fault fixture: " + scenario, result.Status == expected && result.JavaExitCode == exitCode, Brief(result));
    if (scenario == "pressure")
    {
        Check("Both oversized pipes drained with bounded retention", result.StdoutTruncated && result.StderrTruncated
            && Encoding.UTF8.GetByteCount(result.Stdout) <= 1024 * 1024 + 2
            && Encoding.UTF8.GetByteCount(result.Stderr) <= 1024 * 1024 + 2
            && result.Stdout.Contains('中') && result.Stderr.Contains('中') && result.Response is not null);
    }
    if (expected is CallerStatus.ProtocolFailure or CallerStatus.JavaFailure)
    {
        Check("Rejected response never exposed: " + scenario, result.Response is null);
    }
}

bool ProcessGone(CallerResult result)
{
    string pidLine = result.Stdout.Split('\n').First(line => line.StartsWith("fixturePid="));
    int pid = int.Parse(pidLine["fixturePid=".Length..].Trim());
    try { using Process process = Process.GetProcessById(pid); return process.HasExited; }
    catch (ArgumentException) { return true; }
}
var watch = Stopwatch.StartNew();
CallerResult timedOut = await JavaCaller.RunResultAsync([java, fixtureClasses, Path.Combine(work, "timeout")]);
Check("Timeout rejects response and preserves directory", timedOut.Status == CallerStatus.Timeout
    && timedOut.JavaExitCode is null && timedOut.Response is null && Directory.Exists(timedOut.TaskDirectory), Brief(timedOut));
Check("Timeout cleanup bounded and owned process gone", watch.Elapsed < TimeSpan.FromSeconds(18) && ProcessGone(timedOut), watch.Elapsed.TotalSeconds);

string sentinelDirectory = Path.Combine(work, "sentinel", "run");
Directory.CreateDirectory(sentinelDirectory);
using var sentinel = new Process
{
    StartInfo = new ProcessStartInfo(java) { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true }
};
foreach (string argument in new[] { "-cp", fixtureClasses, "cn.bibapcs.evidence.cli.ResultCommand", "result-demo", "--execution-id", identity, "--out", sentinelDirectory })
    sentinel.StartInfo.ArgumentList.Add(argument);
sentinel.Start();
Task<string> sentinelStdout = sentinel.StandardOutput.ReadToEndAsync();
Task<string> sentinelStderr = sentinel.StandardError.ReadToEndAsync();
try
{
    using var cancellation = new CancellationTokenSource();
    string cancelRoot = Path.Combine(work, "cancel");
    Task<CallerResult> running = JavaCaller.RunResultAsync([java, fixtureClasses, cancelRoot], cancellation.Token);
    var readyDeadline = Stopwatch.StartNew();
    while (!Directory.Exists(cancelRoot) || !Directory.EnumerateFiles(cancelRoot, "result.json", SearchOption.AllDirectories).Any())
    {
        if (readyDeadline.Elapsed > TimeSpan.FromSeconds(5) || running.IsCompleted)
            throw new InvalidOperationException("故障进程未在取消前提交测试结果。");
        await Task.Delay(50);
    }
    cancellation.Cancel();
    CallerResult result = await running.WaitAsync(TimeSpan.FromSeconds(8));
    Check("Cancelled task rejects already complete JSON", result.Status == CallerStatus.Cancelled
        && result.JavaExitCode is null && result.Response is null && File.Exists(Path.Combine(result.TaskDirectory!, "result.json")), Brief(result));
    Check("Cancellation stops only owned process", ProcessGone(result) && !sentinel.HasExited);
}
finally
{
    if (!sentinel.HasExited) sentinel.Kill(entireProcessTree: true);
    await sentinel.WaitForExitAsync();
    await Task.WhenAll(sentinelStdout, sentinelStderr);
}

string report = Path.Combine(work, "verification.json");
File.WriteAllText(report, JsonSerializer.Serialize(new
{
    date = "2026-10-14", executor = "assistant", count = checks.Count, checks,
    limitations = new[] { "WPF host and publishing not integrated", "No permission-denied or reparse-point runtime case", "Failure/pressure/timeout/cancellation cases use explicitly separate Java fault fixtures" }
}, new JsonSerializerOptions { WriteIndented = true }));
Console.WriteLine($"PASS: {checks.Count} checks. Evidence: {report}");
