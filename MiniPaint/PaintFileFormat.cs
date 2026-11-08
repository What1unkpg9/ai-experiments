using System;
using System.Buffers.Binary;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;

namespace MiniPaint;

public static class PaintFileFormat
{
    private const int HeaderSize = 24;
    private const ushort Version = 1;
    private const byte PixelFormatBgra32 = 1;

    private const int MaxFileSize = 100 * 1024 * 1024;
    private const long MaxPixelCount = 20_000_000;

    public static void Save(Bitmap source, string path)
    {
        ValidateDimensions(source.Width, source.Height);

        // Приводим изображение к единому формату пикселей.
        using var normalized = new Bitmap(
            source.Width,
            source.Height,
            PixelFormat.Format32bppArgb
        );

        using (Graphics graphics = Graphics.FromImage(normalized))
        {
            graphics.DrawImage(
                source,
                new Rectangle(0, 0, source.Width, source.Height)
            );
        }

        byte[] rawData = ReadBitmapPixels(normalized);

        // Сжимаем пиксели при помощи ZLIB.
        byte[] compressedData;

        using (var memory = new MemoryStream())
        {
            using (var zlib = new ZLibStream(
                memory,
                CompressionLevel.Optimal,
                leaveOpen: true))
            {
                zlib.Write(rawData, 0, rawData.Length);
            }

            compressedData = memory.ToArray();
        }

        if (compressedData.Length >
            MaxFileSize - HeaderSize - 4)
        {
            throw new InvalidDataException(
                "Слишком большой сжатый файл."
            );
        }

        // Создаём заголовок размером 24 байта.
        byte[] header = new byte[HeaderSize];

        header[0] = (byte)'M';
        header[1] = (byte)'P';
        header[2] = (byte)'N';
        header[3] = (byte)'T';

        BinaryPrimitives.WriteUInt16LittleEndian(
            header.AsSpan(4, 2),
            Version
        );

        header[6] = PixelFormatBgra32;
        header[7] = 0;

        BinaryPrimitives.WriteInt32LittleEndian(
            header.AsSpan(8, 4),
            source.Width
        );

        BinaryPrimitives.WriteInt32LittleEndian(
            header.AsSpan(12, 4),
            source.Height
        );

        BinaryPrimitives.WriteInt32LittleEndian(
            header.AsSpan(16, 4),
            rawData.Length
        );

        BinaryPrimitives.WriteInt32LittleEndian(
            header.AsSpan(20, 4),
            compressedData.Length
        );

        // CRC32 считается по заголовку и сжатым данным.
        uint crc = Crc32.Calculate(header, compressedData);

        // Размещаем CRC32 в середине сжатого блока.
        int middle = compressedData.Length / 2;

        using var output = new FileStream(
            path,
            FileMode.Create,
            FileAccess.Write,
            FileShare.None
        );

        output.Write(header, 0, header.Length);

        output.Write(
            compressedData,
            0,
            middle
        );

        Span<byte> crcBytes = stackalloc byte[4];

        BinaryPrimitives.WriteUInt32LittleEndian(
            crcBytes,
            crc
        );

        output.Write(crcBytes);

        output.Write(
            compressedData,
            middle,
            compressedData.Length - middle
        );
    }

