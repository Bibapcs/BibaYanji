using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using YanJi.PluginSdk;

namespace YanJi.Plugin.InspectionReport;

public partial class InspectionReportPage : UserControl
{
    readonly IHostContext _host;
    bool _exporting;
    string _exportDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "笔吧验机报告");

    public InspectionReportPage(IHostContext host)
    {
        _host = host;
        InitializeComponent();
        NoticeText.Text = ReportSnapshot.Notice;
        Loaded += (_, _) =>
        {
            _host.DoneChanged += OnDoneChanged;
            RefreshReport();
        };
        Unloaded += (_, _) => _host.DoneChanged -= OnDoneChanged;
    }

    void OnDoneChanged() => RefreshReport();
    void Refresh_Click(object sender, RoutedEventArgs e) => RefreshReport();

    ReportSnapshot RefreshReport()
    {
        var report = ReportSnapshot.Capture(_host);
        OverviewText.Text = report.Overview;
        ModuleList.ItemsSource = report.Modules;
        HardwareList.ItemsSource = report.Hardware?.Groups;
        HardwareHint.Text = report.Hardware is { } hardware ? $"硬件采集时间：{hardware.CollectedAt:yyyy-MM-dd HH:mm:ss}" :
            "硬件配置未采集。可先进入「配置核对」，也可直接导出当前记录。";
        PerformanceList.ItemsSource = report.Performance;
        return report;
    }

    async void Export_Click(object sender, RoutedEventArgs e)
    {
        if (_exporting) return;
        try
        {
            var report = RefreshReport();
            Directory.CreateDirectory(_exportDirectory);
            var dialog = new SaveFileDialog
            {
                Title = "保存 PDF 验机报告", Filter = "PDF 验机报告 (*.pdf)|*.pdf", DefaultExt = ".pdf",
                AddExtension = true, OverwritePrompt = true, InitialDirectory = _exportDirectory,
                FileName = $"笔吧验机报告-{report.GeneratedAt:yyyyMMdd-HHmmss}-{report.Id}.pdf"
            };
            if (dialog.ShowDialog(Window.GetWindow(this)) != true) return;
            _exporting = true;
            Actions.IsEnabled = false;
            SetStatus("正在生成 PDF 验机报告……");
            await PdfExporter.ExportAsync(ReportDocument.Create(report), dialog.FileName);
            _exportDirectory = Path.GetDirectoryName(dialog.FileName)!;
            SetStatus("已导出：" + dialog.FileName);
        }
        catch (Exception ex) { SetStatus("导出失败：" + ex.Message, true); }
        finally { _exporting = false; Actions.IsEnabled = true; }
    }

    void Copy_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Clipboard.SetText(RefreshReport().ToSummary());
            SetStatus("报告摘要已复制。");
        }
        catch (Exception ex) { SetStatus("复制失败：" + ex.Message, true); }
    }

    void OpenDirectory_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            Directory.CreateDirectory(_exportDirectory);
            Process.Start(new ProcessStartInfo { FileName = _exportDirectory, UseShellExecute = true });
        }
        catch (Exception ex) { SetStatus("打开目录失败：" + ex.Message, true); }
    }

    void SetStatus(string text, bool error = false)
    {
        StatusText.Text = text;
        StatusText.SetResourceReference(TextBlock.ForegroundProperty, error ? "ErrorBrush" : "TextSecondaryBrush");
    }
}
