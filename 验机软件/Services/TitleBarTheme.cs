using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace YanJi.Services;

/// <summary>Windows 标题栏深色模式（DWM 属性 20 = DWMWA_USE_IMMERSIVE_DARK_MODE）。</summary>
public static class TitleBarTheme
{
    [DllImport("dwmapi.dll")]
    static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

    public static void Apply(Window window, bool dark)
    {
        var hwnd = new WindowInteropHelper(window).Handle;
        if (hwnd == IntPtr.Zero) return;
        int val = dark ? 1 : 0;
        DwmSetWindowAttribute(hwnd, 20, ref val, sizeof(int));
    }
}
