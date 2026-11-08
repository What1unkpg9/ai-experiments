using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace MiniPaint;

public enum DrawingTool
{
    Brush,
    Eraser,
    Fill
}

public sealed class CanvasControl : Control
{
    private Bitmap _bitmap;

    private bool _isDrawing;
    private Point _lastPoint;

    private readonly List<Bitmap> _undoHistory = new();
    private readonly List<Bitmap> _redoHistory = new();

    private const int MaxHistoryEntries = 10;
    private const long MaxHistoryBytes = 128L * 1024 * 1024;
    private const long MaxPixelCount = 20_000_000;

    public event EventHandler? DocumentChanged;
    public event EventHandler? DrawingColorChanged;

    public DrawingTool ActiveTool { get; set; } = DrawingTool.Brush;

    private Color _drawingColor = Color.Black;

    public Color DrawingColor
    {
        get => _drawingColor;

        set
        {
            _drawingColor = value;
            DrawingColorChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    public int BrushSize { get; set; } = 6;

    public int CanvasWidth => _bitmap.Width;
    public int CanvasHeight => _bitmap.Height;

    public CanvasControl()
    {
        SetStyle(
            ControlStyles.UserPaint |
            ControlStyles.AllPaintingInWmPaint |
            ControlStyles.OptimizedDoubleBuffer |
            ControlStyles.ResizeRedraw,
            true
        );

        BackColor = Color.FromArgb(55, 55, 55);
        Cursor = Cursors.Cross;

        _bitmap = CreateWhiteBitmap(1200, 800);
    }

    private static Bitmap CreateWhiteBitmap(
        int width,
        int height)
    {
        var bitmap = new Bitmap(
            width,
            height,
            PixelFormat.Format32bppArgb
        );

        using (Graphics graphics = Graphics.FromImage(bitmap))
        {
            graphics.Clear(Color.White);
        }

        return bitmap;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);

        e.Graphics.Clear(BackColor);

        RectangleF imageRectangle = GetImageRectangle();

        e.Graphics.InterpolationMode =
            InterpolationMode.HighQualityBicubic;

        e.Graphics.PixelOffsetMode =
            PixelOffsetMode.HighQuality;

        e.Graphics.DrawImage(_bitmap, imageRectangle);

        using var border = new Pen(Color.DimGray, 1);

        e.Graphics.DrawRectangle(
            border,
            imageRectangle.X,
            imageRectangle.Y,
            imageRectangle.Width,
            imageRectangle.Height
        );
    }

    private RectangleF GetImageRectangle()
    {
        if (ClientSize.Width <= 0 || ClientSize.Height <= 0)
            return RectangleF.Empty;

        float scale = Math.Min(
            ClientSize.Width / (float)_bitmap.Width,
            ClientSize.Height / (float)_bitmap.Height
        );

        float width = _bitmap.Width * scale;
        float height = _bitmap.Height * scale;

        float x = (ClientSize.Width - width) / 2f;
        float y = (ClientSize.Height - height) / 2f;

        return new RectangleF(x, y, width, height);
    }

    private bool TryGetImagePoint(
        Point clientPoint,
        out Point imagePoint)
    {
        RectangleF rectangle = GetImageRectangle();

        if (rectangle.IsEmpty ||
            !rectangle.Contains(clientPoint.X, clientPoint.Y))
        {
            imagePoint = Point.Empty;
            return false;
        }

        float scale = rectangle.Width / _bitmap.Width;

        int x = (int)((clientPoint.X - rectangle.X) / scale);
        int y = (int)((clientPoint.Y - rectangle.Y) / scale);

        x = Math.Clamp(x, 0, _bitmap.Width - 1);
        y = Math.Clamp(y, 0, _bitmap.Height - 1);

        imagePoint = new Point(x, y);

        return true;
    }

    // -------------------------------
    // Рисование
    // -------------------------------

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);

        if (e.Button != MouseButtons.Left)
            return;

        Focus();

        if (!TryGetImagePoint(e.Location, out Point point))
            return;

        if (ActiveTool == DrawingTool.Fill)
        {
            FloodFill(point);
            return;
        }

        RecordUndoState();

        _isDrawing = true;
        _lastPoint = point;

        DrawSegment(point, point);

