using System.IO;
using System.IO.Compression;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace YanJi.Services;

/// <summary>插件包（zip）导入与删除。插件目录 = exe 同级 plugins/&lt;id&gt;/；增删均重启后生效
/// （运行时不卸载程序集）。</summary>
public static class PluginPackageService
{
    /// <summary>插件 id 合法性（同时是目录名）：小写字母/数字/短横线/下划线，防路径穿越。</summary>
    public static bool IsValidId(string id) => Regex.IsMatch(id, @"^[a-z0-9_-]{1,32}$");

    /// <summary>从 zip 插件包导入插件，返回清单。包结构：plugin.json 在 zip 根或唯一一层包裹目录下。
    /// 同 id 已存在时覆盖（先删旧目录）。校验失败抛 InvalidDataException（消息可直接给用户看）。</summary>
    public static PluginManifest ImportZip(string zipPath)
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

        string targetRoot = Path.GetFullPath(targetDir) + Path.DirectorySeparatorChar;
        foreach (var entry in zip.Entries)
        {
            string name = entry.FullName.Replace('\\', '/');
            if (!name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) continue;
            string rel = name[prefix.Length..];
            if (rel.Length == 0 || entry.Name.Length == 0) continue; // 目录条目
            string dest = Path.GetFullPath(Path.Combine(targetDir, rel));
            if (!dest.StartsWith(targetRoot, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"插件包含非法路径：{entry.FullName}");
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            entry.ExtractToFile(dest, overwrite: true);
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
