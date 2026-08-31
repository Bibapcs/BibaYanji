using System.IO;
using System.Text.Json;

namespace YanJi;

/// <summary>简单设置持久化（%APPDATA%\YanJi\settings.json）。</summary>
public static class Settings
{
    static string DirPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "YanJi");
    static string FilePath => Path.Combine(DirPath, "settings.json");

    /// <summary>主题选择：null = 未设置过（首次运行跟随系统）。</summary>
    public static bool? DarkTheme { get; set; }

    static Settings() => Load();

    class Dto
    {
        public bool? DarkTheme { get; set; }
    }

    static void Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return;
            var dto = JsonSerializer.Deserialize<Dto>(File.ReadAllText(FilePath));
            if (dto == null) return;
            DarkTheme = dto.DarkTheme;
        }
        catch { /* 损坏则用默认 */ }
    }

    public static void Save()
    {
        try
        {
            Directory.CreateDirectory(DirPath);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(new Dto
            {
                DarkTheme = DarkTheme,
            }, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { /* 设置保存失败不致命 */ }
    }
}
