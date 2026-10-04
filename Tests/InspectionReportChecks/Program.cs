using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using YanJi.Plugin.InspectionReport;
using YanJi.Plugin.StressTest;
using YanJi.PluginSdk;

internal static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        string scratch = Path.Combine(Path.GetTempPath(), "YanJi-report-checks-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(scratch);
        try
        {
            var host = new FakeHost();
            var empty = ReportSnapshot.Capture(host);
            Check(empty.Modules.Count > 0 && empty.Modules.All(m => m.Status == "未安装 / 未加载"), "可选模块缺省状态");
            Check(empty.Hardware == null && empty.ToSummary().Contains("未采集"), "没有配置数据仍可生成摘要");
            host.Installed.Add(new("keyboard", "键盘测试", "1.0.0", 20, false));
            host.Installed.Add(new("report", "验机报告", "1.0.0", 90, true));
            host.Done.Add("keyboard");
            Check(ReportSnapshot.Capture(host).Modules.First().Passed, "通过状态");
            host.Done.Clear();
            Check(!ReportSnapshot.Capture(host).Modules.First().Passed, "撤销通过立即反映");
            Check(!ReportSnapshot.Capture(host).Modules.Any(m => m.Name == "验机报告"), "终点页不计入确认记录");

            string json = Path.Combine(scratch, "scores.json");
            Check(ReportSnapshot.ReadDiskResult(json, null)[0].Value.Contains("未测试"), "缺少成绩");
            File.WriteAllText(json, "{broken");
            Check(ReportSnapshot.ReadDiskResult(json, null)[0].Value.Contains("无法读取"), "损坏 JSON 降级");
            File.WriteAllText(json, "{\"seq_read\":1}");
            Check(ReportSnapshot.ReadDiskResult(json, null)[0].Value.Contains("无法读取"), "缺项 JSON 降级");
            File.WriteAllText(json, "{\"seq_read\":-1,\"seq_write\":2,\"rnd4k_read\":3,\"rnd4k_write\":4}");
            Check(ReportSnapshot.ReadDiskResult(json, null)[0].Value.Contains("无法读取"), "拒绝负数成绩");
            File.WriteAllText(json, "{\"seq_read\":1,\"seq_write\":2,\"rnd4k_read\":3,\"rnd4k_write\":4}");
            Check(ReportSnapshot.ReadDiskResult(json, host.Inspection.StartedAt)[0].Value.Contains("无法确认"), "旧格式成绩不冒充本次");
            WriteScores(DateTimeOffset.Now, Environment.MachineName);
            Check(ReportSnapshot.ReadDiskResult(json, host.Inspection.StartedAt)[0].Value.StartsWith("本次"), "带来源的本次成绩");
            WriteScores(DateTimeOffset.Now.AddDays(-1), Environment.MachineName);
            Check(ReportSnapshot.ReadDiskResult(json, host.Inspection.StartedAt)[0].Value.StartsWith("历史"), "区分历史成绩");
            WriteScores(DateTimeOffset.Now, "OTHER-DEVICE");
            Check(ReportSnapshot.ReadDiskResult(json, host.Inspection.StartedAt)[0].Value.StartsWith("历史"), "区分外部设备成绩");

            DateTime start = DateTime.Now;
            var summary = new StressReportSummary(start, StressTestService.StressMode.Cpu);
            summary.Add(start.AddSeconds(-1), new(100, 999, 100, 999));
            summary.Add(start, new(75, 40, 90, 100));
            summary.Add(start.AddSeconds(2), new(85, 60, 95, 120));
            summary.Add(start.AddSeconds(4), new(double.NaN, null, null, null));
            summary.Add(start.AddSeconds(6), new(0, double.PositiveInfinity, null, null));
            var stress = summary.Finish(start.AddSeconds(8), null);
            Check(stress.CpuPeakTemperature == 85 && stress.CpuAveragePower == 50 && stress.CpuPowerSamples == 2, "峰值 / 均值仅使用本轮有效样本");
            Check(stress.GpuPeakTemperature == null && stress.GpuAveragePower == null, "单烤不统计非负载设备");
            var noSamples = new StressReportSummary(start, StressTestService.StressMode.Both).Finish(start, null);
            Check(noSamples.CpuPeakTemperature == null && noSamples.CpuAveragePower == null, "没有样本保留未知");

            var groups = Enumerable.Range(0, 10).Select(i => new ReportGroup($"模拟硬件分组 {i + 1}",
                Enumerable.Range(0, 12).Select(n => new ReportEntry($"项目 {n + 1}", "模拟数据 · 中文长型号与未知字段 " + new string('长', 50))).ToArray(),
                [new("详细信息", "模拟测试，不是实际验机结果")])).ToArray();
            host.Inspection.Hardware = new(DateTime.Now, groups);
            host.Inspection.Stress = stress;
            host.Installed.Add(new("stress", "散热测试", "1.0.0", 50, false));
            var report = ReportSnapshot.Capture(host);
            var document = ReportDocument.Create(report);
            Check(document.IsPageCountValid && document.PageCount > 2, "长报告自动分页");
            var minimalDocument = ReportDocument.Create(empty);
            Check(minimalDocument.PageCount >= 1, "空数据排版");

            // 可选输出只包含模拟数据；不采集硬件、不启动负载，不作为真机验收。
            if (args.Contains("--render"))
            {
                string renderDir = Path.Combine(AppContext.BaseDirectory, "renders");
                Directory.CreateDirectory(renderDir);
                foreach (int pageNumber in new[] { 0, document.PageCount - 1 })
                {
                    var page = document.GetPage(pageNumber);
                    var bitmap = new RenderTargetBitmap((int)Math.Ceiling(page.Size.Width * 1.5),
                        (int)Math.Ceiling(page.Size.Height * 1.5), 144, 144, PixelFormats.Pbgra32);
                    bitmap.Render(page.Visual);
                    var encoder = new PngBitmapEncoder();
                    encoder.Frames.Add(BitmapFrame.Create(bitmap));
                    using var output = File.Create(Path.Combine(renderDir, $"page-{pageNumber + 1}.png"));
                    encoder.Save(output);
                }
                Console.WriteLine("模拟排版图片：" + renderDir);
            }
            if (args.Contains("--pdf"))
            {
                string pdf = Path.Combine(scratch, "模拟报告.pdf");
                PdfExporter.ExportAsync(document, pdf).GetAwaiter().GetResult();
                Check(File.Exists(pdf) && new FileInfo(pdf).Length > 1000, "系统 PDF 打印完成并保存");
                if (args.Contains("--render")) File.Copy(pdf, Path.Combine(AppContext.BaseDirectory, "renders", "模拟报告.pdf"), overwrite: true);
                // 覆盖导出也走同样的原子替换路径。
                PdfExporter.ExportAsync(minimalDocument, pdf).GetAwaiter().GetResult();
                Check(File.Exists(pdf), "PDF 覆盖导出");
                string protectedPath = Path.Combine(scratch, "已有报告.pdf");
                File.WriteAllText(protectedPath, "keep existing report");
                bool refused = false;
                using (var heldFile = new FileStream(protectedPath, FileMode.Open, FileAccess.Read, FileShare.None))
                {
                    try { PdfExporter.ExportAsync(minimalDocument, protectedPath).GetAwaiter().GetResult(); }
                    catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { refused = true; }
                }
                Check(refused && File.ReadAllText(protectedPath) == "keep existing report", "目标被占用时不破坏已有报告");
            }
            if (args.Contains("--render"))
            {
                var app = new Application();
                string themeDir = Path.Combine(AppContext.BaseDirectory, "Themes");
                foreach (string theme in new[] { "Light", "Dark" })
                {
                    app.Resources.MergedDictionaries.Clear();
                    app.Resources.MergedDictionaries.Add(new ResourceDictionary { Source = new Uri(Path.Combine(themeDir, theme + ".xaml")) });
                    app.Resources.MergedDictionaries.Add(new ResourceDictionary { Source = new Uri(Path.Combine(themeDir, "Controls.xaml")) });
                    var page = new InspectionReportPage(host);
                    page.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent));
                    host.Done.Add("keyboard");
                    host.RaiseDoneChanged();
                    var moduleList = (System.Windows.Controls.ItemsControl)page.FindName("ModuleList");
                    bool FirstPassed() => (bool)moduleList.Items[0].GetType().GetProperty("Passed")!.GetValue(moduleList.Items[0])!;
                    Check(FirstPassed(), theme + " 页面通过状态通知");
                    host.Done.Clear();
                    host.RaiseDoneChanged();
                    Check(!FirstPassed(), theme + " 页面状态撤销通知");
                    host.Done.Add("keyboard");
                    host.RaiseDoneChanged();
                    page.Measure(new Size(1000, 760));
                    page.Arrange(new Rect(0, 0, 1000, 760));
                    page.UpdateLayout();
                    var visual = new DrawingVisual();
                    using (var drawing = visual.RenderOpen())
                    {
                        drawing.DrawRectangle((Brush)app.Resources["WindowBgBrush"], null, new Rect(0, 0, 1000, 760));
                        drawing.DrawRectangle(new VisualBrush(page), null, new Rect(0, 0, 1000, 760));
                    }
                    var bitmap = new RenderTargetBitmap(1500, 1140, 144, 144, PixelFormats.Pbgra32);
                    bitmap.Render(visual);
                    var encoder = new PngBitmapEncoder();
                    encoder.Frames.Add(BitmapFrame.Create(bitmap));
                    using var output = File.Create(Path.Combine(AppContext.BaseDirectory, "renders", theme + ".png"));
                    encoder.Save(output);
                    page.RaiseEvent(new RoutedEventArgs(FrameworkElement.UnloadedEvent));
                }
                Check(true, "深浅主题页面渲染");
            }
            Console.WriteLine("报告自动检查通过（模拟数据；真机验收由用户完成）。");
            return 0;

            void WriteScores(DateTimeOffset time, string machine) => File.WriteAllText(json, JsonSerializer.Serialize(new
            {
                seq_read = 1, seq_write = 2, rnd4k_read = 3, rnd4k_write = 4, drive = "D:", tested_at = time, machine_name = machine
            }));
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
        finally
        {
            try { Directory.Delete(scratch, recursive: true); }
            catch (IOException) { Console.Error.WriteLine("模拟输出仍被打印服务占用：" + scratch); }
        }
    }

    static void Check(bool condition, string name)
    {
        if (!condition) throw new InvalidOperationException("检查失败：" + name);
        Console.WriteLine("通过：" + name);
    }

    sealed class FakeHost : IHostContext
    {
        public List<PluginInfo> Installed { get; } = [];
        public HashSet<string> Done { get; } = [];
        public IReadOnlyList<PluginInfo> Modules => Installed;
        public InspectionData Inspection { get; } = new();
        public bool IsDone(string moduleId) => Done.Contains(moduleId);
        public event Action? DoneChanged;
        public void RaiseDoneChanged() => DoneChanged?.Invoke();
    }
}
