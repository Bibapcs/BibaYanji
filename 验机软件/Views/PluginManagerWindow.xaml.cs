using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Microsoft.Win32;
using YanJi.Services;

namespace YanJi.Views;

/// <summary>插件管理窗口：列出 plugins/ 下已安装插件（名称/ID/版本/路径 + 该模块的开源致谢），
/// 支持删除与导入 zip 插件包。增删都在磁盘上操作 plugins/ 目录，重启程序后由 PluginLoader 重新扫描生效。
/// 致谢来自各插件的 IYanJiPlugin.Credits（随模块走，模块删了其致谢也随之消失）。</summary>
public partial class PluginManagerWindow : Window
{
    readonly IReadOnlyList<LoadedPlugin> _loaded;

    public PluginManagerWindow(IReadOnlyList<LoadedPlugin> loaded)
    {
        _loaded = loaded;
        InitializeComponent();
        Loaded += (_, _) => Reload();
    }

    void Reload()
    {
        PluginList.Children.Clear();
        var installed = PluginLoader.ScanInstalled();
        if (installed.Count == 0)
        {
            PluginList.Children.Add(new TextBlock
            {
                Text = "（未安装任何插件）",
                Foreground = (Brush)FindResource("TextSecondaryBrush"),
                Margin = new Thickness(2, 4, 0, 4),
            });
            return;
        }
        foreach (var (m, dir) in installed)
            PluginList.Children.Add(BuildRow(m, dir));
    }

    UIElement BuildRow(PluginManifest m, string dir)
    {
        var name = new TextBlock { Text = $"{m.Name}  v{m.Version}", FontWeight = FontWeights.SemiBold };
        // 详情行只显示目录路径（插件 id 是开发/包结构概念，不展示给用户避免困惑）
        var detail = new TextBlock { Text = dir, Margin = new Thickness(0, 2, 0, 0) };
        detail.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
        var texts = new StackPanel();
        texts.Children.Add(name);
        texts.Children.Add(detail);
        // 该模块的开源致谢（插件自带；加载失败的插件显示不出，仅影响展示）
        var credits = _loaded.FirstOrDefault(p => p.Info.Id == m.Id)?.Plugin.Credits;
        if (credits is { Count: > 0 })
        {
            var tb = new TextBlock
            {
                Text = "开源致谢：" + string.Join("；", credits.Select(c => $"{c.Name}（{c.Usage}，{c.License}）")),
                TextWrapping = TextWrapping.Wrap, FontSize = 11, Margin = new Thickness(0, 4, 0, 0),
            };
            tb.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
            texts.Children.Add(tb);
        }

        var del = new Button { Content = "删除", Padding = new Thickness(12, 4, 12, 4),
            VerticalAlignment = VerticalAlignment.Center,
            ToolTip = "删除该插件（重启程序后生效）" };
        del.Click += (_, _) => OnDeleteClick(m);

        var grid = new Grid { Margin = new Thickness(10, 6, 10, 6) };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.Children.Add(texts);
        Grid.SetColumn(del, 1);
        grid.Children.Add(del);

        return new Border
        {
            Style = (Style)FindResource("CardBorder"),
            Margin = new Thickness(0, 0, 0, 8),
            Child = grid,
        };
    }

    void OnDeleteClick(PluginManifest m)
    {
        if (MessageBox.Show(this, $"确定删除插件「{m.Name}」吗？\n插件目录将被移除，重启程序后生效。",
                "删除插件", MessageBoxButton.OKCancel, MessageBoxImage.Warning) != MessageBoxResult.OK)
            return;
        try
        {
            PluginPackageService.Delete(m.Id);
            Reload();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "删除失败：" + ex.Message, "错误", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    void OnImportClick(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Title = "选择插件包",
            Filter = "zip 插件包 (*.zip)|*.zip",
        };
        if (dlg.ShowDialog(this) != true) return;
        try
        {
            // 同 id 已安装时先确认覆盖（ImportZip 的行为是覆盖安装）
            string? conflictId = PeekId(dlg.FileName);
            if (conflictId != null && Directory.Exists(Path.Combine(PluginLoader.PluginsRoot, conflictId)) &&
                MessageBox.Show(this, $"已存在同 ID 插件「{conflictId}」，导入将覆盖它。继续吗？",
                    "覆盖确认", MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK)
                return;
            var m = PluginPackageService.ImportZip(dlg.FileName);
            Reload();
            MessageBox.Show(this, $"插件「{m.Name}」导入成功。\n重启程序后出现在左侧导航。",
                "导入成功", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex) when (ex is InvalidDataException or IOException or UnauthorizedAccessException)
        {
            MessageBox.Show(this, "导入失败：" + ex.Message, "错误", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    /// <summary>只读 zip 里的 plugin.json 取 id（用于覆盖前的确认提示）；读不到返回 null。</summary>
    static string? PeekId(string zipPath)
    {
        try
        {
            using var zip = System.IO.Compression.ZipFile.OpenRead(zipPath);
            var entry = zip.Entries.FirstOrDefault(e =>
                e.FullName.Replace('\\', '/').EndsWith("plugin.json", StringComparison.OrdinalIgnoreCase));
            if (entry == null) return null;
            using var reader = new StreamReader(entry.Open());
            var m = System.Text.Json.JsonSerializer.Deserialize<PluginManifest>(reader.ReadToEnd(), PluginLoader.JsonOptions);
            return m != null && PluginPackageService.IsValidId(m.Id) ? m.Id : null;
        }
        catch { return null; }
    }
}
