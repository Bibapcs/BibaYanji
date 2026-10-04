using Microsoft.Win32.SafeHandles;
using System.Runtime.InteropServices;

namespace YanJi.Plugin.PowerMode.Providers;

internal interface IAsusAcpi
{
    bool Supports(uint device);
    bool Set(uint device, int value);
}

/// <summary>ASUSMode 原型的 ATKACPI 协议参考实现。每次操作独立开关句柄，不保留后台设备。</summary>
internal sealed class AsusAcpi : IAsusAcpi
{
    const uint Ioctl = 0x0022240C, Init = 0x54494E49, Dsts = 0x53545344, Devs = 0x53564544;
    public bool Supports(uint device) => Call(Dsts, device, 0, out uint status) && (status & 0x10000) != 0;
    public bool Set(uint device, int value) => Call(Devs, device, value, out uint status) && (status & 0xffff) == 1;

    static bool Call(uint method, uint device, int value, out uint status)
    {
        status = 0;
        using var handle = CreateFile(@"\\.\ATKACPI", 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
        if (handle.IsInvalid) return false;
        byte[] Build(uint id, uint argument, int data)
        {
            var input = new byte[16];
            BitConverter.GetBytes(id).CopyTo(input, 0);
            BitConverter.GetBytes(8u).CopyTo(input, 4);
            BitConverter.GetBytes(argument).CopyTo(input, 8);
            BitConverter.GetBytes(data).CopyTo(input, 12);
            return input;
        }
        var output = new byte[16];
        // INIT 仅初始化接口；探测路径不发送任何 DEVS 设置命令。
        if (!DeviceIoControl(handle, Ioctl, Build(Init, 0, 0), 16, output, 16, out _, IntPtr.Zero)) return false;
        if (!DeviceIoControl(handle, Ioctl, Build(method, device, value), 16, output, 16,
                out uint bytesReturned, IntPtr.Zero) || bytesReturned < 4) return false;
        status = BitConverter.ToUInt32(output, 0);
        return true;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, EntryPoint = "CreateFileW", SetLastError = true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security,
        uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool DeviceIoControl(SafeFileHandle handle, uint code, byte[] input, uint inputSize,
        [Out] byte[] output, uint outputSize, out uint bytesReturned, IntPtr overlapped);
}
