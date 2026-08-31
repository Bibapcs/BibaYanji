using System.IO;
using System.Runtime.Loader;
using System.Text.Json;
using System.Windows.Controls;
using YanJi.PluginSdk;

namespace YanJi.Services;

/// <summary>plugin.json 清单（反序列化模型；与插件目录里的文件一一对应）。</summary>
public class PluginManifest
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Version { get; set; } = "1.0.0";
    public int Order { get; set; } = 100;
    public bool Terminal { get; set; }
    public string EntryAssembly { get; set; } = "";
    public string EntryType { get; set; } = "";
}

/// <summary>已加载插件：信息 + 入口实例 + 页面（页面实例常驻，切导航不丢状态）。</summary>
public sealed class LoadedPlugin
{
    public required PluginInfo Info { get; init; }
    public required IYanJiPlugin Plugin { get; init; }
    public required UserControl Page { get; init; }
    /// <summary>插件所在目录（plugins/&lt;id&gt;/）。</summary>
    public required string Directory { get; init; }
}

/// <summary>插件加载器：启动时扫 exe 同级 plugins/*/plugin.json，逐个加载程序集并实例化入口类型。
/// 单个插件损坏只跳过该插件并写日志，不影响整体启动与其它插件。</summary>
public static class PluginLoader
{
    public static string PluginsRoot => Path.Combine(AppContext.BaseDirectory, "plugins");

    /// <summary>plugin.json 用小写字段名，反序列化不区分大小写（管理窗口 Peek/导入/扫描共用）。</summary>
    internal static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };

    static bool _resolvingHooked;

    /// <summary>扫描并加载全部插件（按 Order 排序）。host 在创建页面前已填好 Modules 快照。</summary>
    public static List<LoadedPlugin> LoadAll(IHostContext host)
    {
        HookDependencyResolution();
        var result = new List<LoadedPlugin>();
        foreach (var (manifest, dir) in ScanInstalled())
        {
            try
            {
                string asmPath = Path.Combine(dir, manifest.EntryAssembly);
                if (!File.Exists(asmPath))
                    throw new FileNotFoundException($"入口程序集不存在：{manifest.EntryAssembly}");
                var asm = AssemblyLoadContext.Default.LoadFromAssemblyPath(asmPath);
                var type = asm.GetType(manifest.EntryType, throwOnError: true)!;
                if (Activator.CreateInstance(type) is not IYanJiPlugin plugin)
                    throw new InvalidDataException($"{manifest.EntryType} 未实现 IYanJiPlugin");
                result.Add(new LoadedPlugin
                {
                    Info = new PluginInfo(manifest.Id, manifest.Name, manifest.Version, manifest.Order, manifest.Terminal),
                    Plugin = plugin,
                    Page = plugin.CreatePage(host),
                    Directory = dir,
                });
            }
            catch (Exception ex)
            {
                Log($"插件加载失败（{dir}）：{ex.Message}");
            }
        }
        result.Sort((a, b) => a.Info.Order.CompareTo(b.Info.Order));
        return result;
    }

    /// <summary>扫描 plugins/ 下所有带合法 plugin.json 的目录（不加载程序集，管理窗口与加载器共用）。</summary>
    public static List<(PluginManifest Manifest, string Dir)> ScanInstalled()
    {
        var list = new List<(PluginManifest Manifest, string Dir)>();
        if (!System.IO.Directory.Exists(PluginsRoot)) return list;
        foreach (var dir in System.IO.Directory.GetDirectories(PluginsRoot))
        {
            string jsonPath = Path.Combine(dir, "plugin.json");
            if (!File.Exists(jsonPath)) continue;
            try
            {
                var m = JsonSerializer.Deserialize<PluginManifest>(File.ReadAllText(jsonPath), PluginLoader.JsonOptions);
                if (m == null || !PluginPackageService.IsValidId(m.Id) || m.EntryAssembly.Length == 0 || m.EntryType.Length == 0)
                    continue;
                list.Add((m, dir));
            }
            catch (Exception ex)
            {
                Log($"plugin.json 解析失败（{jsonPath}）：{ex.Message}");
            }
        }
        list.Sort((a, b) => a.Manifest.Order.CompareTo(b.Manifest.Order));
        return list;
    }

    /// <summary>插件依赖（如 NAudio.dll、LibreHardwareMonitorLib.dll）在插件目录里，默认加载上下文
    /// 只探测 exe 目录与 TPA——解析失败时到各插件目录找同名 dll 兜底（按名去重，先加载者胜出）。
    /// 兼容 RID 专属托管资产（如 LHM 传递依赖 Mono.Posix.NETStandard，NuGet 只放在
    /// runtimes/win-x64/lib/ 下：插件的 deps.json 不会被处理，必须兜底找到，否则 LHM
    /// OpCode.Open 的方法体在 JIT 时就因缺程序集抛 FileNotFoundException——SMART/传感器全灭）。
    /// 注意：RID 原生资产（native/*.dll）不在此列——Windows 路径不会调到（MonoPosixHelper 仅 Unix）。</summary>
    static void HookDependencyResolution()
    {
        if (_resolvingHooked) return;
        _resolvingHooked = true;
        AssemblyLoadContext.Default.Resolving += (ctx, name) =>
        {
            if (!System.IO.Directory.Exists(PluginsRoot)) return null;
            foreach (var dir in System.IO.Directory.GetDirectories(PluginsRoot))
            {
                string candidate = Path.Combine(dir, name.Name + ".dll");
                string? found = File.Exists(candidate) ? candidate : FindRidAsset(dir, name.Name!);
                if (found == null) continue;
                try { return ctx.LoadFromAssemblyPath(found); }
                catch { /* 该目录的副本加载失败则继续找下一个 */ }
            }
            return null;
        };
    }

    /// <summary>在插件目录的 runtimes/win-x64/lib/ 下按文件名找 RID 专属托管资产（多个 TFM 目录时取排序最后者）。</summary>
    static string? FindRidAsset(string pluginDir, string asmName)
    {
        string ridLib = Path.Combine(pluginDir, "runtimes", "win-x64", "lib");
        if (!System.IO.Directory.Exists(ridLib)) return null;
        return System.IO.Directory.GetFiles(ridLib, asmName + ".dll", SearchOption.AllDirectories)
            .OrderByDescending(p => p)
            .FirstOrDefault();
    }

    internal static void Log(string message)
    {
        try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "YanJi-plugin.log"),
            $"{DateTime.Now:yyyy-MM-dd HH:mm:ss}  {message}\n"); }
        catch { /* 日志失败不致命 */ }
    }
}
