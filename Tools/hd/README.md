# Build HD textures from your own Barnyard PC installation

This optional tool rebuilds 1,864 HD textures from the original game's texture libraries. It writes about **322 MiB** of DDS files to the separate remaster installation's `Data/Textures` directory. The builder uses the local game models to place Otis's fur detail on his neck and torso. The two separately licensed custom font atlases are excluded, so a fresh installation uses the game's own fonts.

## Build the textures

First create a playable remaster with the `install.py` from the extracted **release patch**. Then open a terminal in the **source repository root**, the directory containing `Tools/hd`. Use Python 3.10 or later. Point `--game` at the original game folder containing `Data`, and `--output` at the remaster folder that the installer created. [Patch installation guide](../../RemasterTools/Patch/INSTALL.md)

**Windows PowerShell**

```powershell
py -3 -m pip install -r Tools/hd/requirements.txt
py -3 Tools/hd/build_hd.py --game "C:\Games\Barnyard" --output "C:\Games\BarnyardRemaster"
```

**Linux or SteamOS**

```sh
python3 -m venv .venv-hd
. .venv-hd/bin/activate
python -m pip install -r Tools/hd/requirements.txt
python Tools/hd/build_hd.py --game "$HOME/Games/Barnyard" --output "$HOME/Games/BarnyardRemaster"
```

The default path uses one CPU worker, Lanczos upscaling and sharpening. It applies the terrain, foliage and fur recipes to the upscaled pixels. To try a small subset first, append `--only "World/grassmain.tga" --only "Char/otis.tga"`. After building, open **Options** from the game's main menu, set **HD Textures** to **On**, and restart the game.

## Use the v5.5 GPU upscaler

The v5.5 release used the `realesrgan-x4plus` model with Real-ESRGAN NCNN Vulkan. This path requires a Vulkan-capable GPU. Extract the complete `realesrgan-ncnn-vulkan-20220424-windows.zip` or `realesrgan-ncnn-vulkan-20220424-ubuntu.zip` archive. Keep its `models` directory next to the executable. The builder verifies the hashes of the model's `.bin` and `.param` files. [Official Real-ESRGAN v0.2.5.0 release](https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0)

**Windows PowerShell**

```powershell
py -3 Tools/hd/build_hd.py --game "C:\Games\Barnyard" --output "C:\Games\BarnyardRemaster" --upscaler "C:\Tools\RealESRGAN\realesrgan-ncnn-vulkan.exe"
```

**Linux or SteamOS**, with the Python environment above active

```sh
python Tools/hd/build_hd.py --game "$HOME/Games/Barnyard" --output "$HOME/Games/BarnyardRemaster" --upscaler "$HOME/Tools/RealESRGAN/realesrgan-ncnn-vulkan"
```

## Resume a build and read its results

Rerun the same command after an interruption. The tool skips matching files and tracks the variants it created in the remaster's `.barnyard-hd.json`. You can return later with `--upscaler` to rebuild CPU-generated variants with the GPU. If another mod or an editor changed an output texture, the builder preserves the file, prints its path and stops.

The builder checks the original DDS data before upscaling. It joins six-tile menu backgrounds into one image for upscaling, then cuts them back into tiles and generates complete mip chains. The release recipes cover 23 ground and grass textures, 30 foliage and bark textures, and Otis's fur.

`Release SHA-256 matches` counts DDS files that match v5.5 byte for byte. `Local variants` counts valid local outputs with different bytes. The choice of upscaler and GPU can affect the pixels. These measurements come from separate comparisons.

| Comparison | Result |
|---|---|
| Re-encoded from the retained v5.5 4× upscales | All 1,864 DDS files matched the release hashes. |
| Rebuilt from original game data on a Radeon RX 9070 XT under Windows, with Pillow 12.3.0 and NumPy 2.5.2 | All 54 specially processed textures, the first tile of a six-tile background, and the golf ball matched byte for byte. |
| Separate GPU run from the original textures | 599 of the first 600 DDS files matched. The pixel difference for the remaining file was not measured. |
| CPU output for the 54 specially processed textures | Mean absolute RGB difference averaged 2.30 per image on the 0–255 scale. Alpha matched throughout all 54 images. |
