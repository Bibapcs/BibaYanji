using System.ComponentModel;
using System.Runtime.InteropServices;

namespace YanJi.Plugin.PowerMode.Providers;

internal record PowerScheme(Guid Id, string Name);

internal interface IWindowsPowerApi
{
    Guid GetActiveScheme();
    IReadOnlyList<PowerScheme> GetSchemes();
    void SetScheme(Guid scheme);
    Guid? GetOverlay();
    bool TrySetOverlay(Guid overlay);
}

internal sealed class WindowsPowerApi : IWindowsPowerApi
{
    public Guid GetActiveScheme()
    {
        Check(PowerGetActiveScheme(IntPtr.Zero, out var ptr));
        if (ptr == IntPtr.Zero) throw new InvalidOperationException("系统未返回电源方案。");
        try { return Marshal.PtrToStructure<Guid>(ptr); }
        finally { LocalFree(ptr); }
    }

    public IReadOnlyList<PowerScheme> GetSchemes()
    {
        var schemes = new List<PowerScheme>();
        for (uint index = 0; ; index++)
        {
            uint size = 16;
            uint result = PowerEnumerate(IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, 16, index, out var id, ref size);
            if (result == 259) break; // ERROR_NO_MORE_ITEMS
            Check(result);
            schemes.Add(new(id, ReadName(id)));
        }
        return schemes;
    }

    internal static string ReadName(Guid id)
    {
        uint size = 0;
        uint result = PowerReadFriendlyName(IntPtr.Zero, ref id, IntPtr.Zero, IntPtr.Zero, null, ref size);
        if (result != 0 && result != 234) return id.ToString();
        if (size == 0) return id.ToString();
        var buffer = new byte[size];
        return PowerReadFriendlyName(IntPtr.Zero, ref id, IntPtr.Zero, IntPtr.Zero, buffer, ref size) == 0
            ? System.Text.Encoding.Unicode.GetString(buffer).TrimEnd('\0') : id.ToString();
    }

    public void SetScheme(Guid scheme)
    {
        Check(PowerSetActiveScheme(IntPtr.Zero, ref scheme));
        if (GetActiveScheme() != scheme) throw new InvalidOperationException("电源方案读回不一致，可能被原厂软件覆盖。");
    }

    // Overlay 导出在部分系统/电源方案上不可用，必须读回验证并允许降级。
    public Guid? GetOverlay()
    {
        try { return PowerGetEffectiveOverlayScheme(out var id) == 0 ? id : null; }
        catch (EntryPointNotFoundException) { return null; }
    }

    public bool TrySetOverlay(Guid overlay)
    {
        try { return PowerSetActiveOverlayScheme(overlay) == 0 && GetOverlay() == overlay; }
        catch (EntryPointNotFoundException) { return false; }
    }

    static void Check(uint error)
    {
        if (error != 0) throw new Win32Exception((int)error);
    }

    [DllImport("powrprof.dll")]
    static extern uint PowerGetActiveScheme(IntPtr root, out IntPtr scheme);
    [DllImport("powrprof.dll")]
    static extern uint PowerSetActiveScheme(IntPtr root, ref Guid scheme);
    [DllImport("powrprof.dll")]
    static extern uint PowerEnumerate(IntPtr root, IntPtr scheme, IntPtr subgroup, uint access,
        uint index, out Guid buffer, ref uint size);
    [DllImport("powrprof.dll")]
    static extern uint PowerReadFriendlyName(IntPtr root, ref Guid scheme, IntPtr subgroup,
        IntPtr setting, [Out] byte[]? buffer, ref uint size);
    [DllImport("powrprof.dll")]
    static extern uint PowerGetEffectiveOverlayScheme(out Guid scheme);
    [DllImport("powrprof.dll")]
    static extern uint PowerSetActiveOverlayScheme(Guid scheme);
    [DllImport("kernel32.dll")]
    static extern IntPtr LocalFree(IntPtr memory);
}
