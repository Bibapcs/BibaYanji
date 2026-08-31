// 烤机窗口：GLFW 窗口 + 全屏 quad + 原版 GLSL 330 着色器 + 三张纹理（fur/noise/wall），
// u_time 驱动渲染循环（对应 Python 版 main.py 的 on_render）。
// 隐藏模式（offscreen）：渲染目标改为自建的 FBO renderbuffer，完全不走 SwapBuffers——
// NVIDIA 驱动对从未显示的窗口，WGL 交换路径会让 CPU/GPU 流水线产生气泡（GPU 利用率卡在 ~95%），
// 离屏渲染与呈现解耦后任何厂商驱动都能吃满。
// 离屏命令流调度（8060S 实测两坑，缺一不可）：① AMD 驱动对无呈现的命令流不会主动提交，
// 必须每帧 GL.Flush 踢交，否则 GPU 大量空转；② 不能用周期性 glFinish 给队列定界——
// 它把流水线完全抽空、完成上报延迟大，每周期产生空闲气泡（GPU 利用率 40~100% 锯齿）。
// 正确做法 = 每帧 Flush + 栅栏环限深（CPU 最多领先 GPU FrameQueueDepth 帧，等价 SwapBuffers 背压）。
using System.Diagnostics;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;
using StbImageSharp;

namespace YanJi.FurMark;

sealed class FurWindow : GameWindow
{
    const int FrameQueueDepth = 4;      // 离屏模式 CPU 最多领先 GPU 的帧数（栅栏环容量）
    int _program, _vao, _vbo, _ebo;
    int _uResolution, _uTime;
    readonly Stopwatch _clock = new();
    readonly string? _shotPath; // 自检：非空则渲若干帧后抓帧存 PNG 并退出
    readonly bool _offscreen;   // 隐藏模式：渲进 FBO，不走 SwapBuffers（见文件头注释）
    readonly int _samples;      // 请求的 MSAA 采样数（FBO 用多样本 renderbuffer 保持负载语义）
    int _fbo, _rbo;
    bool _offscreenOk;          // FBO 完备性检查通过才走离屏路径，否则回退窗口交换
    int _frames;
    readonly IntPtr[] _fences = new IntPtr[FrameQueueDepth]; // 离屏栅栏环（帧 N 覆盖槽位时先等帧 N-Depth 完成）

    // 全屏 quad（对应 mglw.geometry.quad_fs()）：两个三角形铺满裁剪空间
    static readonly float[] Vertices =
    {
        -1f, -1f, 0f,
         1f, -1f, 0f,
         1f,  1f, 0f,
        -1f,  1f, 0f,
    };
    static readonly uint[] Indices = { 0, 1, 2, 2, 3, 0 };

    public FurWindow(GameWindowSettings gws, NativeWindowSettings nws, string? shotPath = null,
        bool offscreen = false, int samples = 0)
        : base(gws, nws)
    {
        _shotPath = shotPath;
        _offscreen = offscreen;
        _samples = samples;
    }

    protected override void OnLoad()
    {
        base.OnLoad();
        Console.WriteLine($"GL: {GL.GetString(StringName.Version)} / {GL.GetString(StringName.Renderer)}");

        _program = CreateProgram(AssetPath("programs/vertex.glsl"), AssetPath("programs/fragment.glsl"));
        GL.UseProgram(_program);
        _uResolution = GL.GetUniformLocation(_program, "u_resolution");
        _uTime = GL.GetUniformLocation(_program, "u_time");
        // 与 Python 版一致：纹理单元 1/2/3
        GL.Uniform1(GL.GetUniformLocation(_program, "u_texture1"), 1);
        GL.Uniform1(GL.GetUniformLocation(_program, "u_texture2"), 2);
        GL.Uniform1(GL.GetUniformLocation(_program, "u_texture3"), 3);
        StbImage.stbi_set_flip_vertically_on_load(1); // moderngl_window 默认 flip
        LoadTexture(AssetPath("textures/fur.jpg"), TextureUnit.Texture1);
        LoadTexture(AssetPath("textures/noise.png"), TextureUnit.Texture2);
        LoadTexture(AssetPath("textures/wall.jpg"), TextureUnit.Texture3);

        _vao = GL.GenVertexArray();
        GL.BindVertexArray(_vao);
        _vbo = GL.GenBuffer();
        GL.BindBuffer(BufferTarget.ArrayBuffer, _vbo);
        GL.BufferData(BufferTarget.ArrayBuffer, Vertices.Length * sizeof(float), Vertices, BufferUsageHint.StaticDraw);
        _ebo = GL.GenBuffer();
        GL.BindBuffer(BufferTarget.ElementArrayBuffer, _ebo);
        GL.BufferData(BufferTarget.ElementArrayBuffer, Indices.Length * sizeof(uint), Indices, BufferUsageHint.StaticDraw);
        int pos = GL.GetAttribLocation(_program, "in_position");
        GL.EnableVertexAttribArray(pos);
        GL.VertexAttribPointer(pos, 3, VertexAttribPointerType.Float, false, 3 * sizeof(float), 0);

        SetViewportAndResolution();
        if (_offscreen) SetupOffscreenFbo();
        _clock.Start();
    }

