using Microsoft.Win32;
using System.Runtime.InteropServices;

namespace YanJi.PluginSdk.Services;

/// <summary>一台活动显示器的采集结果（当前模式 + EDID 解码）。</summary>
public class DisplayInfo
{
    public string GdiName { get; set; } = "";
    /// <summary>屏幕左上角在虚拟桌面中的坐标（物理像素，PerMonitorV2 口径；主屏为 0,0）。</summary>
    public int X { get; set; }
    public int Y { get; set; }
    public int Width { get; set; }
    public int Height { get; set; }
    public int Frequency { get; set; }
    public EdidParser.EdidInfo? Edid { get; set; }
}

/// <summary>显示器信息采集：EnumDisplayDevices 枚举活动显示器 → EnumDisplaySettings 取当前
/// 分辨率/刷新率 → 注册表读 EDID（HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\…\Device Parameters）
/// → EdidParser 解码（尺寸/面板名/色域）。</summary>
public static class DisplayInfoService
{
    const int DISPLAY_DEVICE_ATTACHED_TO_DESKTOP = 0x1;
    const uint EDD_GET_DEVICE_INTERFACE_NAME = 0x1;
    const int ENUM_CURRENT_SETTINGS = -1;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct DISPLAY_DEVICE
    {
        public int cb;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName = "";
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceString = "";
        public int StateFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceID = "";
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceKey = "";

        public DISPLAY_DEVICE() { } // 结构含字段初始值设定项时必须显式声明构造函数（CS8983）
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct DEVMODE
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName = "";
        public ushort dmSpecVersion;
        public ushort dmDriverVersion;
        public ushort dmSize;
        public ushort dmDriverExtra;
        public uint dmFields;
        // 联合体（16 字节）：打印机字段 或 显示字段（dmPosition + dmDisplayOrientation + dmDisplayFixedOutput）
        public int dmPositionX;
        public int dmPositionY;
        public uint dmDisplayOrientation;
        public uint dmDisplayFixedOutput;
        public short dmColor;
        public short dmDuplex;
        public short dmYResolution;
        public short dmTTOption;
        public short dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName = "";
        public ushort dmLogPixels;
        public uint dmBitsPerPel;
        public uint dmPelsWidth;
        public uint dmPelsHeight;
        public uint dmDisplayFlags;
        public uint dmDisplayFrequency;
        public uint dmICMMethod;
        public uint dmICMIntent;
        public uint dmMediaType;
        public uint dmDitherType;
        public uint dmReserved1;
        public uint dmReserved2;
        public uint dmPanningWidth;
        public uint dmPanningHeight;

        public DEVMODE() { } // 结构含字段初始值设定项时必须显式声明构造函数（CS8983）
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool EnumDisplayDevices(string? lpDevice, uint iDevNum, ref DISPLAY_DEVICE lpDisplayDevice, uint dwFlags);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool EnumDisplaySettings(string lpszDeviceName, int iModeNum, ref DEVMODE lpDevMode);

    /// <summary>枚举当前活动显示器（含当前分辨率/刷新率与 EDID 解码结果）。</summary>
    public static List<DisplayInfo> GetDisplays()
    {
        var list = new List<DisplayInfo>();
        for (uint i = 0; ; i++)
        {
            var dd = new DISPLAY_DEVICE { cb = Marshal.SizeOf<DISPLAY_DEVICE>() };
            if (!EnumDisplayDevices(null, i, ref dd, 0)) break;
            if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) continue;

            var info = new DisplayInfo { GdiName = dd.DeviceName.Replace(@"\\.\", "") }; // \\.\DISPLAY1 → DISPLAY1

            // 当前显示模式：位置（物理像素）/ 分辨率 / 刷新率
            var mode = new DEVMODE { dmSize = (ushort)Marshal.SizeOf<DEVMODE>() };
            if (EnumDisplaySettings(dd.DeviceName, ENUM_CURRENT_SETTINGS, ref mode))
            {
                info.X = mode.dmPositionX;
                info.Y = mode.dmPositionY;
                info.Width = (int)mode.dmPelsWidth;
                info.Height = (int)mode.dmPelsHeight;
                info.Frequency = (int)mode.dmDisplayFrequency;
            }

            // 显示器设备 → 注册表 EDID。两种 DeviceID 都取：dwFlags=1 是设备接口名
            // （\\?\DISPLAY#MFG#INSTANCE#{guid}），dwFlags=0 是 PNP 形式（DISPLAY\MFG\INSTANCE_N）
            var mon1 = new DISPLAY_DEVICE { cb = Marshal.SizeOf<DISPLAY_DEVICE>() };
            var mon0 = new DISPLAY_DEVICE { cb = Marshal.SizeOf<DISPLAY_DEVICE>() };
            bool ok1 = EnumDisplayDevices(dd.DeviceName, 0, ref mon1, EDD_GET_DEVICE_INTERFACE_NAME);
            bool ok0 = EnumDisplayDevices(dd.DeviceName, 0, ref mon0, 0);
            if (ok1 || ok0)
            {
                var edid = ReadEdid(ok1 ? mon1.DeviceID : "", ok0 ? mon0.DeviceID : "");
                info.Edid = edid != null ? EdidParser.Parse(edid) : null;
            }
            list.Add(info);
        }
        return list;
    }

    /// <summary>读注册表 EDID（≥128 字节）：先按解析出的实例名直连，失败再遍历该型号所有实例取第一块有效的。</summary>
    static byte[]? ReadEdid(string interfaceId, string pnpId)
    {
        // 注册表 EDID 位于 Enum\DISPLAY\<厂商产品码>\<实例>\Device Parameters
        var cands = new[] { SplitMonitorId(interfaceId), SplitMonitorId(pnpId) };
        foreach (var (mfgProd, inst) in cands)
        {
            if (mfgProd.Length == 0 || inst == null) continue;
            var edid = ReadEdidAt(
                $@"SYSTEM\CurrentControlSet\Enum\DISPLAY\{mfgProd}\{inst}\Device Parameters");
            if (edid != null) return edid;
        }
        foreach (var (mfgProd, _) in cands)
        {
            if (mfgProd.Length == 0) continue;
            var edid = ReadEdidFirstInstance(mfgProd);
            if (edid != null) return edid;
        }
        return null;
    }

    /// <summary>归一化显示器 DeviceID 为（厂商产品码, 实例名）。兼容两种形式：
    /// 设备接口名 \\?\DISPLAY#AOCA618#5&amp;X&amp;0&amp;UID256#{guid} 与 PNP 形式 DISPLAY\AOCA618\5&amp;X&amp;0&amp;UID256_0
    /// （尾部 _N 是显示序号，不属于注册表实例名）；实例段取不到时返回 null（走实例遍历兜底）。</summary>
    static (string MfgProd, string? Instance) SplitMonitorId(string deviceId)
    {
        var parts = deviceId.Replace('#', '\\').Split('\\', StringSplitOptions.RemoveEmptyEntries);
        for (int i = 0; i + 1 < parts.Length; i++)
        {
            if (parts[i] is not ("DISPLAY" or "MONITOR") || parts[i + 1].Length < 7)
                continue;
            string? inst = i + 2 < parts.Length ? parts[i + 2] : null;
            if (inst != null)
            {
                if (inst.StartsWith('{'))
                    inst = null; // 旧式 MONITOR\MFG\{class-guid}\0005：实例名取不到
                else
                {
                    int u = inst.LastIndexOf('_');
                    if (u > 0 && inst[(u + 1)..].All(char.IsDigit))
                        inst = inst[..u]; // UID256_0 → UID256
                }
            }
            return (parts[i + 1], inst);
        }
        return ("", null);
    }

    /// <summary>遍历该型号下所有实例，取第一块有效 EDID。</summary>
    static byte[]? ReadEdidFirstInstance(string mfgProd)
    {
        try
        {
            using var root = Registry.LocalMachine.OpenSubKey(
                @"SYSTEM\CurrentControlSet\Enum\DISPLAY\" + mfgProd);
            if (root == null) return null;
            foreach (var inst in root.GetSubKeyNames())
            {
                try
                {
                    using var key = root.OpenSubKey(inst + @"\Device Parameters");
                    if (key?.GetValue("EDID") is byte[] { Length: >= 128 } edid) return edid;
                }
                catch { /* 读不到则试下一个实例 */ }
            }
        }
        catch { /* 权限/路径问题则放弃 */ }
        return null;
    }

    static byte[]? ReadEdidAt(string keyPath)
    {
        try
        {
            using var key = Registry.LocalMachine.OpenSubKey(keyPath);
            if (key?.GetValue("EDID") is byte[] { Length: >= 128 } edid) return edid;
        }
        catch { /* 单个键失败跳过 */ }
        return null;
    }
}
