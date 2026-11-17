# Install Barnyard Remaster and play co-op

For two-player LAN co-op, install the same roughly 4 MB patch on both computers and connect them to the same local network. Each player needs an installed copy of the original Barnyard PC game. The same patch serves Windows and SteamOS.

## Install the patch

The installer checks the contents of the original game's `Data` directory and its `binkw32.dll` and `fmod.dll`. The verified edition is the 2006 two-disc Windows release, whose installer uses `Barnyard.msi`, `Data1.cab`, `Data11.cab` and `Data2.cab`. Another pressing or language edition will work if the 5,393 game data files and both DLLs match.

Download the release ZIP, extract it completely, and open a terminal in the extracted directory containing `install.py`. Use Python 3.8 or later. For `--game`, supply the original game's installation folder. For `--output`, choose a new or empty directory outside the original game, previous remaster and extracted patch. Replace the example paths with your own. [Latest release](https://github.com/mannin1337/barnyard-remaster/releases/latest)

**Windows PowerShell**

```powershell
py -3 install.py --game "C:\Games\Barnyard" --output "C:\Games\BarnyardRemaster"
```

**SteamOS or Linux terminal**

```sh
python3 install.py --game "$HOME/Games/Barnyard" --output "$HOME/Games/BarnyardRemaster"
```

You can copy your original game's installation directory from your Windows PC for the SteamOS installation. To check the inputs and generated file hashes before installing, add `--verify-only` to the command. The installer reports the path of any file that fails verification.

## Play together on Windows

| What you want to do | From the new remaster folder |
|---|---|
| Host a LAN game | Double-click `Host_LAN.bat`. |
| Join your friend's game | Double-click `Join_LAN.bat` to discover the host automatically. For a direct connection, run `.\Join_LAN.bat 192.0.2.10` in PowerShell, replacing the example address with the host's LAN IPv4 address. |
| Open the regular game menu | Double-click `Play_Barnyard.bat`. In the main menu, **Co-op** offers **Host LAN Game**, **Join LAN Game** and **Same Screen**. For local co-op on one computer, connect a controller for the second player and choose **Same Screen**. |

The LAN host can find the active Wi-Fi or Ethernet adapter's IPv4 address with `ipconfig`. The default game port is UDP 27815. Once both players are on the farm, open the pause menu and choose **LAN Minigames**. One player selects a game and the other accepts the invitation.

For a windowed game, run `.\Play_Barnyard.bat -windowed` from the remaster folder in PowerShell.

If Windows reports a missing `MSVCP140.dll` or `VCRUNTIME140.dll`, install the Microsoft Visual C++ v14 **x86** runtime. The game is a 32-bit executable on both 32-bit and 64-bit Windows. [Microsoft download guide](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) · [Official x86 installer](https://aka.ms/vc14/vc_redist.x86.exe)

## Play together on SteamOS with Proton

1. In Desktop Mode, add the new `OpenBarnyard/OpenBarnyard.exe` to Steam as a **Non-Steam Game**.
2. Open the shortcut's **Properties**. Set **Start In** to the remaster root containing both `Data` and `OpenBarnyard`. Under **Compatibility**, select an installed Proton version. If using Steam Input, choose a gamepad layout.
3. Set **Launch Options** according to the role of this computer.

| Role | Steam launch options |
|---|---|
| Host | `%command% -nopause -nethost` |
| Guest using automatic discovery | `%command% -nopause -netjoin` |
| Guest using the host's address | `%command% -nopause -netjoin 192.0.2.10` (replace the example IP) |

For the normal main menu, leave Launch Options empty. For a windowed launch, add `-windowed` after `%command%`. The installer places the two DLLs from your own game and 235 precompiled DX11 shader files alongside the executable or in its `ShaderCache` directory. Proton supplies its own D3D compiler if the game needs to compile a shader at runtime. If the shortcut reports a missing Visual C++ runtime, install Microsoft's x86 redistributable in that shortcut's Proton prefix. [Proton runtime options](https://github.com/ValveSoftware/Proton#runtime-config-options)

If an older shortcut forces a different D3D compiler, `WINEDLLOVERRIDES="d3dcompiler_47=b" %command% -windowed` selects Proton's built-in one for this game.

## Carry over a previous v5.5 installation

Pass `--previous` to copy recognized v5.5 HD textures. Add `--copy-saves` to copy your old remaster's `OpenBarnyard/Save` folder as well. Keep `--game` pointed at the original PC installation and choose a separate output directory.

```powershell
py -3 install.py --game "C:\Games\Barnyard" --previous "C:\Games\BarnyardRemaster-Old" --copy-saves --output "C:\Games\BarnyardRemaster"
```

To transfer saves by hand, close the game and copy the old `OpenBarnyard/Save` folder, including `savedata.by`, to the same place in the new installation. If Windows kept the old game in a protected location such as Program Files, look under `%LOCALAPPDATA%\VirtualStore` for a matching installation path ending in `OpenBarnyard\Save`.

A fresh installation defaults to **English (UK)** and uses the original game's textures and fonts. The game keeps an existing language preference. To use the optional Hungarian interface and subtitles, open **Options → Language → Magyar** from the main menu and restart the game if prompted. You can also [build HD textures locally](https://github.com/mannin1337/barnyard-remaster/tree/main/Tools/hd).