    /// <summary>隐藏模式离屏渲染目标：FBO + RGBA8 renderbuffer（MSAA 用多样本存储保持负载语义）。
    /// 完备性检查失败则回退默认 framebuffer（照常 SwapBuffers）。</summary>
    void SetupOffscreenFbo()
    {
        _fbo = GL.GenFramebuffer();
        GL.BindFramebuffer(FramebufferTarget.Framebuffer, _fbo);
        _rbo = GL.GenRenderbuffer();
        GL.BindRenderbuffer(RenderbufferTarget.Renderbuffer, _rbo);
        AllocateRenderbuffer(ClientSize.X, ClientSize.Y);
        GL.FramebufferRenderbuffer(FramebufferTarget.Framebuffer, FramebufferAttachment.ColorAttachment0,
            RenderbufferTarget.Renderbuffer, _rbo);
        _offscreenOk = GL.CheckFramebufferStatus(FramebufferTarget.Framebuffer)
            is FramebufferErrorCode.FramebufferComplete;
        if (!_offscreenOk)
        {
            Console.WriteLine("离屏 FBO 不完备，回退窗口交换渲染");
            GL.BindFramebuffer(FramebufferTarget.Framebuffer, 0);
        }
        // 之后 OnRenderFrame 的渲染/ReadPixels 都以该绑定为准
    }

    void AllocateRenderbuffer(int w, int h)
    {
        if (_samples > 0)
            GL.RenderbufferStorageMultisample(RenderbufferTarget.Renderbuffer, _samples,
                RenderbufferStorage.Rgba8, w, h);
        else
            GL.RenderbufferStorage(RenderbufferTarget.Renderbuffer, RenderbufferStorage.Rgba8, w, h);
    }

    protected override void OnResize(ResizeEventArgs e)
    {
        base.OnResize(e);
        SetViewportAndResolution();
        // 隐藏窗口不会收到 resize，这里只为保持语义正确
        if (_offscreenOk) AllocateRenderbuffer(ClientSize.X, ClientSize.Y);
    }

    void SetViewportAndResolution()
    {
        GL.Viewport(0, 0, ClientSize.X, ClientSize.Y);
        if (_uResolution >= 0)
            GL.Uniform2(_uResolution, (float)ClientSize.X, (float)ClientSize.Y);
    }

