using System.IO;
using System.IO.Compression;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace YanJi.Services;

/// <summary>插件包（zip）导入与删除。插件目录 = exe 同级 plugins/&lt;id&gt;/。
/// 生效时机：新插件导入后宿主帅即加载（无需重启）；删除/覆盖安装已加载的插件时，
/// 其 dll 被默认加载上下文锁定（运行时不卸载程序集），走「待处理操作」——写入
/// %APPDATA%\YanJi\plugin-ops.json，自动重启后在插件加载前统一执行（此时无文件锁）。</summary>
public static class PluginPackageService
{
    /// <summary>插件 id 合法性（同时是目录名）：小写字母/数字/短横线/下划线，防路径穿越。</summary>
    public static bool IsValidId(string id) => Regex.IsMatch(id, @"^[a-z0-9_-]{1,32}$");

    // ═══ 待处理操作（删除/覆盖已加载插件时排队，下次启动在加载前执行） ═══

    public record PendingOp(string Action, string Id, string? ZipPath);

    static string OpsFile => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "YanJi", "plugin-ops.json");

    /// <summary>排队一个待处理操作（delete / import）。</summary>
    public static void QueuePendingOp(string action, string id, string? zipPath = null)
    {
        var ops = ReadOps();
        ops.RemoveAll(o => o.Action == action && o.Id == id); // 同 id 同动作去重（后排队者覆盖）
        ops.Add(new PendingOp(action, id, zipPath));
        WriteOps(ops);
    }

    /// <summary>启动时（插件加载前）执行全部待处理操作，然后清空队列。</summary>
    public static void ProcessPendingOps()
    {
        var ops = ReadOps();
        if (ops.Count == 0) return;
        foreach (var op in ops)
        {
            try
            {
                if (op.Action == "delete")
                    Delete(op.Id);
                else if (op.Action == "import" && op.ZipPath != null && File.Exists(op.ZipPath))
                    ImportZip(op.ZipPath);
            }
            catch (Exception ex)
            {
                PluginLoader.Log($"待处理插件操作失败（{op.Action} {op.Id}）：{ex.Message}");
            }
            finally
            {
                // 导入用的暂存 zip 用完即删
                if (op.Action == "import" && op.ZipPath != null)
                    try { File.Delete(op.ZipPath); } catch { }
            }
        }
        try { File.Delete(OpsFile); } catch { /* 下次启动重试 */ }
    }

    /// <summary>覆盖导入已加载插件用的暂存：把 zip 拷到 %APPDATA%\YanJi\pending-imports\，返回暂存路径。</summary>
    public static string StageZipForPendingImport(string id, string zipPath)
    {
        string dir = Path.Combine(Path.GetDirectoryName(OpsFile)!, "pending-imports");
        Directory.CreateDirectory(dir);
        string staged = Path.Combine(dir, id + ".zip");
        File.Copy(zipPath, staged, overwrite: true);
        return staged;
    }

    static List<PendingOp> ReadOps()
    {
        try
        {
            if (File.Exists(OpsFile))
                return JsonSerializer.Deserialize<List<PendingOp>>(File.ReadAllText(OpsFile), PluginLoader.JsonOptions) ?? [];
        }
        catch { /* 损坏则视为空 */ }
        return [];
    }

    static void WriteOps(List<PendingOp> ops)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(OpsFile)!);
        File.WriteAllText(OpsFile, JsonSerializer.Serialize(ops));
    }

    /// <summary>从 zip 插件包导入插件，返回清单。包结构：plugin.json 在 zip 根或唯一一层包裹目录下。
    /// 同 id 已存在时覆盖（先删旧目录）。校验失败抛 InvalidDataException（消息可直接给用户看）。</summary>
    public static PluginManifest ImportZip(string zipPath) => ImportZip(zipPath, null);

    /// <summary>带进度回报的导入（大插件包解压耗时，UI 须走后台线程 + 进度条）：
    /// progress 收到 (已解压文件数, 总文件数)，约每 8 个文件一报，最后一次为 (Total, Total)。</summary>
    public static PluginManifest ImportZip(string zipPath, IProgress<(int Done, int Total)>? progress)
    {
        using var zip = ZipFile.OpenRead(zipPath);

        // 定位 plugin.json：允许在根目录或一层包裹目录下（如 config/plugin.json）
        var manifestEntry = zip.Entries.FirstOrDefault(e =>
            e.FullName.Replace('\\', '/').Trim('/').Split('/').Length <= 2 &&
            e.FullName.Replace('\\', '/').EndsWith("plugin.json", StringComparison.OrdinalIgnoreCase) &&
            !e.FullName.EndsWith("/"));
        if (manifestEntry == null)
            throw new InvalidDataException("插件包中未找到 plugin.json（应位于 zip 根目录或一层子目录下）");

        PluginManifest manifest;
        using (var reader = new StreamReader(manifestEntry.Open()))
            manifest = JsonSerializer.Deserialize<PluginManifest>(reader.ReadToEnd(), PluginLoader.JsonOptions)
                       ?? throw new InvalidDataException("plugin.json 内容为空或格式错误");
        if (!IsValidId(manifest.Id))
            throw new InvalidDataException($"plugin.json 的 id 非法：「{manifest.Id}」（只允许小写字母/数字/短横线/下划线）");
        if (manifest.EntryAssembly.Length == 0 || manifest.EntryType.Length == 0)
            throw new InvalidDataException("plugin.json 缺少 entryAssembly 或 entryType 字段");

        // 包内文件统一相对 plugin.json 所在前缀提取
        string prefix = manifestEntry.FullName[..^"plugin.json".Length];
        bool hasEntry = zip.Entries.Any(e =>
            e.FullName.Replace('\\', '/').Equals(prefix + manifest.EntryAssembly, StringComparison.OrdinalIgnoreCase));
        if (!hasEntry)
            throw new InvalidDataException($"插件包中未找到入口程序集 {manifest.EntryAssembly}");

        string targetDir = Path.Combine(PluginLoader.PluginsRoot, manifest.Id);
        if (Directory.Exists(targetDir))
            Directory.Delete(targetDir, recursive: true); // 同 id 覆盖安装
        Directory.CreateDirectory(targetDir);

        // 先算出待提取条目（进度条分母），再逐个提取
        var toExtract = zip.Entries.Where(e =>
        {
            string name = e.FullName.Replace('\\', '/');
            return name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) &&
                   name[prefix.Length..].Length > 0 && e.Name.Length > 0;
        }).ToList();
        string targetRoot = Path.GetFullPath(targetDir) + Path.DirectorySeparatorChar;
        int done = 0;
        foreach (var entry in toExtract)
        {
            string rel = entry.FullName.Replace('\\', '/')[prefix.Length..];
            string dest = Path.GetFullPath(Path.Combine(targetDir, rel));
            if (!dest.StartsWith(targetRoot, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"插件包含非法路径：{entry.FullName}");
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            entry.ExtractToFile(dest, overwrite: true);
            done++;
            if (done % 8 == 0 || done == toExtract.Count)
                progress?.Report((done, toExtract.Count));
        }
        return manifest;
    }

    /// <summary>删除已安装插件（整个 plugins/&lt;id&gt;/ 目录）。不存在时静默返回。</summary>
    public static void Delete(string id)
    {
        if (!IsValidId(id)) return;
        string dir = Path.Combine(PluginLoader.PluginsRoot, id);
        if (Directory.Exists(dir))
            Directory.Delete(dir, recursive: true);
    }
}
