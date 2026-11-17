# Barnyard Remaster v5.5

A Barnyard PC-változatának közösségi remastere az OpenBarnyard alapján. A játék DirectX 11-es megjelenítőt, magyar felületet és kétjátékos LAN-módot kapott. Windowson közvetlenül indíthatod, SteamOS-en a Windows-programot a Proton futtatja.

**Játékhoz** töltsd le a frissítést, és készítsd elő a saját, telepített Barnyard PC-példányodat. **Fejlesztéshez** a lentebbi fordítási útmutatót kövesd. [Legfrissebb kiadás](https://github.com/mannin1337/barnyard-remaster/releases/latest)

## Telepítés és mentés

Ha lemezen van meg a játék, először telepítsd a Barnyard PC-változatát. A vizsgált alap a kétlemezes PC-kiadás. Keresd meg azt a telepítési mappát, ahol a `Data` könyvtár, a `binkw32.dll` és a `fmod.dll` található. A telepítő innen másolja át az eredeti játékadatokat, valamint a videóhoz és a hanghoz szükséges két DLL-t.

Bontsd ki a frissítőcsomagot, és nyiss terminált az `install.py` mellett. Python 3.8 vagy újabb szükséges. A `--game` után a saját eredeti játékod helyét, a `--output` után egy új vagy üres, írható mappát adj meg. A példákban szereplő útvonalakat cseréld ki a sajátjaidra.

**Windows PowerShell**

```powershell
py -3 install.py --game "C:\Jatekok\Barnyard" --output "C:\Jatekok\BarnyardRemaster"
```

**SteamOS, asztali mód**

```sh
python3 install.py --game "$HOME/Games/Barnyard" --output "$HOME/Games/BarnyardRemaster"
```

Az indítók és az új `OpenBarnyard` könyvtár a célmappába kerülnek. Ha korábbi v5.5-ös remastered is van, add a parancs végéhez a `--previous "régi remaster gyökérmappája"` opciót a felismert HD-erőforrások átvételéhez. Ha annak saját mentését is szeretnéd továbbvinni, előbb lépj ki a játékból, és add hozzá a `--copy-saves` kapcsolót is. Mindkét esetben szükséges az eredeti játékot kijelölő `--game`.

**Kézi mentésmásolat.** Írható telepítésnél a mentés az `OpenBarnyard/Save/savedata.by` fájl. Kilépés után az egész `OpenBarnyard/Save` könyvtárat átmásolhatod az új példány azonos helyére. Ha a régi Windows-telepítés mappája írásvédett volt, a mentést a `%LOCALAPPDATA%\VirtualStore` alatt, a telepítési útvonalat tükröző `OpenBarnyard\Save` könyvtárban keresd.

**HD-textúrák készítése.** Ha saját játékodból állítanád elő a HD-textúrákat, a telepítés után a forráskód gyökerében futtasd az alábbi parancsokat. A `--output` most a már létrehozott remaster mappája. A program alapból egy CPU-szálon, Lanczos-szűréssel nagyít. Saját Real-ESRGAN NCNN Vulkan program és x4plus modell esetén a `--upscaler` kapcsolóval megadhatod a nagyítóprogram helyét.

```powershell
py -3 -m pip install Pillow numpy
py -3 Tools/hd/build_hd.py --game "C:\Jatekok\Barnyard" --output "C:\Jatekok\BarnyardRemaster"
```

## Indítás Windowson

A játék a Windows DirectX 11 összetevőit és a Microsoft Visual C++ v14 futtatókörnyezetét használja. Ha a futtatókörnyezet hiányzik, a Microsoft letöltési oldalán válaszd az **X86** telepítőt. A játék 32 bites, ezért 64 bites Windowson is ez a megfelelő csomag. [Microsoft Visual C++ Redistributable letöltése](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#latest-supported-redistributable-version)

A létrejött remaster mappában indítsd a **Barnyard_inditas.bat** fájlt. Ablakos indításhoz a mappában megnyitott PowerShellben futtasd a `.\Barnyard_inditas.bat -windowed` parancsot. Az indító a megfelelő munkakönyvtárat állítja be, hogy az EXE megtalálja a játékadatokat.

## SteamOS és Steam Deck

1. Asztali módban add hozzá a Steamhez nem Steam-játékként a célmappa **OpenBarnyard/OpenBarnyard.exe** fájlját.
2. A parancsikon **Indítás helye / Start In** mezője a remaster gyökérmappája legyen, ahol a `Data` és az `OpenBarnyard` egymás mellett található. A telepítő az EXE melletti `OpenBarnyard/ShaderCache` mappába teszi a motorhoz előre lefordított 235 shaderfájlt.
3. A **Tulajdonságok → Kompatibilitás** alatt válassz egy telepített hivatalos Proton-verziót. A Steam Input kiosztása legyen **Gamepad**. Első ablakos indításhoz elég a következő indítási beállítás.

   ```sh
   %command% -windowed
   ```

Ha egy shader futás közben fordítást igényel, a Proton beépített D3D-fordítója használható. Ha egy korábbi beállítás másik fordítót kényszerít, a `WINEDLLOVERRIDES="d3dcompiler_47=b" %command% -windowed` indítási sorral ehhez a játékhoz a beépített változatot választhatod.

LAN-hoz a házigazda az indítási sor végére írja a `-nopause -nethost` kapcsolókat. A másik gépen a `-nopause -netjoin` kéri az automatikus keresést. Kézi csatlakozáshoz a `-netjoin` után add meg a házigazda helyi IP-címét.

## LAN két gépen

Mindketten ugyanazt a remasterkiadást indítsátok ugyanazon a helyi hálózaton. Mindkét játékos saját kamerát és helyi mentést használ.

1. Windowson a házigazda indítsa a **Barnyard_LAN_szerver.bat** fájlt. A vendég kattintson duplán a **Barnyard_LAN_csatlakozas.bat** fájlra. Az indító automatikusan megkeresi a házigazdát a helyi hálózaton. Kézi csatlakozáshoz az indító első argumentumaként add meg a házigazda helyi IPv4-címét. A házigazda ezt az `ipconfig` kimenetében az aktív Wi-Fi- vagy Ethernet-kapcsolatnál találja. A játék alapértelmezett portja UDP 27815. A Windows tűzfal kérdésénél a saját, privát hálózatot engedélyezd.
2. A farmon nyissátok meg a szünetmenüt. Egyikőtök a **LAN minijátékok** alatt válasszon játékot, a másik fogadja el a meghívást. A **Következő oldal** gombbal lapozhattok.

| Mód | Játékok | Hogyan játszotok? |
|---|---|---|
| Közös pálya vagy asztal | Sárugrók, Darts, Tanya-biliárd | Sárugrókban egyszerre játszotok két gépi ellenféllel. A darts három fordulójában felváltva dobtok, fordulónként fejenként tíz nyilat. Biliárdban a játék szabályai döntik el, ki lök. |
| Váltott kör nézőképpel | Tyúkröptetés, Mosómedve-püfölő, Ürgegolf | Amíg egyikőtök játszik, a másik gépen az ő körét látjátok. |
| Pontpárbaj | Postásbosszantás, Gülüné bosszantása, Csibehajsza, Veteményesvédő, Tehéndöntés, Tejbár | Ugyanazt a feladatot teljesítitek a saját gépeteken. A helyi menetek végén a játék összehasonlítja a pontokat. |
| Időpárbaj | Bicikliverseny, Örömautózás | Saját futamot teljesítetek. A rövidebb érvényes célidő nyer. |

### Mit ellenőriztünk a v5.5 LAN-javításnál?

A próbák két Windows-játékfolyamattal, eltérő képkockasebességgel, mesterséges késleltetéssel és csomagvesztéssel futottak.

- **Közös eredmény és helyi irányítás.** A Tyúkröptetés, Mosómedve-püfölő, Sárugrók, Darts, Tehéndöntés és Tejbár meccsei a rendes eredményig futottak. Biliárdban 30/120 és 120/30 FPS mellett is mindketten lőttek, a vendég erőjelzője az első helyi képkockán reagált. Dartsban a házigazda mind a 60 vendégoldali tengelyrögzítést az első gombnyomással egyezően fogadta el, 400 ms szimulált oda-vissza késéssel és 2% csomagvesztéssel.
- **Hat játékban a közös indulást és a kapcsolatvesztést teszteltük.** Postásbosszantás, Gülüné bosszantása, Csibehajsza, Veteményesvédő, Bicikliverseny és Örömautózás. A vendég kilépése után a házigazda visszatért a farmra.
- **További teljes meccsek.** A Postásbosszantás, Gülüné bosszantása és a Csibehajsza próbájában mindkét játékos a menet végéig jutott. A Veteményesvédőben mindkét képernyőn 41–20 lett az eredmény. Az Örömautózásban mindkét fél kifutott a játékidőből, és ugyanazt a befejezetlen futamot látta az eredményképernyőn.
- **Ürgegolfban** a házigazda végigjátszotta az első menetet, majd átadta a kört a vendégnek. Mindkét gép nézőként is mutatta a felszínre bújó ürgét és a földnyomát. A próba a vendég körében zárult, mindketten visszatértek a farmra. A második menet vége és a teljes meccs eredménye külön ellenőrzésre vár.

A frissítés fizikai SteamOS–Windows géppáron végigjátszott tesztje még hátravan.

## Fordítás forrásból Windowson

Szükséges a **Git**, a **Python 3.10 vagy újabb**, valamint a **Visual Studio 2022** vagy a **Visual Studio 2022 Build Tools**. A Visual Studio Installerben válaszd az *Asztali fejlesztés C++ használatával / Desktop development with C++* munkaterhelést, az **MSVC v143 x86/x64** eszközöket és a **Windows SDK**-t.

A hang és a videó fordításához a saját **FMOD 3.74** és **Bink 1.9c** fejlesztői csomagod kompatibilis fejléceire és 32 bites importkönyvtáraira van szükség. Helyezd el őket az alábbi szerkezetben. Az `include` könyvtárakba a teljes fejléckészlet kerüljön.

```text
BarnyardSDK/
  bink/include/bink.h
  bink/include/radbase.h
  bink/lib/x86/binkw32.lib
  fmod/include/fmod/fmod.h
  fmod/lib/x86/fmodvc.lib
```

A következő parancsokat PowerShellben futtasd. A `-LegacySdkRoot` útvonala a fenti két SDK közös szülőmappája. Az előkészítő a többi szükséges buildfüggőséget az OpenBarnyard rögzített változatából tölti le, így az első indításhoz internetkapcsolat kell.

```powershell
git clone https://github.com/mannin1337/barnyard-remaster.git
cd barnyard-remaster
.\Build-Remaster.ps1 -LegacySdkRoot "C:\SDK\BarnyardSDK"
```

A szkript a Premake **vs2022** generátorával **x86 / DX11** projektet készít és **Release / Win32** módban fordít. Az eredmény a `bin/Release_Win32_x86_DX11/OpenBarnyard/OpenBarnyard.exe`. Ha Visual Studióban szeretnél dolgozni, a buildparancshoz add hozzá a `-ConfigureOnly` kapcsolót, majd nyisd meg a létrejött `OpenBarnyard.sln` fájlt. Válaszd a **Release** konfigurációt, a **Win32** platformot és az **OpenBarnyard** projektet. Az SDK-útvonalat a `BARNYARD_LEGACY_SDK` környezeti változóban is megadhatod.

A saját build kipróbálásához telepítsd a frissítést külön játékmappába a fenti módon. Kilépés után abban cseréld az `OpenBarnyard/OpenBarnyard.exe` fájlt az általad fordítottra, és a mappa indítóját használd. Az eredeti PC-játék adataira forrásból fordított programnál is szükség van.

## Eredeti fejlesztők és licenc

A Barnyard PC-játékot és a TOSHI motort a **Blue Tongue Entertainment** fejlesztette. A TOSHI jelenlegi jogtulajdonosaként az eredeti licenc a **THQ Nordic** céget nevezi meg. Az OpenBarnyard/OpenToshi visszafejtésének szerzői **AdventureT** és **InfiniteC0re**. Ez a közösségi remaster az ő munkájukra épül.

A forrás felhasználási és terjesztési feltételeit a projekt eredeti licence tartalmazza. Az upstream bemutatkozása, a közreműködői hivatkozások és a külső könyvtárak jegyzéke az eredeti README-ben olvasható. [Licenc](License.md) · [Eredeti OpenBarnyard README](README.upstream.md)
