using System.Text;

namespace YanJi.PluginSdk.Services;

/// <summary>EDID（显示器扩展显示识别数据，128 字节）解码：厂商/产品码、面板名称、
/// 物理尺寸（对角线英寸）、色度坐标 → sRGB / DCI-P3 色域容积比估算。</summary>
public static class EdidParser
{
    /// <summary>EDID 解码结果；字段缺失时为默认值（PanelName 空串、尺寸/色域为 0 表示无法读取）。</summary>
    public class EdidInfo
    {
        public string Manufacturer { get; set; } = "";
        public string ProductCode { get; set; } = "";
        public string PanelName { get; set; } = "";
        public int WidthCm { get; set; }
        public int HeightCm { get; set; }
        /// <summary>对角线尺寸（英寸）；物理尺寸未知时为 0。</summary>
        public double DiagonalInches { get; set; }
        /// <summary>sRGB 色域容积比（%）：面板 RGB 三角形面积 ÷ sRGB 标准三角形面积；无有效色度坐标时为 0。</summary>
        public double SrgbVolume { get; set; }
        /// <summary>DCI-P3 色域容积比（%），算法同上。</summary>
        public double P3Volume { get; set; }
    }

    /// <summary>解析 EDID；不足 128 字节或头校验失败返回 null。</summary>
    public static EdidInfo? Parse(byte[]? edid)
    {
        if (edid is not { Length: >= 128 }) return null;
        // 头：00 FF FF FF FF FF FF 00
        byte[] header = [0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00];
        for (int i = 0; i < 8; i++)
            if (edid[i] != header[i]) return null;

        var info = new EdidInfo();

        // 厂商 ID：字节 8-9，大端，3 个 5-bit 字母（值 1 = 'A'，即 'A' + v - 1）；产品码：字节 10-11 小端
        int m = (edid[8] << 8) | edid[9];
        info.Manufacturer = new string([
            (char)('A' + ((m >> 10) & 31) - 1),
            (char)('A' + ((m >> 5) & 31) - 1),
            (char)('A' + (m & 31) - 1)]);
        info.ProductCode = (edid[10] | (edid[11] << 8)).ToString("X4");

        // 物理尺寸：字节 21/22（水平/垂直厘米）；为 0 表示未知
        info.WidthCm = edid[21];
        info.HeightCm = edid[22];
        if (info.WidthCm > 0 && info.HeightCm > 0)
            info.DiagonalInches =
                Math.Sqrt(info.WidthCm * info.WidthCm + info.HeightCm * info.HeightCm) / 2.54;

        // 面板名称：详细描述符块（54/72/90/108）中 tag 0xFC
        for (int off = 54; off <= 108; off += 18)
        {
            if (edid[off] == 0 && edid[off + 1] == 0 && edid[off + 2] == 0 && edid[off + 3] == 0xFC)
            {
                info.PanelName = Encoding.ASCII.GetString(edid, off + 5, 13).Split('\n')[0].Trim('\0', ' ');
                break;
            }
        }

        // 色度坐标（字节 25-34）：10bit 小数 = (高8位 << 2 | 低2位) / 1024。
        // 字节 25 高到低依次是 Rx/Ry/Gx/Gy 的低 2 位，字节 26 是 Bx/By/Wx/Wy 的低 2 位；
        // 字节 27-34 依次是 Rx Ry Gx Gy Bx By Wx Wy 的高 8 位
        double rx = Chrom(edid, 27, 25, 6), ry = Chrom(edid, 28, 25, 4);
        double gx = Chrom(edid, 29, 25, 2), gy = Chrom(edid, 30, 25, 0);
        double bx = Chrom(edid, 31, 26, 6), by = Chrom(edid, 32, 26, 4);
        if (rx > 0 && ry > 0 && gx > 0 && gy > 0 && bx > 0 && by > 0)
        {
            // 色域容积比：面板 RGB 三角形面积 ÷ 标准色域三角形面积。
            // 注意：EDID 顶点可超出标准色域（比值可 >100%），且不代表覆盖率，展示时须标注「容积比」
            double panel = TriangleArea((rx, ry), (gx, gy), (bx, by));
            double srgb = TriangleArea((0.640, 0.330), (0.300, 0.600), (0.150, 0.060));
            double p3 = TriangleArea((0.680, 0.320), (0.265, 0.690), (0.150, 0.060));
            info.SrgbVolume = panel / srgb * 100;
            info.P3Volume = panel / p3 * 100;
        }
        return info;
    }

    /// <summary>10bit 色度坐标：高 8 位在 hi 字节，低 2 位在 lo 字节的 shift 位移处。</summary>
    static double Chrom(byte[] e, int hi, int lo, int shift) =>
        ((e[hi] << 2) | ((e[lo] >> shift) & 3)) / 1024.0;

    /// <summary>CIE xy 平面上三点构成的三角形面积（鞋带公式）。</summary>
    static double TriangleArea((double X, double Y) a, (double X, double Y) b, (double X, double Y) c) =>
        Math.Abs(a.X * (b.Y - c.Y) + b.X * (c.Y - a.Y) + c.X * (a.Y - b.Y)) / 2;
}
