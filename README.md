# GMod Hand Tracker

## Build EXE on GitHub (no local Python required)

1. Create a GitHub repository and upload `hand_tracker.py`, `requirements.txt` and `.github/workflows/build.yml` preserving the directory structure.
2. Open **Actions** → **Build GMod Hand Tracker** → **Run workflow**.
3. When completed, open the run → **Artifacts** → **GModHandTracker-Windows**.
4. Extract `GModHandTracker.exe` from the downloaded ZIP and run it on **your own PC**, with the webcam attached. Press `Q` in the camera window to exit.

The compiled EXE may be large and may take longer to start when using `--onefile`. Microsoft Visual C++ runtime may still be required on Windows.

## Install GLua

Copy `lua/autorun/client/cl_virtualhand.lua` to `<GMod>/garrysmod/lua/autorun/client/cl_virtualhand.lua`. Start a singleplayer map, keep the EXE running, and look in the console for `[VirtualHand] Connected`. Requires a GMod DHTML engine that supports WebSocket access to local `ws://` addresses; this bridge may be restricted in some game versions. The 21 client-only cubes appear in front of your view when tracking succeeds.

## Privacy

The tracker listens only on `127.0.0.1:8765`. It does not upload camera frames to GitHub; Actions only compiles source code. Camera processing happens on your PC, while network packets contain only landmark coordinates.
