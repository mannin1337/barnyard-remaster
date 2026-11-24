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
| `coop_minigame_protocol` | Exercises matchmaking, both players' ready flags, authority for each minigame, turn changes, results and the return to the farm. |
| `view_cadence` | Both negotiated snapshot rates keep their cadence across different render rates. A 30 FPS sender produces at most one snapshot per frame. A stall sends only the latest pose. |
| `darts_view_input` | Checks both local aim locks, press order, host validation and the throw animation. Includes duplicate, expired and wrong-match clicks and legacy inputs. |
| `golf_ground` | The spectator can decode the position, heading, animation and progress of 25 gopher-trail and mound pieces. A later snapshot clears stale pieces after a dropped update. |
| `chicken_motion` | The bird and camera continue moving between 30 Hz snapshots at 120 FPS. The test exercises a missed snapshot, a pause in the stream and a change of match. |
| `minigame_names` | Names follow the host and guest when their roles swap. The test checks English and Hungarian scorecards, winners, times, custom translations and names containing slot markers or percent signs. |
| `input_delivery` | The host receives the guest's selected Darts aim, Pool shot power, direction and cue-ball placement across packet loss and reordering. Checks single consumption, older wire formats, immediate local scores and rejoining a restarted host. |
| `mud_local_input` | Exercises jumps, kicks, rejected moves and reconciliation after delayed host updates at several frame rates. |
| `pool_local_shot` | Checks local aim, cue movement and the cue ball's first movement while the guest waits for the authoritative table. |
| `whack_input` | Verifies single-axis mouse movement, the selected hole and stray release events during turn changes. |
| `raw_mouse_frame` (Windows) | Queues two synthetic raw mouse events for a hidden window and checks the frame that consumes them. Covers focus, close and quit ordering. |

For an in-game measurement, add `-devinputlatency` to the launch options on each computer. While a LAN minigame is running, each process writes `[input-latency]` entries to its own game log. Match an input ID across `dispatch`, `action` and `submit` to see when the game received the input, first changed the native game state, and submitted the frame. Frame numbers and timestamps can be compared within one player's log. Physical controller polling, scan-out and display response need a separate hardware measurement.
