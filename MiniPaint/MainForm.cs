using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace MiniPaint;

public sealed class MainForm : Form
{
    private readonly CanvasControl _canvas;

    private string? _currentProjectPath;
    private bool _isModified;

    private readonly ToolStripStatusLabel _statusLabel = new();

    public MainForm()
    {
        Text = "MiniPaint Beta 0.2";

        StartPosition = FormStartPosition.CenterScreen;

        Width = 1100;
        Height = 800;

        MinimumSize = new Size(850, 600);

        KeyPreview = true;

        _canvas = new CanvasControl
        {
            Dock = DockStyle.Fill
        };

        _canvas.DocumentChanged += (_, _) =>
        {
            _isModified = true;

            UpdateWindowTitle();
            UpdateStatus();
        };

        var menu = CreateMenu();
        var toolbar = CreateToolbar();

        var status = new StatusStrip();

        status.Items.Add(_statusLabel);

        MainMenuStrip = menu;

        // Порядок добавления нужен для правильной компоновки Dock.
        Controls.Add(_canvas);
        Controls.Add(status);
        Controls.Add(toolbar);
        Controls.Add(menu);

        UpdateWindowTitle();
        UpdateStatus();
    }

    // -------------------------------
    // Главное меню
    // -------------------------------

    private MenuStrip CreateMenu()
    {
        var menu = new MenuStrip();

        var file = new ToolStripMenuItem("Файл");

        file.DropDownItems.Add(
            new ToolStripMenuItem(
                "Новый",
                null,
                (_, _) => NewDocument()
            )
            {
                ShortcutKeys = Keys.Control | Keys.N
            }
        );

        file.DropDownItems.Add(
            new ToolStripMenuItem(
                "Открыть проект...",
                null,
                (_, _) => OpenProject()
            )
            {
                ShortcutKeys = Keys.Control | Keys.O
            }
        );

        file.DropDownItems.Add(
            new ToolStripMenuItem(
                "Сохранить",
                null,
                (_, _) => SaveProject()
            )
            {
                ShortcutKeys = Keys.Control | Keys.S
            }
        );

        file.DropDownItems.Add(
            new ToolStripMenuItem(
                "Сохранить как...",
                null,
                (_, _) => SaveProjectAs()
            )
            {
                ShortcutKeys =
                    Keys.Control | Keys.Shift | Keys.S
            }
        );

        file.DropDownItems.Add(new ToolStripSeparator());

        file.DropDownItems.Add(
            "Импортировать изображение...",
            null,
            (_, _) => ImportImage()
        );

        file.DropDownItems.Add(
            "Экспортировать в PNG...",
            null,
            (_, _) => ExportPng()
        );

        file.DropDownItems.Add(new ToolStripSeparator());

        file.DropDownItems.Add(
            "Выход",
            null,
            (_, _) => Close()
        );

        var edit = new ToolStripMenuItem("Правка");

        edit.DropDownItems.Add(
            new ToolStripMenuItem(
                "Отменить",
                null,
                (_, _) => _canvas.Undo()
            )
            {
                ShortcutKeys = Keys.Control | Keys.Z
            }
        );

        edit.DropDownItems.Add(
            new ToolStripMenuItem(
                "Повторить",
                null,
                (_, _) => _canvas.Redo()
            )
            {
                ShortcutKeys = Keys.Control | Keys.Y
            }
        );

        edit.DropDownItems.Add(new ToolStripSeparator());

        edit.DropDownItems.Add(
            "Очистить холст",
            null,
            (_, _) => _canvas.ClearCanvas()
        );

        menu.Items.Add(file);
        menu.Items.Add(edit);

        return menu;
    }

    // -------------------------------
    // Панель инструментов
    // -------------------------------

    private ToolStrip CreateToolbar()
    {
        var toolbar = new ToolStrip
        {
            GripStyle = ToolStripGripStyle.Hidden,
            Dock = DockStyle.Top
        };

        AddToolButton(toolbar, "Кисть", DrawingTool.Brush);
        AddToolButton(toolbar, "Заливка", DrawingTool.Fill);
        AddToolButton(toolbar, "Ластик", DrawingTool.Eraser);

        toolbar.Items.Add(new ToolStripSeparator());

        var colorButton = new ToolStripButton("Цвет")
        {
            BackColor = _canvas.DrawingColor
        };

        _canvas.DrawingColorChanged += (_, _) =>
        {
            colorButton.BackColor = _canvas.DrawingColor;
        };

        colorButton.Click += (_, _) =>
        {
            using var dialog = new ColorDialog
            {
                Color = _canvas.DrawingColor,
                FullOpen = true
            };

            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                _canvas.DrawingColor = dialog.Color;
            }
        };

        toolbar.Items.Add(colorButton);

        toolbar.Items.Add(
            new ToolStripLabel("Размер:")
        );

        var sizeBox = new ToolStripComboBox
        {
            DropDownStyle = ComboBoxStyle.DropDownList,
            Width = 65
        };

        sizeBox.Items.AddRange(new object[]
        {
            "2", "4", "6", "10", "16", "24", "32"
        });

        sizeBox.SelectedIndex = 2;

        sizeBox.SelectedIndexChanged += (_, _) =>
        {
            if (int.TryParse(
                sizeBox.SelectedItem?.ToString(),
                out int size))
            {
                _canvas.BrushSize = size;
            }
        };

        toolbar.Items.Add(sizeBox);

        toolbar.Items.Add(new ToolStripSeparator());

        var undoButton = new ToolStripButton("Отмена");

