using System.Windows;
using Microsoft.Win32;

namespace YanJi.Services;

/// <summary>深浅主题切换：替换 Application.Resources 里的主题字典（控件样式全用
/// DynamicResource 引用令牌，切换即时生效）。选择持久化到 Settings（settings.json）；
/// 首次运行跟随系统设置（注册表 AppsUseLightTheme），读不到落浅色。</summary>
public static class ThemeManager
{
    static readonly Uri LightUri = new("Themes/Light.xaml", UriKind.Relative);
    static readonly Uri DarkUri = new("Themes/Dark.xaml", UriKind.Relative);

    public static bool IsDark { get; private set; }

    /// <summary>主题变更（切换后由 MainWindow 同步开关状态等）。</summary>
    public static event Action? ThemeChanged;

    public static void ApplyInitial() => Apply(Settings.DarkTheme ?? SystemPrefersDark(), persist: false);

    public static void Set(bool dark) => Apply(dark, persist: true);

    /// <summary>对指定窗口应用当前主题的 DWM 标题栏颜色。新窗口在 Loaded 时调用。</summary>
    public static void ApplyTitleBar(Window window)
    {
        window.Loaded += (_, _) => TitleBarTheme.Apply(window, IsDark);
        // 句柄已创建时立即应用
        if (new System.Windows.Interop.WindowInteropHelper(window).Handle != IntPtr.Zero)
            TitleBarTheme.Apply(window, IsDark);
    }

    static void Apply(bool dark, bool persist)
    {
        IsDark = dark;
        var dicts = Application.Current.Resources.MergedDictionaries;
        for (int i = dicts.Count - 1; i >= 0; i--)
        {
            var s = dicts[i].Source?.OriginalString ?? "";
            if (s.EndsWith("Light.xaml") || s.EndsWith("Dark.xaml"))
                dicts.RemoveAt(i);
        }
        dicts.Add(new ResourceDictionary { Source = dark ? DarkUri : LightUri });
        // 对所有已打开窗口应用 DWM 标题栏
        foreach (Window w in Application.Current.Windows)
            TitleBarTheme.Apply(w, dark);
        if (persist)
        {
            Settings.DarkTheme = dark;
            Settings.Save();
        }
        ThemeChanged?.Invoke();
    }

    /// <summary>跟随 Windows 应用模式（HKCU 个性化注册表）；读不到默认浅色。</summary>
    static bool SystemPrefersDark()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(
                @"SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            return key?.GetValue("AppsUseLightTheme") is int v && v == 0;
        }
        catch { return false; }
    }
}
