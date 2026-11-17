# LAN-regressziós tesztek

A négy teszt közvetlenül a játék hálózati és megjelenítési fejléceit használja. Futtatásukhoz CMake 3.20 vagy újabb és C++17-es fordító szükséges.

## Futtatás

A tároló gyökeréből futtasd a következő parancsokat. A CTest kiírja az egyes méréseket és az ellenőrzések eredményét.

```sh
cmake -S Tools/tests/netplay -B build/netplay
cmake --build build/netplay --config Release
ctest --test-dir build/netplay -C Release -V
```

Visual Studio generátorral a Win32-es változathoz add meg az első parancsban a `-A Win32` kapcsolót. A `build/netplay` helyén másik üres fordítási mappát is használhatsz.

| Teszt | Mit ellenőriz? |
| --- | --- |
| `view_cadence` | A 30 Hz-es mintavételt négy képkockasebességnél és ingadozó képidő mellett. Hosszabb megakadás után egyetlen frissítés esedékes. |
| `darts_view_input` | A kattintás ahhoz a célzóképhez tartozik, amelyet a vendég látott. A teszt lefedi az egyszeri feldolgozást, az újrapróbálást, a lejáratot és a régebbi csomagformátumot. |
| `golf_ground` | A 25 nyom- és kupacdarab kódolásának pontosságát és a darabok eltűnését, ha kiesik egy köztes állapotkép. |
| `chicken_motion` | A madár és a kamera mozgását 120 képkocka/másodpercnél, 30 Hz-es mintákkal, kihagyott csomaggal, megállással és meccsváltással. |
