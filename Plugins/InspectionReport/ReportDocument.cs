using System.Globalization;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using YanJi.PluginSdk;

namespace YanJi.Plugin.InspectionReport;

internal static class ReportDocument
{
    // 打印文档固定纸张与黑白墨色，不继承屏幕主题或显示器 DPI。
    internal static readonly Size A4 = new(210 / 25.4 * 96, 297 / 25.4 * 96);

    public static DocumentPaginator Create(ReportSnapshot report)
    {
        var document = new FlowDocument
        {
            FontFamily = new FontFamily("Microsoft YaHei"), FontSize = 12,
            Foreground = Brushes.Black, Background = Brushes.White,
            PageWidth = A4.Width, PageHeight = A4.Height,
            PagePadding = new Thickness(42, 40, 42, 56), ColumnWidth = double.PositiveInfinity,
            IsHyphenationEnabled = false
        };
        document.Blocks.Add(new Paragraph(new Run("笔吧验机 · 验机报告"))
        {
            FontSize = 24, FontWeight = FontWeights.Bold, Margin = new Thickness(0, 0, 0, 14), KeepWithNext = true
        });
        document.Blocks.Add(new Paragraph(new Run(report.Overview)) { Margin = new Thickness(0, 0, 0, 12) });
        Heading("验机项目确认记录");
        Rows(report.Modules.Select(m => new ReportEntry(m.Name, m.Status)));
        Heading("硬件配置");
        if (report.Hardware is { } hardware)
        {
            document.Blocks.Add(new Paragraph(new Run($"采集时间：{hardware.CollectedAt:yyyy-MM-dd HH:mm:ss}")));
            foreach (var group in hardware.Groups)
            {
                Heading(group.Title);
                Rows(group.Entries.Concat(group.Details));
            }
        }
        else document.Blocks.Add(new Paragraph(new Run("未采集，请先进入配置核对页面；未安装配置模块时仍可导出其他验机记录。")));
        Heading("性能测试摘要");
        Rows(report.Performance);
        document.Blocks.Add(new Paragraph(new Run(ReportSnapshot.Notice))
        {
            FontSize = 10, Margin = new Thickness(0, 16, 0, 0)
        });
        var paginator = ((IDocumentPaginatorSource)document).DocumentPaginator;
        paginator.PageSize = A4;
        paginator.ComputePageCount();
        return new NumberedPaginator(paginator, report.Id);

        void Heading(string text) => document.Blocks.Add(new Paragraph(new Run(text))
        {
            FontSize = 15, FontWeight = FontWeights.Bold, KeepWithNext = true,
            Margin = new Thickness(0, 14, 0, 6)
        });

        void Rows(IEnumerable<ReportEntry> entries)
        {
            var table = new Table { CellSpacing = 0, Margin = new Thickness(0) };
            table.Columns.Add(new TableColumn { Width = new GridLength(185) });
            table.Columns.Add(new TableColumn { Width = new GridLength(A4.Width - 84 - 185) });
            var rows = new TableRowGroup();
            table.RowGroups.Add(rows);
            foreach (var entry in entries)
            {
                if (entry.Name.Length == 0 && entry.Value.Length == 0) continue;
                var row = new TableRow();
                foreach (string value in new[] { entry.Name, entry.Value })
                    row.Cells.Add(new TableCell(new Paragraph(new Run(value)) { Margin = new Thickness(0) })
                    {
                        Padding = new Thickness(6), BorderThickness = new Thickness(0, 0, 0, 0.5), BorderBrush = Brushes.LightGray
                    });
                rows.Rows.Add(row);
            }
            document.Blocks.Add(table);
        }
    }

    sealed class NumberedPaginator(DocumentPaginator inner, string id) : DocumentPaginator
    {
        public override bool IsPageCountValid => inner.IsPageCountValid;
        public override int PageCount => inner.PageCount;
        public override Size PageSize { get => inner.PageSize; set => inner.PageSize = value; }
        public override IDocumentPaginatorSource Source => inner.Source;
        public override DocumentPage GetPage(int pageNumber)
        {
            var page = inner.GetPage(pageNumber);
            if (page == DocumentPage.Missing) return page;
            var background = new DrawingVisual();
            using (var drawing = background.RenderOpen())
            {
                drawing.DrawRectangle(Brushes.White, null, new Rect(page.Size));
            }
            var footerVisual = new DrawingVisual();
            using (var drawing = footerVisual.RenderOpen())
            {
                var footer = new FormattedText($"笔吧验机  ·  {id}                                      第 {pageNumber + 1} / {PageCount} 页",
                    CultureInfo.GetCultureInfo("zh-CN"), FlowDirection.LeftToRight, new Typeface("Microsoft YaHei"), 10, Brushes.Black, 1);
                drawing.DrawText(footer, new Point(42, page.Size.Height - 32));
            }
            // 直接保留页面 Visual，VisualBrush 在系统 PDF 打印中会把整页栅格化。
            if (VisualTreeHelper.GetParent(page.Visual) is ContainerVisual previous)
                previous.Children.Remove(page.Visual);
            var visual = new ContainerVisual();
            visual.Children.Add(background);
            visual.Children.Add(page.Visual);
            visual.Children.Add(footerVisual);
            return new DocumentPage(visual, page.Size, page.BleedBox, page.ContentBox);
        }
    }
}
