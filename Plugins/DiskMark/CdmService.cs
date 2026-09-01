using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;

namespace YanJi.Plugin.DiskMark;

/// <summary>CrystalDiskMark 硬盘基准封装（源码集成：CdmHost/CdmHost.exe 是用 CDM 9.0.3 源码
/// 原封不动编译的控制台 host（MIT），引擎 DiskBench 驱动定制 diskspd64.exe 子进程测速）。
/// 用法：`CdmHost.exe [盘符]`，只跑 SEQ1M Q8T1 顺序读/写 + RND4K Q1T1 随机读/写四项
/// （1 GiB 测试文件、随机数据、5 轮+1 预热、测 5s 间隔 5s，全部写死在 host 里），
/// stdout 最后一行输出单行 JSON 成绩 `{"seq_read":…,"seq_write":…,"rnd4k_read":…,"rnd4k_write":…}`（MB/s）。
/// stdout 噪音过滤：diskspd 子进程把几千行结果表格灌进同一 stdout——只有整行 `{...}` 是成绩，
/// 只有 `...` 结尾的行转发为进度，其余只进日志缓冲。兜底 15 分钟杀进程树。
/// 移植自 BibaAutoTest 同名服务，改动：host 定位到本插件目录、结果目录改 %APPDATA%、
/// 追加成绩记录 CSV。</summary>
public class CdmService
{
    /// <summary>一次跑分的结果（四项 MB/s + JSON 落盘路径）。</summary>
    public record CdmResult(double SeqRead, double SeqWrite, double Rnd4KRead, double Rnd4KWrite, string JsonPath);

    /// <summary>插件目录（plugins/disk/）。</summary>
    public static string PluginDir =>
        Path.GetDirectoryName(System.Reflection.Assembly.GetExecutingAssembly().Location)!;

    /// <summary>CdmHost.exe 路径：插件目录下的 CdmHost 负载。</summary>
    public static string HostExe => Path.Combine(PluginDir, "CdmHost", "CdmHost.exe");

    public static bool HostAvailable => File.Exists(HostExe);

