# Barnyard Remaster

**A DirectX 11 fan remaster of the 2006 Barnyard PC game for Windows and Linux (via Proton).** Play the original story with soft sun shadows, wind in the grass and trees, and optional HD textures built from your copy of the game. Share a camera on the couch or explore the farm from two computers. We also added two-player LAN modes to 14 of the original game's single-player minigames.

[**Download the patch**](https://github.com/mannin1337/barnyard-remaster/releases/latest) · [Install and transfer saves](RemasterTools/Patch/INSTALL.md) · [Graphics and shadows](#directx-11-shaders-and-shadows) · [Couch and LAN co-op](#play-together) · [14 minigame screenshots](#minigame-gallery)

![Bimbo and Gyurcsany beside the red barn, with the apple trees casting shadows across the dirt path.](docs/images/farm-lan.png)

## DirectX 11, shaders and shadows

- **Sunlight and local lights.** Outdoors, trees, buildings and characters cast soft-edged sun shadows. Three filtered shadow maps cover the ground from near the camera into the distance, at up to 4096 × 4096 per map. Sun shadows fade at night. The bike headlamp can cast shadows of its own, and bloom gives glowing lamps and windows a halo.
- **Depth and atmosphere.** Choose HBAO or GTAO ambient occlusion for contact shading where surfaces meet. Sun shafts appear around the visible sun, while volumetric fog picks up sunlight and shadow. These controls are under **Options → Effects**.
- **Wind and vegetation.** Grass tips bend in gusts, and leaf shaders add sway and flutter to foliage on the farm. Under **Options → Detail** you can change the farm's draw distance and grass density.
- **Display and filtering.** Under **Options → Graphics** you can set the resolution, display mode, refresh rate, frame-rate cap and V-Sync. Supported hardware offers up to 8× MSAA for smoother edges and 16× anisotropic filtering for clearer ground textures at a distance. The patch includes 235 precompiled DirectX 11 shaders. [DX11 effects source](OpenBarnyard/Source/Platform/DX11/Effects) · [Shader manifest](RemasterTools/Patch/shader-hashes.json)

### Optional HD textures

The patch starts with the original artwork. The HD builder uses your game files to create **1,864 higher-resolution textures** in the farm's original colour palette. Grass, soil, leaves and bark get extra surface detail, and Otis gets a subtle coat texture on his neck and torso. Build the textures, switch on **Options → Graphics → HD Textures**, then restart the game. [Build the HD textures](Tools/hd/README.md)

## Play the story

Install the patch using your own copy of the original two-disc Barnyard PC game. On Windows, launch `Play_Barnyard.bat` and start or load your game. On desktop Linux or Steam Deck, add `OpenBarnyard/OpenBarnyard.exe` to Steam and select Proton. You can choose Xbox or PlayStation button prompts and play in English or with the optional Hungarian translation. [Installation and save transfer](RemasterTools/Patch/INSTALL.md) · [Linux and Steam Deck setup](RemasterTools/Patch/INSTALL.md#play-on-linux-with-steam-and-proton)

## Play together

For LAN, both players need the same remaster release and a connection to the same local network. Either computer can host.

| Your setup | How to start |
| --- | --- |
| **One PC, one screen** | Connect a controller for player two. In the main menu, choose **Co-op → Same Screen**, then start or load a game. Player one can use the keyboard and mouse or another controller. |
| **Two computers on LAN** | In the main menu, one player chooses **Co-op → Host LAN Game** and the other chooses **Co-op → Join LAN Game**. Start or load a game on each computer. The guest finds the host automatically. |

On LAN you can see each other on the farm and inside the barn when you're in the same area. One player can head indoors while the other stays outside. You each keep your own save and play the story missions in your own game. The game uses each cow's saved name, and you can choose a different display name for a session. [Choose your co-op names](RemasterTools/Patch/INSTALL.md#choose-your-co-op-names)

### Couch co-op on one screen

In **Same Screen**, one camera keeps both cows in view. Here Bimbo and Gyurcsany are inside the barn together.

[![Bimbo and Gyurcsany with name tags above their cows, sharing one view of the barn.](docs/images/couch-coop.png)](docs/images/couch-coop.png)

### LAN co-op from both players' screens

The two screenshots in each row come from the same LAN session. On the farm, Bimbo looks toward the red barn while Gyurcsany looks back toward the open shed. In Darts, both players can see the orange dart in Bimbo's hand before the fifth throw. Both scoreboards read 95–20.

| Bimbo's screen (host) | Gyurcsany's screen (guest) |
| --- | --- |
| **Farm**<br>[![Bimbo faces the red barn with Gyurcsany standing under the apple tree.](docs/images/lan-farm-bimbo.png)](docs/images/lan-farm-bimbo.png) | **Farm**<br>[![Gyurcsany looks back at Bimbo by the bed in the open shed.](docs/images/lan-farm-gyurcsany.png)](docs/images/lan-farm-gyurcsany.png) |
| **Darts · aiming**<br>[![Bimbo aims the orange dart. The scoreboard says Your turn (Bimbo).](docs/images/lan-darts-bimbo.png)](docs/images/lan-darts-bimbo.png) | **Darts · spectating**<br>[![Gyurcsany sees Bimbo's hand holding the dart and the message Watching your partner (Bimbo).](docs/images/lan-darts-gyurcsany.png)](docs/images/lan-darts-gyurcsany.png) |

## Challenge a friend

All 14 activities below are playable solo in the original PC game. For this remaster, we built a LAN mode for each one. Depending on the game, you compete in the same arena, watch each other's turns, or compare scores and finish times. On the farm, open the **pause menu → LAN Minigames**. Either player can invite the other. Accept the invitation to start, or choose **Next page** to browse the games.

| How you play | Minigames |
| --- | --- |
| Jump and kick in the same arena, alongside two AI opponents | **Mud Jumpers** |
| Take turns on the same board or table and watch your partner's shot | **Darts**, **Barnyard Pool** |
| Play a turn on your own computer while your partner spectates, then swap | **Gopher Golf**, **Chicken Launch**, **Whack-a-Rac** |
| Play the same challenge on each computer and compare scores | **Tease the Mailman**, **Tease Mrs Beady**, **Chasing Chicks**, **Veggie Patch Defender**, **Cow Tipping**, **Milk Bar** |
| Race on each computer and compare finish times | **Bike Race**, **Joy Ride** |

When you play as the guest, you can lock both Darts aim bars, start a Pool shot or jump in Mud Jumpers while your input is still travelling to the host. The host settles shared hits and scores. [Input timing and LAN playtest results](docs/multiplayer-testing.md#lan-input-timing-in-v55-lan5)

### Minigame gallery

Select a screenshot to open the full 1920 × 1080 frame from Bimbo and Gyurcsany's LAN sessions. We captured these in the game with HD textures, 8× MSAA, 16× anisotropic filtering and the highest detail and shadow settings.

| Chicken Launch | Whack-a-Rac |
| --- | --- |
| [![Bimbo tumbles backward in a cloud of feathers beside the chicken launcher.](docs/images/minigames/previews/chicken-launch.webp)](docs/images/minigames/chicken-launch.webp)<br>Launch Peck through the rings while your friend watches his flight. | [![Two toy raccoons pop up in the middle of the board with Bimbo leading 1–0.](docs/images/minigames/previews/whack-a-rac.webp)](docs/images/minigames/whack-a-rac.webp)<br>Hit the toy raccoons and coyotes, and spare the chickens. |

| Gopher Golf | Mud Jumpers |
| --- | --- |
| [![Gyurcsany watches Bimbo's gopher surface beside the golf ball.](docs/images/minigames/previews/gopher-golf.webp)](docs/images/gopher-golf.png)<br>Tee off, then steer your gopher underground to retrieve the ball. | [![Gyurcsany's view of Bimbo lifting a hoof to kick the pipe across the muddy arena.](docs/images/minigames/previews/mud-jumpers.webp)](docs/images/minigames/mud-jumpers.webp)<br>Jump the pipe or kick it back at your friend and the two computer-controlled cows. |

| Darts | Barnyard Pool |
| --- | --- |
| [![Bimbo aims at the dartboard with Gyurcsany leading 120–0.](docs/images/minigames/previews/darts.webp)](docs/images/darts-lan.png)<br>Take turns throwing at the same board and watch your friend's aim. | [![Bimbo watches as Gyurcsany lines up the second shot towards a red ball.](docs/images/minigames/previews/barnyard-pool.webp)](docs/images/minigames/barnyard-pool.webp)<br>Clear your colour, then pocket the black ball to win. |

| Tease the Mailman | Tease Mrs Beady |
| --- | --- |
| [![A cow crouches behind the mailman while three farm animals peek out from behind a tree.](docs/images/minigames/previews/tease-the-mailman.webp)](docs/images/minigames/tease-the-mailman.webp)<br>Pose behind the mailman's back and drop to all fours before he turns around. | [![Mrs Beady turns in her armchair as a cow peeks through the living-room window.](docs/images/minigames/previews/tease-mrs-beady.webp)](docs/images/minigames/tease-mrs-beady.webp)<br>Strike a pose outside Mrs Beady's window, then duck before Mr Beady spots you. |

| Chasing Chicks | Veggie Patch Defender |
| --- | --- |
| [![Two cows cross paths near the red barn as yellow chicks scatter along the farmyard trail.](docs/images/minigames/previews/chasing-chicks.webp)](docs/images/minigames/chasing-chicks.webp)<br>Stand in a scoring ring and toss a chick back into the coop. | [![A cow fires a jet of milk between vegetable rows beside a pumpkin-headed scarecrow.](docs/images/minigames/previews/vegetable-patch-defender.webp)](docs/images/minigames/vegetable-patch-defender.webp)<br>Squirt the raiding raccoons and throw them out before they steal the vegetables. |

| Bike Race | Joy Ride |
| --- | --- |
| [![Bimbo's bike is airborne above a hillside trail on the first lap.](docs/images/minigames/previews/bike-race.webp)](docs/images/minigames/bike-race.webp)<br>Race the Jersey Cows, then compare your finish time with your friend's. | [![Sparks fly as Bimbo scrapes a fence at 129 km/h on a nighttime Joy Ride.](docs/images/minigames/previews/joy-ride.webp)](docs/images/minigames/joy-ride.webp)<br>Drive the farmer's pickup back to the farm. The faster finish wins. |

| Cow Tipping | Milk Bar |
| --- | --- |
| [![Bimbo aims a tomato launcher across a moonlit pasture dotted with sleeping cows.](docs/images/minigames/previews/cow-tipping.webp)](docs/images/minigames/cow-tipping.webp)<br>Lob tomatoes at the kids before they can tip the sleeping cows. | [![A cow fills a pink milkshake at the tap while another waits at the checkered counter.](docs/images/minigames/previews/milk-bar.webp)](docs/images/minigames/milk-bar.webp)<br>Serve the thirsty customers without dropping a glass. |

## Build and test

For developers, the source includes the Win32 DX11 build scripts and the LAN regression tests. The playtest notes record how the host and guest behaved under different frame rates, latency and packet loss. [Build from source](BUILDING.md) · [Run the LAN tests](Tools/tests/netplay/README.md) · [Multiplayer playtest results](docs/multiplayer-testing.md)

## Credits

Blue Tongue Entertainment developed Barnyard and the TOSHI engine. This fan remaster builds on the OpenBarnyard and OpenToshi decompilation work by AdventureT and InfiniteC0re. The upstream license credits THQ Nordic as the owner of TOSHI. [License](License.md) · [Upstream project and credits](README.upstream.md)
