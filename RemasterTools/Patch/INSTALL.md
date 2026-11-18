# Install Barnyard Remaster and play co-op

Windows and Linux use the same roughly 4 MB patch. It installs the 32-bit Windows game, which you can launch through Steam's Proton compatibility tool on Linux, including SteamOS. For LAN co-op, each player needs their own installed copy of the original Barnyard PC game, the same patch release and a connection to the same local network.

## Install the patch

The installer checks the contents of the original game's `Data` directory and its `binkw32.dll` and `fmod.dll`. The verified edition is the 2006 two-disc Windows release, whose installer uses `Barnyard.msi`, `Data1.cab`, `Data11.cab` and `Data2.cab`. Another pressing or language edition will work if the 5,393 game data files and both DLLs match.

Download the release ZIP, extract it completely, and open a terminal in the extracted directory containing `install.py`. Use Python 3.8 or later. For `--game`, supply the original game's installation folder. For `--output`, choose a new or empty directory outside the original game, previous remaster and extracted patch. Replace the example paths with your own. [Latest release](https://github.com/mannin1337/barnyard-remaster/releases/latest)

**Windows PowerShell**

```powershell
py -3 install.py --game "C:\Games\Barnyard" --output "C:\Games\BarnyardRemaster"
```

**Linux terminal (on Steam Deck, use Desktop Mode)**

```sh
python3 install.py --game "$HOME/Games/Barnyard" --output "$HOME/Games/BarnyardRemaster"
```

On Linux, you can use a copy of the original game's installation directory from your Windows PC. Run the Python installer in your Linux terminal. To check the inputs and generated file hashes before installing, add `--verify-only` to the command. The installer reports the path of any file that fails verification.

## Play together on Windows

| What you want to do | From the new remaster folder |
|---|---|
| Host a LAN game | Double-click `Host_LAN.bat`. |
| Join your friend's game | Double-click `Join_LAN.bat` to discover the host automatically. For a direct connection, run `.\Join_LAN.bat 192.0.2.10` in PowerShell, replacing the example address with the host's LAN IPv4 address. |
| Open the regular game menu | Double-click `Play_Barnyard.bat`. In the main menu, **Co-op** offers **Host LAN Game**, **Join LAN Game** and **Same Screen**. For local co-op on one computer, connect a controller for the second player and choose **Same Screen**. |

The LAN host can find the active Wi-Fi or Ethernet adapter's IPv4 address with `ipconfig`. The default game port is UDP 27815. Once both players are on the farm, open the pause menu and choose **LAN Minigames**. One player selects a game and the other accepts the invitation.

For a windowed game, run `.\Play_Barnyard.bat -windowed` from the remaster folder in PowerShell.

If Windows reports a missing `MSVCP140.dll` or `VCRUNTIME140.dll`, install the Microsoft Visual C++ v14 **x86** runtime. The game is a 32-bit executable on both 32-bit and 64-bit Windows. [Microsoft download guide](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) · [Official x86 installer](https://aka.ms/vc14/vc_redist.x86.exe)

## Play on Linux with Steam and Proton

1. Open the Steam desktop client on your Linux PC. On Steam Deck, choose **Steam → Power → Switch to Desktop** first. In Steam, select **Games → Add a Non-Steam Game to My Library**, browse to the installed `OpenBarnyard/OpenBarnyard.exe`, and add it. [Valve's shortcut guide](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC)
2. In your library, open the new shortcut's **Properties**. Leave **Target** pointing at the executable. Set **Start In** to the full path of the remaster folder containing both `Data` and `OpenBarnyard`, for example `/home/alex/Games/BarnyardRemaster`. Substitute your own installation path.
3. Under **Compatibility**, enable **Force the use of a specific Steam Play compatibility tool** and select a Valve Proton version. If using Steam Input, choose a gamepad layout. You can now launch the shortcut from your library, including in Gaming Mode on Steam Deck. [Valve's Proton guide](https://github.com/ValveSoftware/Proton#introduction)
4. To connect directly at launch, enter the appropriate **Launch Options** on each computer.

| Role | Steam launch options |
|---|---|
| Host | `%command% -nopause -nethost` |
| Guest using automatic discovery | `%command% -nopause -netjoin` |
| Guest using the host's address | `%command% -nopause -netjoin 192.0.2.10` (replace the example IP) |

Leave Launch Options empty to host, join or choose **Same Screen** from the game's **Co-op** menu. For a direct LAN connection, the Linux host can find its local IPv4 address under the active Wi-Fi or Ethernet interface with `ip -4 addr`. The default game port is UDP 27815. For a windowed launch, add `-windowed` after `%command%`.

The installer puts the two original game DLLs beside the executable and 235 precompiled DX11 shaders in `OpenBarnyard/ShaderCache`. Proton supplies its own D3D compiler if the game needs to compile a shader at runtime. If the shortcut reports a missing Visual C++ runtime, install Microsoft's x86 redistributable in that shortcut's Proton prefix. [Proton runtime options](https://github.com/ValveSoftware/Proton#runtime-config-options)

If an older shortcut forces a different D3D compiler, `WINEDLLOVERRIDES="d3dcompiler_47=b" %command% -windowed` selects Proton's built-in one for this game.

## Choose your co-op names

LAN uses the cow name from each player's save. To display a different name for a session, add `-coopname` on that player's computer. For couch co-op, `-coopname2` names the second player on the same screen. These options leave the names in your saves intact. Put names containing spaces in double quotes.

| Example | Windows PowerShell | Steam launch options on Linux |
| --- | --- | --- |
| Host as Bimbo | `.\Host_LAN.bat -coopname Bimbo` | `%command% -nopause -nethost -coopname Bimbo` |
| Join as Gyurcsany | `.\Join_LAN.bat -coopname Gyurcsany` | `%command% -nopause -netjoin -coopname Gyurcsany` |
| Share a screen as Bimbo and Gyurcsany | `.\Play_Barnyard.bat -coopname Bimbo -coopname2 Gyurcsany` | `%command% -coopname Bimbo -coopname2 Gyurcsany` |

For the last example, choose **Co-op → Same Screen** in the main menu. For a direct LAN connection, put the host's IPv4 address immediately after `-netjoin` in Steam or immediately after `.\Join_LAN.bat` in PowerShell.

## Carry over a previous v5.5 installation

Pass `--previous` to copy recognized v5.5 HD textures. Add `--copy-saves` to copy your old remaster's `OpenBarnyard/Save` folder as well. Keep `--game` pointed at the original PC installation and choose a separate output directory.

```powershell
py -3 install.py --game "C:\Games\Barnyard" --previous "C:\Games\BarnyardRemaster-Old" --copy-saves --output "C:\Games\BarnyardRemaster"
```

To transfer saves by hand, close the game and copy the old `OpenBarnyard/Save` folder, including `savedata.by`, to the same place in the new installation. If Windows kept the old game in a protected location such as Program Files, look under `%LOCALAPPDATA%\VirtualStore` for a matching installation path ending in `OpenBarnyard\Save`.

A fresh installation defaults to **English (UK)** and uses the original game's textures and fonts. The game keeps an existing language preference. To use the optional Hungarian interface and subtitles, open **Options → Language → Magyar** from the main menu and restart the game if prompted. You can also [build HD textures locally](https://github.com/mannin1337/barnyard-remaster/tree/main/Tools/hd).