    /// <summary>成绩输出目录（%APPDATA%\YanJi\disk_result；不写插件目录——可能在 Program Files 下）。</summary>
    public static string ResultDir => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "YanJi", "disk_result");

    /// <summary>成绩 JSON 落盘路径（每次跑完覆盖，与 BibaAutoTest 的 硬盘基准.json 同格式）。</summary>
    public static string JsonPath => Path.Combine(ResultDir, "硬盘基准.json");

    /// <summary>成绩记录 CSV（每次跑完追加一行；文件首行是表头）。</summary>
    public static string ScoresCsvPath => Path.Combine(ResultDir, "硬盘成绩记录.csv");

    /// <summary>当前跑分进程（取消/关程序兜底 Kill 用）。</summary>
    static Process? _current;

    /// <summary>盘符参数归一化：接受 C / C: / C:\，返回单个大写字母；非法则抛 ArgumentException。</summary>
    public static string NormalizeDrive(string drive)
    {
        string d = drive.Trim().TrimEnd(':', '\\', '/').Trim().ToUpperInvariant();
        if (d.Length != 1 || d[0] < 'A' || d[0] > 'Z')
            throw new ArgumentException("无效盘符：" + drive);
        return d;
    }

    /// <summary>对指定盘跑一次四项基准并返回成绩（JSON 同时落盘 JsonPath、追加 ScoresCsvPath）。
    /// 取消 = 杀整棵进程树（连带 diskspd）；兜底 15 分钟超时同样杀树。</summary>
    public async Task<CdmResult> RunAsync(string drive, IProgress<string>? progress, CancellationToken ct)
    {
        string exe = HostExe;
        if (!File.Exists(exe))
            throw new FileNotFoundException("未找到 CdmHost 负载（应随插件一同分发）：" + exe);
        drive = NormalizeDrive(drive);

        var psi = new ProcessStartInfo
        {
            FileName = exe,
            Arguments = drive,
            WorkingDirectory = Path.GetDirectoryName(exe)!, // 引擎按相对路径找 CdmResource\diskspd\diskspd64.exe
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8,
        };
        using var proc = new Process { StartInfo = psi, EnableRaisingEvents = true };
        var sb = new StringBuilder();
        string? jsonLine = null;
        proc.OutputDataReceived += (_, e) =>
        {
            if (e.Data == null) return;
            sb.AppendLine(e.Data);
            var line = e.Data.Trim();
            if (line.StartsWith("{") && line.EndsWith("}")) jsonLine = line;
            // 只转发 host 自己的进度行（diskspd 子进程的海量表格输出会刷爆 UI）
            else if (line.EndsWith("...")) progress?.Report(line);
        };
        proc.ErrorDataReceived += (_, e) => { if (e.Data != null) sb.AppendLine(e.Data); };

        proc.Start();
        _current = proc;
        try
        {
            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();
            using var timeoutCts = CancellationTokenSource.CreateLinkedTokenSource(ct);
            timeoutCts.CancelAfter(TimeSpan.FromMinutes(15)); // 兜底超时（2 项 × 6 轮 × 5s + 建 1 GiB 文件，正常 ~2 分钟）
            try
            {
                await using (timeoutCts.Token.Register(() => { try { proc.Kill(true); } catch { /* 已退出 */ } }))
                {
                    await proc.WaitForExitAsync(timeoutCts.Token).ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException) when (!ct.IsCancellationRequested)
            {
                throw new TimeoutException("硬盘基准超时（15 分钟未完成）");
            }
        }
        finally
        {
            _current = null;
        }

        if (jsonLine == null)
            throw new InvalidOperationException($"CdmHost 未输出成绩（退出码 {proc.ExitCode}）：\n{Tail(sb.ToString(), 1200)}");

        Directory.CreateDirectory(ResultDir);
        File.WriteAllText(JsonPath, jsonLine + "\n", new UTF8Encoding(false));
        var result = ParseScores(jsonLine, JsonPath);
        AppendScore(drive + ":", result);
        return result;
    }

    /// <summary>解析成绩 JSON（`{"seq_read":…,"seq_write":…,"rnd4k_read":…,"rnd4k_write":…}`，MB/s）。</summary>
    public static CdmResult ParseScores(string json, string jsonPath)
    {
        using var doc = JsonDocument.Parse(json);
        var r = doc.RootElement;
        return new CdmResult(
            r.GetProperty("seq_read").GetDouble(),
            r.GetProperty("seq_write").GetDouble(),
            r.GetProperty("rnd4k_read").GetDouble(),
            r.GetProperty("rnd4k_write").GetDouble(),
            jsonPath);
    }

    /// <summary>追加一行成绩记录（UTF-8 带 BOM；落盘失败不影响本次成绩）。</summary>
    public static void AppendScore(string drive, CdmResult res)
    {
        try
        {
            bool newFile = !File.Exists(ScoresCsvPath);
            using var w = new StreamWriter(ScoresCsvPath, append: true,
                new UTF8Encoding(encoderShouldEmitUTF8Identifier: true));
            if (newFile) w.WriteLine("时间,盘符,顺序读取(MB/s),顺序写入(MB/s),随机读取4K(MB/s),随机写入4K(MB/s)");
            w.WriteLine(string.Join(',',
                DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"),
                drive,
                res.SeqRead.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture),
                res.SeqWrite.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture),
                res.Rnd4KRead.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture),
                res.Rnd4KWrite.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture)));
        }
        catch { /* 成绩落盘失败不影响本次成绩 */ }
    }

    static string Tail(string s, int n) => s.Length <= n ? s : s[^n..];

    /// <summary>兜底杀掉当前跑分进程树（页面切走/程序关闭时调用；无进程则空操作）。</summary>
    public static void KillCurrent()
    {
        try { _current?.Kill(entireProcessTree: true); } catch { /* 已退出 */ }
    }
}
