# LAN regression tests

These tests use the game's input protocol, spectator code and scorecard formatter. They run without starting Barnyard or loading retail assets. You need **CMake 3.20 or later** and a **C++17 compiler**.

## Build and run

On Linux, use the system's C++ compiler and run these commands from the repository root. The tests run directly on Linux and do not require Steam or Proton. CTest prints each program's measurements and the final pass count.

```sh
cmake -S Tools/tests/netplay -B build/netplay -DCMAKE_BUILD_TYPE=Release
cmake --build build/netplay
ctest --test-dir build/netplay -V
```

To match the game's 32-bit Windows architecture, use a fresh build directory and select the VS 2022 Win32 generator explicitly.

```powershell
cmake -S Tools/tests/netplay -B build/netplay-win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/netplay-win32 --config Release
ctest --test-dir build/netplay-win32 -C Release -V
```

## Test coverage

| Test | What it checks |
|---|---|
| `view_cadence` | The host publishes about 30 snapshots per second at 30, 59.94, 60 and 144 FPS and with uneven frame times. After a long stall, one update is due. |
| `darts_view_input` | A guest click carries the chosen snapshot ID to the host and can be consumed once. The test covers a fresh click after an invalid reference, expired input, mismatched matches or peers, and the legacy 28-byte input format. |
| `golf_ground` | The spectator can decode the position, heading, animation and progress of 25 gopher-trail and mound pieces. A later snapshot clears stale pieces after a dropped update. |
| `chicken_motion` | The bird and camera continue moving between 30 Hz snapshots at 120 FPS. The test exercises a missed snapshot, a pause in the stream and a change of match. |
| `minigame_names` | Names follow the host and guest when their roles swap. The test checks English and Hungarian scorecards, winners, times, custom translations and names containing slot markers or percent signs. |
