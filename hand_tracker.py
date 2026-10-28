"""Local webcam-to-Garry's Mod hand tracker over WebSocket."""
import asyncio
import json
import threading
import time
from pathlib import Path

import cv2
import mediapipe as mp
from websockets.asyncio.server import serve

HOST = "127.0.0.1"
PORT = 8765
CAMERA_ID = 0
SCALE = 100.0
SEND_FPS = 30
SMOOTHING = 0.45

latest_lock = threading.Lock()
latest_packet = {"detected": False, "points": []}


def set_packet(detected, points):
    global latest_packet
    with latest_lock:
        latest_packet = {"detected": detected, "points": points}


async def handle_connection(websocket):
    print("[WebSocket] GMod connected")
    try:
        while True:
            with latest_lock:
                packet = latest_packet
            await websocket.send(json.dumps(packet, separators=(",", ":")))
            await asyncio.sleep(1 / SEND_FPS)
    except Exception as exc:
        print("[WebSocket] Client disconnected:", type(exc).__name__)


async def ws_main():
    async with serve(handle_connection, HOST, PORT, max_size=65536):
        print(f"[WebSocket] Listening on ws://{HOST}:{PORT}")
        await asyncio.Future()


def start_ws():
    try:
        asyncio.run(ws_main())
    except OSError as exc:
        print("[WebSocket] Could not start:", exc)


def main():
    threading.Thread(target=start_ws, daemon=True).start()
    camera = cv2.VideoCapture(CAMERA_ID, cv2.CAP_DSHOW)
    if not camera.isOpened():
        camera.release()
        camera = cv2.VideoCapture(CAMERA_ID)
    if not camera.isOpened():
        print("[Camera] Camera not available (close other camera apps)")
        input("Press Enter to exit...")
        return

    camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    mp_hands = mp.solutions.hands
    mp_draw = mp.solutions.drawing_utils
    prev = None
    print("[Camera] Tracking; press Q in camera window to exit")
    try:
        with mp_hands.Hands(static_image_mode=False, max_num_hands=1,
                            model_complexity=1, min_detection_confidence=0.65,
                            min_tracking_confidence=0.6) as hands:
            while True:
                ok, frame = camera.read()
                if not ok:
                    print("[Camera] Read failed")
                    set_packet(False, [])
                    break
                frame = cv2.flip(frame, 1)
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                rgb.flags.writeable = False
                result = hands.process(rgb)
                points = []
                if result.multi_hand_landmarks:
                    landmarks = result.multi_hand_landmarks[0]
                    for i, p in enumerate(landmarks.landmark):
                        position = [(p.x - 0.5) * SCALE,
                                    (0.5 - p.y) * SCALE,
                                    -p.z * SCALE]
                        if prev is not None:
                            position = [a + (b - a) * SMOOTHING
                                        for a, b in zip(prev[i], position)]
                        points.append(position)
                    prev = points
                    mp_draw.draw_landmarks(frame, landmarks, mp_hands.HAND_CONNECTIONS)
                else:
                    prev = None
                set_packet(bool(points), points)
                cv2.putText(frame, "HAND DETECTED" if points else "NO HAND",
                            (12, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.8,
                            (0, 255, 0) if points else (0, 0, 255), 2)
                cv2.imshow("GMod Hand Tracker - Q to quit", frame)
                if cv2.waitKey(1) & 0xFF == ord('q'):
                    break
    finally:
        set_packet(False, [])
        camera.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