        Invalidate();
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);

        if (!_isDrawing || e.Button != MouseButtons.Left)
            return;

        if (!TryGetImagePoint(e.Location, out Point point))
            return;

        DrawSegment(_lastPoint, point);

        _lastPoint = point;

        Invalidate();
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);

        if (!_isDrawing)
            return;

        _isDrawing = false;

        Invalidate();

        DocumentChanged?.Invoke(this, EventArgs.Empty);
    }

    private void DrawSegment(Point start, Point end)
    {
        using Graphics graphics = Graphics.FromImage(_bitmap);

        graphics.SmoothingMode = SmoothingMode.AntiAlias;

        Color color = ActiveTool == DrawingTool.Eraser
            ? Color.White
            : DrawingColor;

        float diameter = Math.Max(1, BrushSize);

        if (start == end)
        {
            using var brush = new SolidBrush(color);

            graphics.FillEllipse(
                brush,
                start.X - diameter / 2f,
                start.Y - diameter / 2f,
                diameter,
                diameter
            );
        }
        else
        {
            using var pen = new Pen(color, diameter)
            {
                StartCap = LineCap.Round,
                EndCap = LineCap.Round,
                LineJoin = LineJoin.Round
            };

            graphics.DrawLine(pen, start, end);
        }
    }

    // -------------------------------
    // История действий
    // -------------------------------

    private static long GetBitmapMemory(Bitmap bitmap)
    {
        return (long)bitmap.Width * bitmap.Height * 4;
    }

    private static void AddHistorySnapshot(
        List<Bitmap> history,
        Bitmap current)
    {
        history.Add((Bitmap)current.Clone());

        long totalMemory = 0;

        foreach (Bitmap item in history)
        {
            totalMemory += GetBitmapMemory(item);
        }

        while (history.Count > MaxHistoryEntries ||
               (history.Count > 1 &&
                totalMemory > MaxHistoryBytes))
        {
            Bitmap oldest = history[0];

            totalMemory -= GetBitmapMemory(oldest);

            history.RemoveAt(0);

            oldest.Dispose();
        }
    }

    private static void ClearHistory(List<Bitmap> history)
    {
        foreach (Bitmap bitmap in history)
        {
            bitmap.Dispose();
        }

        history.Clear();
    }

    private void RecordUndoState()
    {
        AddHistorySnapshot(_undoHistory, _bitmap);

        ClearHistory(_redoHistory);
    }

    public void Undo()
    {
        if (_undoHistory.Count == 0)
            return;

        AddHistorySnapshot(_redoHistory, _bitmap);

        Bitmap previous = _undoHistory[^1];

        _undoHistory.RemoveAt(_undoHistory.Count - 1);

        _bitmap.Dispose();

        _bitmap = previous;

        Invalidate();

        DocumentChanged?.Invoke(this, EventArgs.Empty);
    }

    public void Redo()
    {
        if (_redoHistory.Count == 0)
            return;

        AddHistorySnapshot(_undoHistory, _bitmap);

        Bitmap next = _redoHistory[^1];

        _redoHistory.RemoveAt(_redoHistory.Count - 1);

        _bitmap.Dispose();

        _bitmap = next;

        Invalidate();

        DocumentChanged?.Invoke(this, EventArgs.Empty);
    }

    // -------------------------------
    // Очистка и размеры
    // -------------------------------

    public void ClearCanvas()
    {
        RecordUndoState();

        using Graphics graphics = Graphics.FromImage(_bitmap);

        graphics.Clear(Color.White);

        Invalidate();

        DocumentChanged?.Invoke(this, EventArgs.Empty);
    }

    public void NewCanvas(int width, int height)
    {
        if (width < 1 || height < 1 ||
            width > 16000 || height > 16000 ||
            (long)width * height > MaxPixelCount)
        {
            throw new ArgumentException(
                "Недопустимый размер холста."
            );
        }

        ReplaceBitmap(CreateWhiteBitmap(width, height));
    }

    // -------------------------------
    // Открытие и сохранение
    // -------------------------------

    private void ReplaceBitmap(Bitmap replacement)
    {
        try
        {
            RecordUndoState();
        }
        catch
        {
            replacement.Dispose();
            throw;
        }

        Bitmap oldBitmap = _bitmap;

        _bitmap = replacement;

        oldBitmap.Dispose();

        Invalidate();

        DocumentChanged?.Invoke(this, EventArgs.Empty);
    }

    public void OpenImage(string path)
    {
        using Image source = Image.FromFile(path);

        if (source.Width < 1 ||
            source.Height < 1 ||
            (long)source.Width * source.Height > MaxPixelCount)
        {
            throw new InvalidDataException(
                "Изображение слишком большое."
            );
        }

        var replacement = new Bitmap(
            source.Width,
            source.Height,
            PixelFormat.Format32bppArgb
        );

        try
        {
            using (Graphics graphics = Graphics.FromImage(replacement))
            {
                graphics.DrawImage(
                    source,
                    new Rectangle(
                        0, 0, source.Width, source.Height
                    )
                );
            }

            ReplaceBitmap(replacement);
        }
        catch
        {
            replacement.Dispose();
            throw;
        }
    }

    public void OpenMpaint(string path)
    {
        Bitmap replacement = PaintFileFormat.Load(path);

        ReplaceBitmap(replacement);
    }

    public void SaveAsMpaint(string path)
    {
        PaintFileFormat.Save(_bitmap, path);
    }

    public void SaveAsPng(string path)
    {
        _bitmap.Save(path, ImageFormat.Png);
    }

    // -------------------------------
    // Заливка
    // -------------------------------

    private void FloodFill(Point start)
    {
        int width = _bitmap.Width;
        int height = _bitmap.Height;

        int targetColor = _bitmap
            .GetPixel(start.X, start.Y)
            .ToArgb();

        int replacementColor = DrawingColor.ToArgb();

        if (targetColor == replacementColor)
            return;

        RecordUndoState();

        var rectangle = new Rectangle(0, 0, width, height);

        BitmapData data = _bitmap.LockBits(
            rectangle,
            ImageLockMode.ReadWrite,
            PixelFormat.Format32bppArgb
        );

        try
        {
            int[] pixels = new int[width * height];

            for (int y = 0; y < height; y++)
            {
                Marshal.Copy(
                    IntPtr.Add(data.Scan0, y * data.Stride),
                    pixels,
                    y * width,
                    width
                );
            }

            var stack = new Stack<int>();

            stack.Push(start.Y * width + start.X);

            while (stack.Count > 0)
            {
                int index = stack.Pop();

                if (pixels[index] != targetColor)
                    continue;

                int x = index % width;
                int y = index / width;

                int left = x;
                int right = x;

                while (left > 0 &&
                       pixels[y * width + left - 1] == targetColor)
                {
                    left--;
                }

                while (right < width - 1 &&
                       pixels[y * width + right + 1] == targetColor)
                {
                    right++;
                }

                int rowOffset = y * width;

                for (int i = left; i <= right; i++)
                {
                    pixels[rowOffset + i] = replacementColor;
                }

                if (y > 0)
                {
                    AddRunsInRow(
                        pixels, width, y - 1,
                        left, right, targetColor, stack
                    );
                }

                if (y < height - 1)
                {
                    AddRunsInRow(
                        pixels, width, y + 1,
                        left, right, targetColor, stack
                    );
                }
            }

            for (int y = 0; y < height; y++)
            {
                Marshal.Copy(
                    pixels,
                    y * width,
                    IntPtr.Add(data.Scan0, y * data.Stride),
                    width
                );
            }
        }
        finally
        {
            _bitmap.UnlockBits(data);
        }

        Invalidate();

        DocumentChanged?.Invoke(this, EventArgs.Empty);
    }

    private static void AddRunsInRow(
        int[] pixels,
        int width,
        int y,
        int left,
        int right,
        int targetColor,
        Stack<int> stack)
    {
        int rowOffset = y * width;

        bool insideRun = false;

        for (int x = left; x <= right; x++)
        {
            int index = rowOffset + x;

            if (pixels[index] == targetColor)
            {
                if (!insideRun)
                    stack.Push(index);

                insideRun = true;
            }
            else
            {
                insideRun = false;
            }
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _bitmap.Dispose();

            ClearHistory(_undoHistory);
            ClearHistory(_redoHistory);
        }

        base.Dispose(disposing);
    }
}