        undoButton.Click += (_, _) => _canvas.Undo();

        var redoButton = new ToolStripButton("Повтор");

        redoButton.Click += (_, _) => _canvas.Redo();

        toolbar.Items.Add(undoButton);
        toolbar.Items.Add(redoButton);

        toolbar.Items.Add(new ToolStripSeparator());

        var clearButton = new ToolStripButton("Очистить");

        clearButton.Click += (_, _) => _canvas.ClearCanvas();

        toolbar.Items.Add(clearButton);

        return toolbar;
    }

    private void AddToolButton(
        ToolStrip toolbar,
        string name,
        DrawingTool tool)
    {
        var button = new ToolStripButton(name);

        button.Click += (_, _) =>
        {
            _canvas.ActiveTool = tool;
        };

        toolbar.Items.Add(button);
    }

    // -------------------------------
    // Новый документ
    // -------------------------------

    private void NewDocument()
    {
        if (!ConfirmDiscardChanges())
            return;

        _canvas.NewCanvas(1200, 800);

        _currentProjectPath = null;
        _isModified = true;

        UpdateWindowTitle();
    }

    // -------------------------------
    // Открытие MPAINT
    // -------------------------------

    private void OpenProject()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Открыть проект MiniPaint",
            Filter =
                "MiniPaint Project (*.mpaint)|*.mpaint"
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;

        if (!ConfirmDiscardChanges())
            return;

        try
        {
            _canvas.OpenMpaint(dialog.FileName);

            MarkSaved(dialog.FileName);
        }
        catch (Exception ex)
        {
            ShowError(
                "Не удалось открыть проект.",
                ex
            );
        }
    }

    // -------------------------------
    // Сохранение MPAINT
    // -------------------------------

    private void SaveProject()
    {
        if (string.IsNullOrWhiteSpace(_currentProjectPath))
        {
            SaveProjectAs();
            return;
        }

        try
        {
            _canvas.SaveAsMpaint(_currentProjectPath);

            MarkSaved(_currentProjectPath);
        }
        catch (Exception ex)
        {
            ShowError(
                "Не удалось сохранить проект.",
                ex
            );
        }
    }

    private void SaveProjectAs()
    {
        using var dialog = new SaveFileDialog
        {
            Title = "Сохранить проект MiniPaint",
            Filter =
                "MiniPaint Project (*.mpaint)|*.mpaint",

            DefaultExt = "mpaint",
            AddExtension = true,
            FileName = "Drawing.mpaint"
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;

        try
        {
            _canvas.SaveAsMpaint(dialog.FileName);

            MarkSaved(dialog.FileName);
        }
        catch (Exception ex)
        {
            ShowError(
                "Не удалось сохранить проект.",
                ex
            );
        }
    }

    // -------------------------------
    // Импорт изображения
    // -------------------------------

    private void ImportImage()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Импортировать изображение",

            Filter =
                "Изображения|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff|" +
                "Все файлы|*.*",

            CheckFileExists = true
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;

        if (!ConfirmDiscardChanges())
            return;

        try
        {
            _canvas.OpenImage(dialog.FileName);

            _currentProjectPath = null;
            _isModified = true;

            UpdateWindowTitle();
        }
        catch (Exception ex)
        {
            ShowError(
                "Не удалось импортировать изображение.",
                ex
            );
        }
    }

    // -------------------------------
    // Экспорт PNG
    // -------------------------------

    private void ExportPng()
    {
        using var dialog = new SaveFileDialog
        {
            Title = "Экспортировать изображение",

            Filter = "PNG Image (*.png)|*.png",

            DefaultExt = "png",
            AddExtension = true,
            FileName = "Drawing.png"
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;

        try
        {
            _canvas.SaveAsPng(dialog.FileName);
        }
        catch (Exception ex)
        {
            ShowError(
                "Не удалось экспортировать изображение.",
                ex
            );
        }
    }

    // -------------------------------
    // Состояние документа
    // -------------------------------

    private void MarkSaved(string path)
    {
        _currentProjectPath = path;
        _isModified = false;

        UpdateWindowTitle();
    }

    private void UpdateWindowTitle()
    {
        string documentName = string.IsNullOrWhiteSpace(
            _currentProjectPath
        )
            ? "Без названия"
            : Path.GetFileName(_currentProjectPath);

        string modified = _isModified ? " *" : "";

        Text = $"MiniPaint Beta 0.2 — {documentName}{modified}";
    }

    private void UpdateStatus()
    {
        _statusLabel.Text =
            $"Холст: {_canvas.CanvasWidth} × " +
            $"{_canvas.CanvasHeight} px";
    }

    private bool ConfirmDiscardChanges()
    {
        if (!_isModified)
            return true;

        DialogResult result = MessageBox.Show(
            this,
            "Есть несохранённые изменения.\n" +
            "Продолжить без сохранения?",
            "MiniPaint",
            MessageBoxButtons.YesNo,
            MessageBoxIcon.Warning
        );

        return result == DialogResult.Yes;
    }

    private void ShowError(string message, Exception ex)
    {
        MessageBox.Show(
            this,
            message + "\n\n" + ex.Message,
            "Ошибка MiniPaint",
            MessageBoxButtons.OK,
            MessageBoxIcon.Error
        );
    }

    protected override void OnFormClosing(
        FormClosingEventArgs e)
    {
        if (!ConfirmDiscardChanges())
        {
            e.Cancel = true;
            return;
        }

        base.OnFormClosing(e);
    }
}
