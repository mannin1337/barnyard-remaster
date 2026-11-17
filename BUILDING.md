# A v5.5 forráskiadás fordítása

A támogatott kiadási cél a 32 bites Windows-program Direct3D 11-gyel. SteamOS-on ez a program fut Proton alatt.

## Windows

1. Telepíts Python 3.10 vagy újabb verziót és Visual Studio 2022-t, illetve a Build Tools változatát. Válaszd a C++ asztali fejlesztési csomagot, az x86/x64 MSVC v143 fordítót és a Windows SDK-t. [Python](https://www.python.org/downloads/windows/) · [Microsoft C++ Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
2. Készítsd elő a saját, jogosultan használt régi PC-s fejlesztőkészleteidet. A közös könyvtáruk alatt az alábbi szerkezetet várja a fordító. A verziószámok a kiadáshoz használt fejlécek verziószámai. A hivatkozások a tulajdonosok termékoldalai. [Firelight FMOD](https://www.fmod.com/download) · [RAD Bink](https://www.radgametools.com/bnkmain.htm)

```text
<SDK-mappa>/
  fmod/include/fmod/fmod.h      FMOD 3.74
  fmod/lib/x86/fmodvc.lib
  bink/include/bink.h           Bink 1.9c
  bink/include/radbase.h
  bink/lib/x86/binkw32.lib
```

3. A repó gyökeréből indítsd el a fordítást PowerShellben.

```powershell
.\Build-Remaster.ps1 -LegacySdkRoot 'sajat-sdk-mappa'
```

A szkript az upstream commitból előkészíti a függőségeket, minden fájlhoz ellenőrzi a Git blobazonosítót, majd létrehozza az `OpenBarnyard.sln` megoldást. Egyszerre legfeljebb két fordítószálat használ. Az EXE a `bin/Release_Win32_x86_DX11/OpenBarnyard/OpenBarnyard.exe` útvonalra kerül. Futtatásához az eredeti PC-játék adatai és a hozzájuk tartozó futásidejű könyvtárak szükségesek.

A `BARNYARD_LEGACY_SDK` környezeti változóval is megadható az SDK-k mappája. A `-ConfigureOnly` csak a projektgenerálásig fut. Az opcionális `-UpstreamDirectory 'sajat-upstream-git-mappa'` egy meglévő helyi Git-klónból tölti be a rögzített fájlokat, így offline is lehet fordítani. A klón objektumtárának tartalmaznia kell a rögzített commitot. [Upstream revízió](https://github.com/InfiniteC0re/OpenBarnyard/tree/f359e963c9256c608951b863a16c2eefaa352823)

## Magyar szövegek és feliratok

```powershell
py -3 -X utf8 -B RemasterTools/Locale/build_hungarian.py --data 'sajat-jatek/Data' --out 'magyar-kimenet'
```

A kimenethez adj új mappanevet. A program a magyar forrásszövegekből, a v5.5 felületének kiegészítéseiből és a feliratok időzítéséből elkészíti a `Data/Locale`, `Data/Subs` és `Data/ScriptedSequence` megfelelő fájljait. Az eredeti fájlokból a saját játéktelepítést olvassa.

A HD textúrák eszköze a `RemasterTools/Textures/hd_textures.py`. A betűkészletek elemzését és a már mellékelt karakterelhelyezési tábla előállítását a `DevTools/HDFonts/font_tool.py` és `gen_layout.py` végzi. Mindkét képfeldolgozó eszköz Pillow-t használ. A táblagenerátorhoz NumPy, SciPy és fontTools is kell. A TTF-et és a képi bemeneteket a fejlesztő adja meg. A kész tábla a motorral együtt fordul.

## Eredet és licencek

A Toshi és a 2006-os játék eredeti fejlesztője a Blue Tongue Entertainment. A Toshi jelenlegi jogosultjaként az upstream licenc a THQ Nordicot nevezi meg. Az OpenBarnyard dekompiláció szerzői AdventureT és InfiniteC0re. A módosított forrásra az eredeti projekt nem kereskedelmi licencfeltételei vonatkoznak. [Eredeti licenc](License.md) · [Upstream projektleírás](README.upstream.md) · [OpenBarnyard](https://github.com/InfiniteC0re/OpenBarnyard)

| Összetevő | Eredet és felhasználási feltételek |
|---|---|
| Premake | Projektgenerátor. [Projekt](https://github.com/premake/premake-core) · [Mellékelt licenc](Vendor/premake/LICENSE.txt) |
| PooledStrings | Az OpenBarnyard C# kódgenerátora. [Forráskód](DevTools/PooledStrings/PooledStrings.cs) · [Upstream licenc](License.md) |
| Dear ImGui | Omar Cornut és a közreműködők. [Projekt](https://github.com/ocornut/imgui) · [MIT licenc](BuildTools/licenses/Dear-ImGui.txt) |
| DirectXTex | Microsoft. [Projekt](https://github.com/microsoft/DirectXTex) · [MIT licenc](BuildTools/licenses/DirectXTex.txt) |
| MinHook | Tsuda Kageyu és a közreműködők. [Projekt](https://github.com/TsudaKageyu/minhook) · [A használt verzió licence](https://github.com/InfiniteC0re/OpenBarnyard/blob/f359e963c9256c608951b863a16c2eefaa352823/Toshi/Vendor/minhook/LICENSE.txt) |
| OPCODE 1.3 | Pierre Terdiman ütközésvizsgáló könyvtára. [A szerző kiadása és felhasználási engedélye](https://www.codercorner.com/Opcode.htm) |
| stb_image és stb_image_write | Sean Barrett és a közreműködők. A feltételek a letöltött fejlécek végén szerepelnek. [Projekt](https://github.com/nothings/stb) |

A Windows SDK-t és az MSVC-t a Microsoft telepítője biztosítja. A Bink és az FMOD a fejlesztő által megadott SDK-mappából érhető el. A letöltött nyilvános függőségek fájlneve és rögzített azonosítója is megtalálható a jegyzékben. [Függőségi jegyzék](BuildTools/upstream-files.json)
