# Build Barnyard Remaster from source

Build the game on Windows for **Win32 / Release / Direct3D 11**. The resulting executable also runs through Proton on SteamOS. To play the published version with a friend, start with the [installation and co-op guide](RemasterTools/Patch/INSTALL.md).

## Prepare the build tools

Install Git, Python 3.10 or later and either Visual Studio 2022 or Build Tools for Visual Studio 2022. In Visual Studio Installer, select **Desktop development with C++**, **MSVC v143 x86/x64 build tools**, and a **Windows SDK**. [Git for Windows](https://git-scm.com/downloads/win) · [Python for Windows](https://www.python.org/downloads/windows/) · [Microsoft C++ Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)

Provide your licensed legacy FMOD and Bink development kits in a single SDK directory. The release build used FMOD 3.74 and Bink 1.9c. Keep the complete header sets together with the 32-bit import libraries. The paths below are relative to the directory you will pass to the build script. [FMOD](https://www.fmod.com/download) · [RAD Bink](https://www.radgametools.com/bnkmain.htm)

```text
BarnyardSDK/
  fmod/include/fmod/fmod.h
  fmod/lib/x86/fmodvc.lib
  bink/include/bink.h
  bink/include/radbase.h
  bink/lib/x86/binkw32.lib
```

## Build and run

Clone the source, then run the build from the repository root in PowerShell. Replace the SDK path with your own.

```powershell
git clone https://github.com/mannin1337/barnyard-remaster.git
cd barnyard-remaster
.\Build-Remaster.ps1 -LegacySdkRoot 'C:\SDK\BarnyardSDK'
```

The script fetches the other build dependencies from a pinned OpenBarnyard revision, checks their Git blob IDs, generates `OpenBarnyard.sln` with Premake, and compiles the game with up to two compiler processes. The executable is **`bin/Release_Win32_x86_DX11/OpenBarnyard/OpenBarnyard.exe`**. To try it, create a separate playable installation from your original PC game using the patch, replace `OpenBarnyard/OpenBarnyard.exe` in that installation with your build, and run the launcher at its root. [Installation guide](RemasterTools/Patch/INSTALL.md)

| Build option | When to use it |
|---|---|
| `-ConfigureOnly` | Generate the Visual Studio solution. Open it, select **Release / Win32**, and build the **OpenBarnyard** project. |
| `-UpstreamDirectory 'C:\Source\OpenBarnyard'` | Use a local upstream Git clone for an offline build. Its object database must contain the pinned revision. [Pinned upstream revision](https://github.com/InfiniteC0re/OpenBarnyard/tree/f359e963c9256c608951b863a16c2eefaa352823) |
| `BARNYARD_LEGACY_SDK` | Set this environment variable to the SDK directory to reuse it across builds without passing `-LegacySdkRoot` each time. |

To exercise the LAN input and spectator code separately, use the four standalone [netplay regression tests](Tools/tests/netplay/README.md).

## Optional Hungarian text and subtitles

The release installer already builds these files from the player's copy of the game. To rebuild them separately, run the following from the repository root. Pass the original game's **Data** directory and a **new** output directory outside it.

```powershell
py -3 -X utf8 -B RemasterTools/Locale/build_hungarian.py --data 'C:\Games\Barnyard\Data' --out 'C:\Games\Barnyard-Hungarian'
```

The tool combines the Hungarian source strings, interface additions and subtitle timings with local game data. The output contains the translated files under `Data/Locale`, `Data/Subs` and `Data/ScriptedSequence`. Players can select **Magyar** under **Options → Language** in the main menu.

## Textures and fonts

For release-style textures, use the [HD texture builder](Tools/hd/README.md). The separate texture utility is `RemasterTools/Textures/hd_textures.py`. The font analysis and layout tools are `DevTools/HDFonts/font_tool.py` and `DevTools/HDFonts/gen_layout.py`. They use Pillow, and the layout generator also needs NumPy, SciPy and fontTools. Generating a new layout requires your own TTF and image inputs. The engine compiles the supplied layout table during a normal build.

## Credits and licenses

Blue Tongue Entertainment developed the original 2006 game and TOSHI engine. The upstream license identifies THQ Nordic as the current owner of TOSHI. AdventureT and InfiniteC0re created the OpenBarnyard decompilation. Its non-commercial license applies to the modified source. [Original license](License.md) · [Upstream README](README.upstream.md) · [OpenBarnyard](https://github.com/InfiniteC0re/OpenBarnyard)

| Component | Credit and license |
|---|---|
| Premake | Project generator. [Project](https://github.com/premake/premake-core) · [Bundled license](Vendor/premake/LICENSE.txt) |
| PooledStrings | OpenBarnyard's C# code generator. [Source](DevTools/PooledStrings/PooledStrings.cs) · [Upstream license](License.md) |
| Dear ImGui | Omar Cornut and contributors. [Project](https://github.com/ocornut/imgui) · [MIT license](BuildTools/licenses/Dear-ImGui.txt) |
| DirectXTex | Microsoft. [Project](https://github.com/microsoft/DirectXTex) · [MIT license](BuildTools/licenses/DirectXTex.txt) |
| MinHook | Tsuda Kageyu and contributors. [Project](https://github.com/TsudaKageyu/minhook) · [License for the pinned version](https://github.com/InfiniteC0re/OpenBarnyard/blob/f359e963c9256c608951b863a16c2eefaa352823/Toshi/Vendor/minhook/LICENSE.txt) |
| OPCODE 1.3 | Pierre Terdiman's collision detection library. [Author's distribution and terms](https://www.codercorner.com/Opcode.htm) |
| stb_image and stb_image_write | Sean Barrett and contributors. The license text is at the end of the downloaded headers. [Project](https://github.com/nothings/stb) |

The Microsoft installer supplies MSVC and the Windows SDK. Bink and FMOD are read from the developer's SDK directory. The public dependency manifest lists the path and pinned ID of each file the build downloads. [Dependency manifest](BuildTools/upstream-files.json)
