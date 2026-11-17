# HD-textúrák építése saját Barnyard PC-példányból

Az építő az eredeti játék textúrakönyvtáraiból 1864 HD-textúrát készít a külön telepített remaster `Data/Textures` mappájába. Otis szőrmintájához a saját játékpéldány modelljeiből számolja ki, hol van a törzs és a nyak. A kész textúrák együtt körülbelül 322 MiB helyet igényelnek. A két egyedi betűatlasz külön licenchez tartozik, így a játék saját betűkészlete marad használatban.

## Futtatás

Előbb futtasd a repó gyökerében található `install.py` telepítőt. Az alábbi parancsokat ugyanebből a mappából indítsd Python 3.10 vagy újabb verzióval. A `--game` az eredeti játék mappája, amelyben a `Data` található. A `--output` annak a különálló remasternek a mappája, amelyet a telepítő elkészített.

```shell
python -m pip install -r Tools/hd/requirements.txt
python Tools/hd/build_hd.py --game "/games/Barnyard" --output "/games/Barnyard-Remaster"
```

Ez a változat egy processzormagon dolgozik. Lanczos nagyítást, élesítést, valamint az eredeti színekhez igazított talaj- és lombrecepteket használ. A v5.5-höz alkalmazott neurális nagyítást a `--upscaler` kapcsoló választja ki.

### A kiadási képminőség előállítása

Töltsd le a Real-ESRGAN oldaláról a `realesrgan-ncnn-vulkan-20220424-windows.zip` vagy a `realesrgan-ncnn-vulkan-20220424-ubuntu.zip` fájlt. Csomagold ki a teljes könyvtárat, hogy a bináris mellett maradjon a `models` almappa. A program ellenőrzi a használt `realesrgan-x4plus.bin` és `.param` fájl hashét. Ehhez a módhoz Vulkan-kompatibilis GPU szükséges. [Real-ESRGAN v0.2.5.0 hivatalos kiadás](https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0)

Windows alatt például így futtasd.

```powershell
py -3 Tools/hd/build_hd.py --game "D:/Games/Barnyard" --output "D:/Games/Barnyard-Remaster" --upscaler "D:/Tools/RealESRGAN/realesrgan-ncnn-vulkan.exe"
```

Linuxon és SteamOS-en a Linux-csomag végrehajtható fájlját add meg.

```sh
python3 Tools/hd/build_hd.py --game "$HOME/Games/Barnyard" --output "$HOME/Games/Barnyard-Remaster" --upscaler "$HOME/Tools/RealESRGAN/realesrgan-ncnn-vulkan"
```

Gyors próbához egészítsd ki a parancsot például a `--only "World/grassmain.tga" --only "Char/otis.tga"` kapcsolókkal. A mappák írásmódja számít a Linux fájlrendszerén. Az építés után kapcsold be a játék HD-textúra beállítását, és indítsd újra a játékot.

## Pontosság és folytatás

A program az eredeti DDS-adatot még a nagyítás előtt ellenőrzi. A hatszeletes menüháttereket összeilleszti, egyben nagyítja, majd újra kivágja. Teljes mip-láncokat épít. A 23 talaj- és fűtextúra, a 30 lomb- és kéregtextúra, valamint Otis egyedi ecsetvonásai a korábbi kiadások azonos magból induló receptjeit használják.

Az összesítésben a `Release SHA-256 matches` a teljes fájlra egyező v5.5-textúrák száma, a `Local variants` a helyben létrejött eltérő fájloké. Az eltérő nagyító vagy GPU számítása más pixelt adhat. Windows alatt Radeon RX 9070 XT-n, Pillow 12.3.0-val és NumPy 2.5.2-vel mind az 54 egyedi változat, egy hatszeletes háttér első csempéje és a golflabda közvetlenül az eredeti játékfájlokból bájtra egyezett a v5.5-ös kiadással.

A korábbi kiadás megőrzött 4× nagyításait használva mind az 1864 textúra teljes DDS-fájlja egyezett a v5.5-ös kiadás SHA-256 értékével. A CPU-s nagyítással az 54 egyedi textúra átlagos RGB-eltérése képenként átlagolva 2,30 volt a 0–255-ös skálán. Az átlátszóság ezen az 54 képen mindenütt egyezett.

Az eredeti textúrából indított külön GPU-próbán az első 600 teljes DDS-ből 599 bájtra egyezett a v5.5 kiadással. Az egy eltérő kép pixelkülönbségéről nincs mérés.

Ugyanazzal a paranccsal folytathatod a megszakadt építést. A program az egyező fájlokat átugorja. A saját helyi változatait a célmappa `.barnyard-hd.json` állományában tartja nyilván. A CPU-s változatról később átválthatsz a GPU-s építésre. Ha egy céltextúrát egy másik mod vagy a felhasználó módosított, az építő megőrzi, megadja az útvonalát és megáll.
