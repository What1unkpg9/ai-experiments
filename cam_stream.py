import os
import sys
import base64
import socket
import configparser
import datetime
import io
import threading
import time
from functools import wraps

import cv2
import numpy as np
import sounddevice as sd
from flask import Flask, Response, request, render_template_string

# -------------------------------------------------------------------
# 1. Определяем папку рядом с .exe (или со скриптом при разработке)
# -------------------------------------------------------------------
if getattr(sys, 'frozen', False):
    # Если запущено как собранный PyInstaller .exe
    BASE_DIR = os.path.dirname(sys.executable)
else:
    # Если запущено как обычный .py скрипт
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))

CONFIG_PATH = os.path.join(BASE_DIR, "config.ini")

# -------------------------------------------------------------------
# 2. Работа с config.ini (создание при отсутствии)
# -------------------------------------------------------------------
DEFAULT_CONFIG = {
    "auth": {
        "login": "admin",
        "password": "12345",
    },
    "server": {
        "port": "8080",
    }
}

def load_or_create_config():
    config = configparser.ConfigParser()
    if not os.path.exists(CONFIG_PATH):
        config.read_dict(DEFAULT_CONFIG)
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            config.write(f)
        print(f"[INFO] Файл конфигурации не найден. Создан новый: {CONFIG_PATH}")
        print("[INFO] Логин: admin, Пароль: 12345, Порт: 8080")
        print("[INFO] Вы можете изменить эти значения в config.ini и перезапустить программу.")
    else:
        config.read(CONFIG_PATH, encoding="utf-8")
    return config

config = load_or_create_config()
LOGIN = config.get("auth", "login", fallback="admin")
PASSWORD = config.get("auth", "password", fallback="12345")
PORT = config.getint("server", "port", fallback=8080)

# -------------------------------------------------------------------
# 3. Flask приложение и Basic Auth
# -------------------------------------------------------------------
app = Flask(__name__)

def check_auth(auth_header):
    """Проверяет заголовок Authorization."""
    if not auth_header:
        return False
    try:
        auth_type, credentials = auth_header.split(" ", 1)
        if auth_type.lower() != "basic":
            return False
        decoded = base64.b64decode(credentials).decode("utf-8")
        username, password = decoded.split(":", 1)
        return username == LOGIN and password == PASSWORD
    except Exception:
        return False

def requires_auth(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        auth = request.headers.get("Authorization")
        if not check_auth(auth):
            return Response(
                "Authentication required", 401,
                {"WWW-Authenticate": 'Basic realm="CamStream"'}
            )
        return f(*args, **kwargs)
    return decorated

# -------------------------------------------------------------------
# 4. Захват видео и наложение времени
# -------------------------------------------------------------------
camera = cv2.VideoCapture(0, cv2.CAP_DSHOW)  # CAP_DSHOW ускоряет запуск на Windows

def generate_frames():
    """Генератор MJPEG кадров с временной меткой."""
    while True:
        success, frame = camera.read()
        if not success:
            break
        # Наложение даты и времени
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        cv2.putText(frame, timestamp, (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
        # Кодирование в JPEG
        ret, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 80])
        if not ret:
            continue
        frame_bytes = buffer.tobytes()
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')

# -------------------------------------------------------------------
# 5. Захват аудио и потоковая отдача WAV
# -------------------------------------------------------------------
AUDIO_SAMPLE_RATE = 44100
AUDIO_CHANNELS = 1
AUDIO_BLOCKSIZE = 1024

def generate_audio_wav():
    """
    Генерирует WAV-поток с микрофона.
    Заголовок WAV формируется один раз, далее идут сырые PCM-данные.
    """
    # Формируем WAV-заголовок для потока (длительность неизвестна, ставим максимальную)
    # Используем wave-модуль для создания заголовка в памяти.
    import wave
    wav_buffer = io.BytesIO()
    with wave.open(wav_buffer, 'wb') as wav_file:
        wav_file.setnchannels(AUDIO_CHANNELS)
        wav_file.setsampwidth(2)  # 16-bit
        wav_file.setframerate(AUDIO_SAMPLE_RATE)
        # Записываем "бесконечный" заголовок: просто оставляем место
        # (на практике браузер/VLC будут читать поток до обрыва)
        wav_file.writeframes(b'')  # пустые данные, заголовок сформирован
    wav_header = wav_buffer.getvalue()

    # Отдаём заголовок
    yield wav_header

    # Затем бесконечно отдаём аудиоданные с микрофона
    def audio_callback(indata, frames, time_info, status):
        if status:
            print(f"[AUDIO STATUS] {status}", file=sys.stderr)
        # indata — numpy array, конвертируем в 16-bit PCM
        pcm = (indata * 32767).astype(np.int16).tobytes()
        # Кладём в очередь (используем простой список как буфер)
        audio_queue.append(pcm)

    # Очередь для аудиоданных
    audio_queue = []

    # Запускаем поток
    stream = sd.InputStream(
        samplerate=AUDIO_SAMPLE_RATE,
        channels=AUDIO_CHANNELS,
        dtype='float32',
        blocksize=AUDIO_BLOCKSIZE,
        callback=audio_callback
    )
    stream.start()

    try:
        while True:
            if audio_queue:
                chunk = audio_queue.pop(0)
                yield chunk
            else:
                time.sleep(0.01)
    finally:
        stream.stop()
        stream.close()

# -------------------------------------------------------------------
# 6. HTML-страница
# -------------------------------------------------------------------
HTML_PAGE = """
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>CamStream</title>
    <style>
        body { margin: 0; background: #111; color: #eee; font-family: sans-serif; text-align: center; }
        h1 { font-size: 1.2rem; padding: 10px; }
        img { max-width: 100%; height: auto; border: 1px solid #333; }
        audio { width: 100%; margin-top: 10px; }
        .container { max-width: 800px; margin: 0 auto; padding: 10px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>CamStream — локальная IP-камера</h1>
        <img src="/video" alt="Видеопоток">
        <audio controls src="/audio"></audio>
    </div>
</body>
</html>
"""

# -------------------------------------------------------------------
# 7. Маршруты
# -------------------------------------------------------------------
@app.route("/")
@requires_auth
def index():
    return render_template_string(HTML_PAGE)

@app.route("/video")
@requires_auth
def video():
    return Response(generate_frames(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route("/audio")
@requires_auth
def audio():
    return Response(generate_audio_wav(),
                    mimetype='audio/wav')

# -------------------------------------------------------------------
# 8. Определение локального IP и вывод информации
# -------------------------------------------------------------------
def get_local_ip():
    """Возвращает локальный IPv4 адрес."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

if __name__ == "__main__":
    local_ip = get_local_ip()
    print("=" * 60)
    print("CamStream запущен!")
    print(f"Логин: {LOGIN}  Пароль: {PASSWORD}")
    print(f"Браузер (видео + звук): http://{local_ip}:{PORT}/")
    print(f"VLC (только видео):     http://{LOGIN}:{PASSWORD}@{local_ip}:{PORT}/video")
    print("Для остановки нажмите Ctrl+C или закройте это окно.")
    print("=" * 60)
    app.run(host="0.0.0.0", port=PORT, threaded=True)
