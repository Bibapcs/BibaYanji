using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using Microsoft.Win32;
using YanJi.Services;

namespace YanJi.Views;

/// <summary>插件管理窗口：列出 plugins/ 下已安装插件（名称/版本/路径 + 该模块的组件致谢），
/// 支持删除与导入 zip 插件包。
/// 生效时机：新插件导入后宿主帅即加载（无需重启）；删除/覆盖安装已加载的插件时其 dll
/// 被运行中的进程锁定，写入待处理操作队列并提示自动重启（下次启动在插件加载前执行）。
/// 致谢来自各插件的 IYanJiPlugin.Credits（随模块走，模块删了其致谢也随之消失）。</summary>
public partial class PluginManagerWindow : Window
{
    readonly IReadOnlyList<LoadedPlugin> _loaded;
    /// <summary>导入后即时加载回调（MainWindow.TryLoadPluginAtRuntime），返回是否加载成功。</summary>
    readonly Func<string, bool> _loadPlugin;

    public PluginManagerWindow(IReadOnlyList<LoadedPlugin> loaded, Func<string, bool> loadPlugin)
    {
        _loaded = loaded;
        _loadPlugin = loadPlugin;
        InitializeComponent();
        Loaded += (_, _) => Reload();
    }

    bool IsPluginLoaded(string id) => _loaded.Any(p => p.Info.Id == id);

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
        // 该模块的组件致谢（插件自带；加载失败的插件显示不出，仅影响展示）。
        // 注意措辞：其中可能含专有软件（如 Cinebench R23），所以叫「致谢」而非「开源致谢」。
        var credits = _loaded.FirstOrDefault(p => p.Info.Id == m.Id)?.Plugin.Credits;
        if (credits is { Count: > 0 })
        {
            var tb = new TextBlock
            {
                Text = "致谢：" + string.Join("；", credits.Select(c => $"{c.Name}（{c.Usage}，{c.License}）")),
                TextWrapping = TextWrapping.Wrap, FontSize = 11, Margin = new Thickness(0, 4, 0, 0),
            };
            tb.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
            texts.Children.Add(tb);
        }

        var del = new Button { Content = "删除", Padding = new Thickness(12, 4, 12, 4),
            VerticalAlignment = VerticalAlignment.Center,
            ToolTip = "删除该插件（正在运行的插件会自动重启程序完成删除）" };
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
        if (MessageBox.Show(this, $"确定删除插件「{m.Name}」吗？",
                "删除插件", MessageBoxButton.OKCancel, MessageBoxImage.Warning) != MessageBoxResult.OK)
            return;
        if (IsPluginLoaded(m.Id))
        {
            // 运行中的插件 dll 被进程锁定，排队待删除并自动重启（启动时在插件加载前执行，无锁）
            PluginPackageService.QueuePendingOp("delete", m.Id);
            if (MessageBox.Show(this,
                    $"「{m.Name}」正在运行中，删除将在程序重启后完成。\n是否立即重启？（选择「否」则下次启动时生效）",
                    "删除需要重启", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
                MainWindow.RestartApp();
        }
        else
        {
            try
            {
                PluginPackageService.Delete(m.Id); // 未加载（如损坏）的插件无锁，直接删
                Reload();
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "删除失败：" + ex.Message, "错误", MessageBoxButton.OK, MessageBoxImage.Error);
            }
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
            string? peekId = PeekId(dlg.FileName);

            // 覆盖正在运行的插件：文件被锁定无法直接替换，暂存 zip + 排队 + 提示自动重启
            if (peekId != null && IsPluginLoaded(peekId))
            {
                if (MessageBox.Show(this, $"插件「{peekId}」正在运行中，覆盖安装将在程序重启后完成。\n是否继续并立即重启？（选择「否」取消导入）",
                        "覆盖需要重启", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
                    return;
                string staged = PluginPackageService.StageZipForPendingImport(peekId, dlg.FileName);
                PluginPackageService.QueuePendingOp("import", peekId, staged);
                MainWindow.RestartApp();
                return;
            }

            // 同 id 已安装但未运行（如损坏）时先确认覆盖（ImportZip 的行为是覆盖安装）
            if (peekId != null && Directory.Exists(Path.Combine(PluginLoader.PluginsRoot, peekId)) &&
                MessageBox.Show(this, $"该插件已存在，导入将覆盖它。继续吗？",
                    "覆盖确认", MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK)
                return;

            var m = PluginPackageService.ImportZip(dlg.FileName);
            Reload();
            // 新插件（或此前未加载的插件）导入后立即加载，无需重启
            if (_loadPlugin(m.Id))
                MessageBox.Show(this, $"插件「{m.Name}」导入成功，已出现在左侧导航。",
                    "导入成功", MessageBoxButton.OK, MessageBoxImage.Information);
            else
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