    protected override void OnRenderFrame(FrameEventArgs e)
    {
        base.OnRenderFrame(e);
        GL.Clear(ClearBufferMask.ColorBufferBit);
        GL.Uniform1(_uTime, (float)_clock.Elapsed.TotalSeconds);
        GL.BindVertexArray(_vao);
        GL.DrawElements(PrimitiveType.Triangles, Indices.Length, DrawElementsType.UnsignedInt, 0);
        _frames++;
        // 自检抓帧：渲染满 30 帧（毛球动画已进入状态）后读回帧存 PNG 并退出
        if (_shotPath != null && _frames >= 30)
        {
            FrameCapture.SavePng(_shotPath, ClientSize.X, ClientSize.Y);
            Console.WriteLine("自检截图已保存：" + _shotPath);
            Close();
            return;
        }
        if (_offscreenOk)
        {
            // 不 SwapBuffers：离屏渲染与呈现解耦（机制见文件头注释）。
            // 每帧 Flush 踢交命令流；栅栏环把 CPU 领先限在 FrameQueueDepth 帧内提供背压，
            // GPU 既不会饿死也不会被周期性 glFinish 抽空。
            int slot = _frames % FrameQueueDepth;
            if (_fences[slot] != IntPtr.Zero)
            {
                GL.ClientWaitSync(_fences[slot], ClientWaitSyncFlags.None, ulong.MaxValue);
                GL.DeleteSync(_fences[slot]);
                _fences[slot] = IntPtr.Zero;
            }
            _fences[slot] = GL.FenceSync(SyncCondition.SyncGpuCommandsComplete, WaitSyncFlags.None);
            GL.Flush(); // FenceSync 只把栅栏插入命令流，必须 Flush 才会提交到硬件
        }
        else
        {
            SwapBuffers();
        }
    }

    protected override void OnUnload()
    {
        foreach (IntPtr fence in _fences)
            if (fence != IntPtr.Zero) GL.DeleteSync(fence);
        if (_vbo != 0) GL.DeleteBuffer(_vbo);
        if (_ebo != 0) GL.DeleteBuffer(_ebo);
        if (_vao != 0) GL.DeleteVertexArray(_vao);
        if (_program != 0) GL.DeleteProgram(_program);
        if (_rbo != 0) GL.DeleteRenderbuffer(_rbo);
        if (_fbo != 0) GL.DeleteFramebuffer(_fbo);
        base.OnUnload();
    }

    static string AssetPath(string rel) =>
        Path.Combine(AppContext.BaseDirectory, "Assets", rel.Replace('/', Path.DirectorySeparatorChar));

    static void LoadTexture(string path, TextureUnit unit)
    {
        int tex = GL.GenTexture();
        GL.ActiveTexture(unit);
        GL.BindTexture(TextureTarget.Texture2D, tex);
        using var fs = File.OpenRead(path);
        var img = ImageResult.FromStream(fs, ColorComponents.RedGreenBlueAlpha);
        GL.TexImage2D(TextureTarget.Texture2D, 0, PixelInternalFormat.Rgba, img.Width, img.Height, 0,
            PixelFormat.Rgba, PixelType.UnsignedByte, img.Data);
        GL.GenerateMipmap(GenerateMipmapTarget.Texture2D);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMinFilter, (int)TextureMinFilter.LinearMipmapLinear);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureMagFilter, (int)TextureMagFilter.Linear);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureWrapS, (int)TextureWrapMode.Repeat);
        GL.TexParameter(TextureTarget.Texture2D, TextureParameterName.TextureWrapT, (int)TextureWrapMode.Repeat);
    }

    static int CreateProgram(string vsPath, string fsPath)
    {
        int vs = Compile(ShaderType.VertexShader, File.ReadAllText(vsPath));
        int fs = Compile(ShaderType.FragmentShader, File.ReadAllText(fsPath));
        int program = GL.CreateProgram();
        GL.AttachShader(program, vs);
        GL.AttachShader(program, fs);
        GL.LinkProgram(program);
        GL.GetProgram(program, GetProgramParameterName.LinkStatus, out int ok);
        if (ok == 0)
            throw new InvalidOperationException("着色器链接失败：" + GL.GetProgramInfoLog(program));
        GL.DetachShader(program, vs);
        GL.DetachShader(program, fs);
        GL.DeleteShader(vs);
        GL.DeleteShader(fs);
        return program;
    }

    static int Compile(ShaderType type, string source)
    {
        int shader = GL.CreateShader(type);
        GL.ShaderSource(shader, source);
        GL.CompileShader(shader);
        GL.GetShader(shader, ShaderParameter.CompileStatus, out int ok);
        if (ok == 0)
            throw new InvalidOperationException($"{type} 编译失败：" + GL.GetShaderInfoLog(shader));
        return shader;
    }
}
