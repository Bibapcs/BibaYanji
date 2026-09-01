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

    async void OnImportClick(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Title = "选择插件包（可多选）",
            Filter = "zip 插件包 (*.zip)|*.zip",
            Multiselect = true,
        };
        if (dlg.ShowDialog(this) != true || dlg.FileNames.Length == 0) return;

        // 先按 id 分类并统一确认（逐包弹窗在多选时太碎）：
        // 覆盖运行中插件 = 暂存 + 排队（重启生效）；覆盖已安装未运行 = ImportZip 直接覆盖
        var runningIds = new List<string>();
        var existIds = new List<string>();
        foreach (string zipPath in dlg.FileNames)
        {
            string? peekId = PeekId(zipPath);
            if (peekId == null) continue;
            if (IsPluginLoaded(peekId)) runningIds.Add(peekId);
            else if (Directory.Exists(Path.Combine(PluginLoader.PluginsRoot, peekId))) existIds.Add(peekId);
        }
        var skipIds = new HashSet<string>();
        if (runningIds.Count > 0 &&
            MessageBox.Show(this,
                $"以下插件正在运行中，覆盖安装将在程序重启后完成：{string.Join("、", runningIds.Distinct())}\n是否继续？（选择「否」则跳过这些插件）",
                "覆盖需要重启", MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes)
            skipIds.UnionWith(runningIds);
        if (existIds.Count > 0 &&
            MessageBox.Show(this,
                $"以下插件已存在，导入将覆盖：{string.Join("、", existIds.Distinct())}。继续吗？（选择「取消」则跳过这些插件）",
                "覆盖确认", MessageBoxButton.OKCancel, MessageBoxImage.Question) != MessageBoxResult.OK)
            skipIds.UnionWith(existIds);

        // 解压大插件包耗时（GPU 包约 7GB），整个批量过程走后台线程 + 进度窗口（防 UI 卡死焦虑）
        var progressWin = new ImportProgressWindow { Owner = this };
        var progress = new Progress<(int Done, int Total)>(p => progressWin.SetProgress(p.Done, p.Total));
        IsEnabled = false;
        progressWin.Show();
        var staged = new List<string>();    // 已排队待重启覆盖的 id
        var imported = new List<string>();  // 导入成功的名称
        var failed = new List<string>();    // 失败的文件与原因
        try
        {
            for (int i = 0; i < dlg.FileNames.Length; i++)
            {
                string zipPath = dlg.FileNames[i];
                progressWin.SetFile($"正在导入 {i + 1}/{dlg.FileNames.Length}：{Path.GetFileName(zipPath)}");
                progressWin.SetIndeterminate("校验插件包…");
                try
                {
                    string? peekId = PeekId(zipPath);
                    if (peekId != null && skipIds.Contains(peekId)) continue;
                    if (peekId != null && IsPluginLoaded(peekId))
                    {
                        // 覆盖正在运行的插件：文件被锁定无法直接替换，暂存 zip + 排队（重启时执行，无锁）
                        progressWin.SetIndeterminate("暂存插件包（大文件拷贝较慢）…");
                        await Task.Run(() =>
                        {
                            string stagedPath = PluginPackageService.StageZipForPendingImport(peekId, zipPath);
                            PluginPackageService.QueuePendingOp("import", peekId, stagedPath);
                        });
                        staged.Add(peekId);
                        continue;
                    }
                    var m = await Task.Run(() => PluginPackageService.ImportZip(zipPath, progress));
                    // 新插件（或此前未加载的插件）导入后立即加载，无需重启
                    imported.Add(_loadPlugin(m.Id) ? m.Name : $"{m.Name}（重启后加载）");
                }
                catch (Exception ex) when (ex is InvalidDataException or IOException or UnauthorizedAccessException)
                {
                    failed.Add($"{Path.GetFileName(zipPath)}：{ex.Message.Split('\n')[0]}");
                }
            }
        }
        finally
        {
            progressWin.Close();
            IsEnabled = true;
        }
        Reload();

        // 汇总（单选时与旧流程等价：一次覆盖确认 + 一次结果提示）
        var sb = new System.Text.StringBuilder();
        if (imported.Count > 0) sb.AppendLine($"成功导入 {imported.Count} 个：{string.Join("、", imported)}（已出现在左侧导航）");
        if (staged.Count > 0) sb.AppendLine($"已排队覆盖 {staged.Distinct().Count()} 个（重启后生效）：{string.Join("、", staged.Distinct())}");
        if (failed.Count > 0) sb.AppendLine($"失败 {failed.Count} 个：\n" + string.Join("\n", failed));
        if (sb.Length == 0) sb.Append("没有可导入的插件。");
        MessageBox.Show(this, sb.ToString().TrimEnd(), "导入结果", MessageBoxButton.OK,
            failed.Count > 0 ? MessageBoxImage.Warning : MessageBoxImage.Information);

        if (staged.Count > 0 &&
            MessageBox.Show(this, "覆盖安装将在程序重启后完成。是否立即重启？（选择「否」则下次启动时生效）",
                "需要重启", MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes)
            MainWindow.RestartApp();
    }

    /// <summary>导入进度小窗（代码构建，无 XAML）：当前文件行 + 确定性进度条（按 zip 内文件数）+
    /// 明细行（校验/暂存阶段转跑马灯）。主题令牌走 DynamicResource，深浅主题自适应。</summary>
    sealed class ImportProgressWindow : Window
    {
        readonly TextBlock _fileText = new() { FontWeight = FontWeights.SemiBold, TextTrimming = TextTrimming.CharacterEllipsis };
        readonly ProgressBar _bar = new() { Height = 10, Margin = new Thickness(0, 10, 0, 0), Minimum = 0, Maximum = 100 };
        readonly TextBlock _detailText = new() { Margin = new Thickness(0, 6, 0, 0) };

        public ImportProgressWindow()
        {
            Title = "导入插件包";
            Width = 440;
            SizeToContent = SizeToContent.Height;
            WindowStartupLocation = WindowStartupLocation.CenterOwner;
            ResizeMode = ResizeMode.NoResize;
            ShowInTaskbar = false;
            SetResourceReference(BackgroundProperty, "WindowBgBrush");
            SetResourceReference(ForegroundProperty, "TextBrush");
            _detailText.SetResourceReference(TextBlock.ForegroundProperty, "TextSecondaryBrush");
            var panel = new StackPanel { Margin = new Thickness(16, 14, 16, 14) };
            panel.Children.Add(_fileText);
            panel.Children.Add(_bar);
            panel.Children.Add(_detailText);
            Content = panel;
        }

        public void SetFile(string text) => _fileText.Text = text;

        public void SetProgress(int done, int total)
        {
            _bar.IsIndeterminate = false;
            _bar.Maximum = Math.Max(1, total);
            _bar.Value = done;
            _detailText.Text = $"已解压 {done} / {total} 个文件";
        }

        public void SetIndeterminate(string detail)
        {
            _bar.IsIndeterminate = true;
            _detailText.Text = detail;
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
