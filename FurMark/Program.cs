// FurMark 毛球 GPU 烤机 —— C# / OpenTK 重实现。
// 移植自 StanislavPetrovV 的 FurMark-Python（MIT，见 Assets/LICENSE.FurMark.txt）：
// ModernGL 全屏 quad + 原版 GLSL 330 fur 片元着色器（vertex.glsl / fragment.glsl 原样保留）。
// 命令行：--width N --height N（默认 1280×720）、--fullscreen、--msaa 0|2|4|8（GLFW 窗口采样数）。
// 烤机语义：不限帧 + 关闭垂直同步，让 GPU 吃满。
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;

namespace YanJi.FurMark;

public static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        int width = 1280, height = 720, msaa = 0;
        bool fullscreen = false, hidden = false;
        string? shotPath = null; // 自检：渲染 30 帧后抓帧存 PNG 退出
        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--width" when i + 1 < args.Length: int.TryParse(args[++i], out width); break;
                case "--height" when i + 1 < args.Length: int.TryParse(args[++i], out height); break;
                case "--fullscreen": fullscreen = true; break;
                case "--msaa" when i + 1 < args.Length: int.TryParse(args[++i], out msaa); break;
                case "--hidden": hidden = true; break; // 无窗口烤机：窗口不可见 + FBO 离屏渲染（不与呈现交互，GPU 吃满）
                case "--shot" when i + 1 < args.Length: shotPath = args[++i]; break;
                case "--help":
                    Console.WriteLine("FurMark (C# / OpenTK) --width N --height N --fullscreen --msaa 0|2|4|8 [--hidden] [--shot out.png]");
                    return 0;
            }
        }
        if (width <= 0) width = 1280;
        if (height <= 0) height = 720;
        if (msaa is not (0 or 2 or 4 or 8)) msaa = 0;

        try
        {
            var nws = new NativeWindowSettings
            {
                ClientSize = new Vector2i(width, height),
                Title = "FurMark (C# / OpenTK)",
                WindowState = fullscreen ? WindowState.Fullscreen : WindowState.Normal,
                NumberOfSamples = msaa,
                Profile = ContextProfile.Core,
                APIVersion = new Version(3, 3),
                StartVisible = !hidden, // --hidden：GLFW 窗口创建即不可见，上下文/渲染循环不受影响
            };
            // GameWindowSettings.Default：UpdateFrequency=0 → 不限帧；
            // 隐藏模式传 offscreen=true（FBO 渲染）与采样数（多样本 renderbuffer）
            using var win = new FurWindow(GameWindowSettings.Default, nws, shotPath, hidden, msaa);
            win.VSync = VSyncMode.Off; // 烤机：关垂直同步让 GPU 吃满
            win.Run();
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("FurMark 启动失败：" + ex.Message);
            return 1;
        }
    }
}
