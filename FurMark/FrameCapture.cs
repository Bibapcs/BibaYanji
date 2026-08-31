// 自检用帧抓取：glReadPixels 读回当前帧 → 最小 PNG 编码器（zlib = ZLibStream，无第三方依赖）。
using System.IO.Compression;
using OpenTK.Graphics.OpenGL4;

namespace YanJi.FurMark;

static class FrameCapture
{
    public static void SavePng(string path, int width, int height)
    {
        var rgba = new byte[width * height * 4];
        GL.ReadPixels(0, 0, width, height, PixelFormat.Rgba, PixelType.UnsignedByte, rgba);

        // PNG 扫描线：滤波字节 0 + 每行像素；OpenGL 原点在左下 → 按行上下翻转
        using var raw = new MemoryStream();
        for (int y = height - 1; y >= 0; y--)
        {
            raw.WriteByte(0);
            raw.Write(rgba, y * width * 4, width * 4);
        }

        using var fs = File.Create(path);
        WritePngHeader(fs, width, height);
        WriteChunk(fs, "IDAT", ZlibCompress(raw.ToArray()));
        WriteChunk(fs, "IEND", []);
    }

    static void WritePngHeader(Stream s, int w, int h)
    {
        s.Write([137, 80, 78, 71, 13, 10, 26, 10]); // PNG 魔数
        using var ihdr = new MemoryStream();
        WriteBE32(ihdr, w);
        WriteBE32(ihdr, h);
        ihdr.Write([8, 6, 0, 0, 0]); // 8bit RGBA、deflate、无滤波、无隔行
        WriteChunk(s, "IHDR", ihdr.ToArray());
    }

    static byte[] ZlibCompress(byte[] data)
    {
        using var ms = new MemoryStream();
        using (var z = new ZLibStream(ms, CompressionLevel.Fastest, leaveOpen: true))
            z.Write(data);
        return ms.ToArray();
    }

    static void WriteChunk(Stream s, string type, byte[] data)
    {
        WriteBE32(s, data.Length);
        var typeBytes = System.Text.Encoding.ASCII.GetBytes(type);
        s.Write(typeBytes);
        s.Write(data);
        uint crc = Crc32(typeBytes, data);
        WriteBE32(s, (int)crc);
    }

    static void WriteBE32(Stream s, int v)
    {
        s.WriteByte((byte)(v >> 24));
        s.WriteByte((byte)(v >> 16));
        s.WriteByte((byte)(v >> 8));
        s.WriteByte((byte)v);
    }

    static uint Crc32(byte[] a, byte[] b)
    {
        uint crc = 0xFFFFFFFF;
        foreach (byte x in a) crc = Step(crc, x);
        foreach (byte x in b) crc = Step(crc, x);
        return crc ^ 0xFFFFFFFF;

        static uint Step(uint crc, byte x)
        {
            crc ^= x;
            for (int k = 0; k < 8; k++)
                crc = (crc & 1) != 0 ? 0xEDB88320 ^ (crc >> 1) : crc >> 1;
            return crc;
        }
    }
}