    public static Bitmap Load(string path)
    {
        if (!File.Exists(path))
        {
            throw new FileNotFoundException(
                "Файл не найден.",
                path
            );
        }

        long fileLength = new FileInfo(path).Length;

        if (fileLength < HeaderSize + 4 ||
            fileLength > MaxFileSize)
        {
            throw new InvalidDataException(
                "Недопустимый размер файла."
            );
        }

        byte[] fileData = File.ReadAllBytes(path);

        ReadOnlySpan<byte> header =
            fileData.AsSpan(0, HeaderSize);

        // Проверяем сигнатуру.
        if (header[0] != (byte)'M' ||
            header[1] != (byte)'P' ||
            header[2] != (byte)'N' ||
            header[3] != (byte)'T')
        {
            throw new InvalidDataException(
                "Это не файл MiniPaint."
            );
        }

        ushort version =
            BinaryPrimitives.ReadUInt16LittleEndian(
                header.Slice(4, 2)
            );

        if (version != Version)
        {
            throw new InvalidDataException(
                $"Неподдерживаемая версия формата: {version}."
            );
        }

        if (header[6] != PixelFormatBgra32 ||
            header[7] != 0)
        {
            throw new InvalidDataException(
                "Неподдерживаемый формат изображения."
            );
        }

        int width =
            BinaryPrimitives.ReadInt32LittleEndian(
                header.Slice(8, 4)
            );

        int height =
            BinaryPrimitives.ReadInt32LittleEndian(
                header.Slice(12, 4)
            );

        int rawLength =
            BinaryPrimitives.ReadInt32LittleEndian(
                header.Slice(16, 4)
            );

        int compressedLength =
            BinaryPrimitives.ReadInt32LittleEndian(
                header.Slice(20, 4)
            );

        ValidateDimensions(width, height);

        long expectedRawLength = (long)width * height * 4;

        if (rawLength != expectedRawLength)
        {
            throw new InvalidDataException(
                "Неверный размер исходных данных."
            );
        }

        if (compressedLength <= 0)
        {
            throw new InvalidDataException(
                "Некорректный размер сжатых данных."
            );
        }

        long expectedFileLength =
            HeaderSize + (long)compressedLength + 4;

        if (fileData.Length != expectedFileLength)
        {
            throw new InvalidDataException(
                "Структура файла повреждена."
            );
        }

        // Извлекаем CRC32, находящийся между половинами ZLIB.
        int middle = compressedLength / 2;
        int crcOffset = HeaderSize + middle;

        uint storedCrc =
            BinaryPrimitives.ReadUInt32LittleEndian(
                fileData.AsSpan(crcOffset, 4)
            );

        // Собираем сжатые данные обратно.
        byte[] compressedData = new byte[compressedLength];

        Buffer.BlockCopy(
            fileData,
            HeaderSize,
            compressedData,
            0,
            middle
        );

        Buffer.BlockCopy(
            fileData,
            crcOffset + 4,
            compressedData,
            middle,
            compressedLength - middle
        );

        // Проверяем целостность до распаковки.
        uint actualCrc = Crc32.Calculate(
            header,
            compressedData
        );

        if (storedCrc != actualCrc)
        {
            throw new InvalidDataException(
                $"Ошибка CRC32! Ожидалось: {storedCrc:X8}, " +
                $"получено: {actualCrc:X8}. Файл повреждён."
            );
        }

        // Распаковываем ZLIB.
        byte[] rawData = new byte[rawLength];

        using (var memory = new MemoryStream(
            compressedData,
            writable: false))
        using (var zlib = new ZLibStream(
            memory,
            CompressionMode.Decompress))
        {
            int totalRead = 0;

            while (totalRead < rawData.Length)
            {
                int read = zlib.Read(
                    rawData,
                    totalRead,
                    rawData.Length - totalRead
                );

                if (read == 0)
                    break;

                totalRead += read;
            }

            if (totalRead != rawData.Length ||
                zlib.ReadByte() != -1)
            {
                throw new InvalidDataException(
                    "Некорректные распакованные данные."
                );
            }
        }

        return CreateBitmap(width, height, rawData);
    }

    private static void ValidateDimensions(
        int width,
        int height)
    {
        if (width < 1 || height < 1 ||
            width > 16000 || height > 16000)
        {
            throw new InvalidDataException(
                "Недопустимые размеры изображения."
            );
        }

        if ((long)width * height > MaxPixelCount)
        {
            throw new InvalidDataException(
                "Изображение слишком большое."
            );
        }
    }

    private static byte[] ReadBitmapPixels(Bitmap bitmap)
    {
        int rowLength = checked(bitmap.Width * 4);

        byte[] pixels = new byte[
            checked(rowLength * bitmap.Height)
        ];

        var rectangle = new Rectangle(
            0, 0, bitmap.Width, bitmap.Height
        );

        BitmapData data = bitmap.LockBits(
            rectangle,
            ImageLockMode.ReadOnly,
            PixelFormat.Format32bppArgb
        );

        try
        {
            for (int y = 0; y < bitmap.Height; y++)
            {
                Marshal.Copy(
                    IntPtr.Add(data.Scan0, y * data.Stride),
                    pixels,
                    y * rowLength,
                    rowLength
                );
            }
        }
        finally
        {
            bitmap.UnlockBits(data);
        }

        return pixels;
    }

    private static Bitmap CreateBitmap(
        int width,
        int height,
        byte[] pixels)
    {
        var bitmap = new Bitmap(
            width,
            height,
            PixelFormat.Format32bppArgb
        );

        try
        {
            int rowLength = width * 4;

            var rectangle = new Rectangle(
                0, 0, width, height
            );

            BitmapData data = bitmap.LockBits(
                rectangle,
                ImageLockMode.WriteOnly,
                PixelFormat.Format32bppArgb
            );

            try
            {
                for (int y = 0; y < height; y++)
                {
                    Marshal.Copy(
                        pixels,
                        y * rowLength,
                        IntPtr.Add(data.Scan0, y * data.Stride),
                        rowLength
                    );
                }
            }
            finally
            {
                bitmap.UnlockBits(data);
            }

            return bitmap;
        }
        catch
        {
            bitmap.Dispose();
            throw;
        }
    }
}

internal static class Crc32
{
    private static readonly uint[] Table = CreateTable();

    public static uint Calculate(
        ReadOnlySpan<byte> first,
        ReadOnlySpan<byte> second)
    {
        uint crc = 0xFFFFFFFFu;

        foreach (byte value in first)
        {
            crc = Table[(int)((crc ^ value) & 0xFF)]
                  ^ (crc >> 8);
        }

        foreach (byte value in second)
        {
            crc = Table[(int)((crc ^ value) & 0xFF)]
                  ^ (crc >> 8);
        }

        return ~crc;
    }

    private static uint[] CreateTable()
    {
        var table = new uint[256];

        for (uint i = 0; i < 256; i++)
        {
            uint value = i;

            for (int bit = 0; bit < 8; bit++)
            {
                if ((value & 1) != 0)
                {
                    value = 0xEDB88320u ^ (value >> 1);
                }
                else
                {
                    value >>= 1;
                }
            }

            table[(int)i] = value;
        }

        return table;
    }
}
