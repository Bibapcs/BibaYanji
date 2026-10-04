using Microsoft.Win32;
using System.Runtime.InteropServices;

namespace YanJi.Plugin.PowerMode.Core;

public record SystemIdentity(string Manufacturer, string Model);

public static class SystemPowerHelper
{
    public static SystemIdentity GetIdentity()
    {
        try
        {
            using var key = Registry.LocalMachine.OpenSubKey(@"HARDWARE\DESCRIPTION\System\BIOS");
            string Read(string name) => (key?.GetValue(name) as string)?.Trim() ?? "";
            var manufacturer = Read("SystemManufacturer");
            var model = Read("SystemProductName");
            if (manufacturer.Length == 0) manufacturer = Read("BaseBoardManufacturer");
            if (model.Length == 0) model = Read("BaseBoardProduct");
            return new(manufacturer.Length == 0 ? "未知厂商" : manufacturer,
                model.Length == 0 ? "未知型号" : model);
        }
        catch { return new("未知厂商", "未知型号"); }
    }

    /// <summary>null = 无法确认供电；桌面机也以系统返回的 AC 状态为准。</summary>
    public static bool? IsAcConnected() => GetSystemPowerStatus(out var status)
        ? status.AcLineStatus switch { 0 => false, 1 => true, _ => null } : null;

    [StructLayout(LayoutKind.Sequential)]
    struct SystemPowerStatus
    {
        public byte AcLineStatus, BatteryFlag, BatteryLifePercent, SystemStatusFlag;
        public uint BatteryLifeTime, BatteryFullLifeTime;
    }

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool GetSystemPowerStatus(out SystemPowerStatus status);
}
