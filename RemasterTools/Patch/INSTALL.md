# Barnyard Remaster v5.5 telepítése

A frissítés a saját, telepített Barnyard PC-játékodból készít külön játékmappát. Az ellenőrzött alap a 2006-os kétlemezes Windows-kiadás, amelynek telepítője a `Barnyard.msi`, `Data1.cab`, `Data11.cab` és `Data2.cab` fájlokat használja. Az adott kiadás mind az 5393 adatfájljának és mindkét játék-DLL-jének egyeznie kell. Más nyelvű vagy újracsomagolt kiadás kompatibilitása a fájlok tartalmától függ.

Telepítsd az eredeti PC-játékot, majd bontsd ki teljesen a frissítés ZIP-jét. Nyiss terminált a kibontott `install.py` mellett. A példákban cseréld ki az útvonalakat a sajátjaidra. Python 3.8 vagy újabb szükséges.

## Windows

```powershell
py -3 install.py --game "C:\Jatekok\Barnyard" --output "C:\Jatekok\BarnyardRemaster"
```

Az elkészült mappában a `Barnyard_inditas.bat` indítja a játékot. LAN-hoz az egyik gépen a `Barnyard_LAN_szerver.bat`, a másikon a `Barnyard_LAN_csatlakozas.bat` kell. A vendég dupla kattintásra megkeresi a szervert a helyi hálózaton. Kézi csatlakozáshoz a parancssorban a BAT neve után megadhatod a szerver IPv4-címét. Mindkét gépre azonos remaster-verziót telepíts.

Ha a Windows hiányzó `MSVCP140.dll` vagy `VCRUNTIME140.dll` miatt megáll, telepítsd a Microsoft Visual C++ v14 **x86** futtatókörnyezetét. A játék 32 bites. [Microsoft letöltési útmutató](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) · [Hivatalos x86 telepítő](https://aka.ms/vc14/vc_redist.x86.exe)

## SteamOS és Proton

```sh
python3 install.py --game "/út/az/eredeti/Barnyard" --output "$HOME/Games/BarnyardRemaster"
```

Az eredeti Windows-telepítés mappáját a saját PC-dről is átmásolhatod. A Steam asztali módjában add hozzá az elkészült `OpenBarnyard/OpenBarnyard.exe` fájlt nem Steam-játékként. A kompatibilitásnál válassz Protont. Az *Indítás helye / Start In* mező a `Data` mappát tartalmazó új remaster-gyökér legyen. A telepítő bemásolja a saját játékod Bink és FMOD könyvtárait, valamint a remaster 235 előfordított DX11 shaderét. Ha a Proton-prefix hiányzó Visual C++ futtatókörnyezetet jelez, a Microsoft x86-os telepítőjét ugyanabban a prefixben futtasd.

LAN-szervernél a Steam indítási kapcsolója `%command% -nopause -nethost`. A másik gépen a `%command% -nopause -netjoin` megkeresi a szervert. A kapcsoló után közvetlenül a szerver IPv4-címét is megadhatod.

## Régebbi v5.5 frissítése

Az eredeti telepítés minden módban szükséges. A `--previous` opcióval az ismert v5.5-ös HD-textúrák a meglévő remasteredből átkerülnek az új mappába. A `--copy-saves` a régi mentéseidet is átmásolja. A régi játék mappáját megőrizheted.

```powershell
py -3 install.py --game "C:\Jatekok\Barnyard" --previous "C:\Jatekok\BarnyardRegi" --copy-saves --output "C:\Jatekok\BarnyardRemaster"
```

Az új kimeneti mappa kezdetben üres legyen. Az eredeti fájlok és a frissítés ellenőrzéséhez a fenti parancshoz hozzáadhatod a `--verify-only` kapcsolót. Ekkor a program csak az ellenőrzést végzi el. Fájleltérésnél kiírja a problémás útvonalat.

A tiszta telepítés a lemezes kiadás textúráival és betűkészletével, magyar fordítással, feliratokkal és a v5.5 játékmenetével indul. Az opcionális HD-generátor a projekt forrásában, a `Tools/hd` könyvtárban található.
