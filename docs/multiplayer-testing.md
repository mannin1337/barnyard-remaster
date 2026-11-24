# Multiplayer playtest results

## LAN input timing in v5.5-lan.5

The gameplay test runs a host and a guest as two muted Win32 DX11 processes on one Windows PC. Both run at idle priority in background mode, which presents frames without waiting for VSync. The test sends scripted inputs through the game's regular input mapping. Each process records when it dispatches an input, when the minigame applies the first matching action, and when the renderer reaches its next frame submission point. A difference of zero frames means all three events took place in the same game update. The measurement starts after device polling and ends before desktop composition and display scan-out. The spectator receives the other player's actions after the packets cross the link. Timing a SteamOS host and a separate Windows guest on physical hardware remains open for this release. [Record input timings on your own setup](../Tools/tests/netplay/README.md#test-coverage)

All **322 measured actions** reached both the native game state and frame submission in their dispatch frame. Across the two roles, the 95th percentile was **0.435 ms** from dispatch to action and **6.099 ms** from dispatch to frame submission. The game added 150–185 ms of network delay in each direction with 3% packet loss. Host and guest alternated between 30 and 90 FPS caps, with a 20 FPS host for Pool. The inputs included keyboard, mouse and emulated XInput events.

The input counts below come from the timing run. The last column records matches played through to their natural result on the same game executable, including separate full-length sessions. Each reported result matched on both players' scoreboards, and both returned to the farm.

| Minigame | Host samples | Guest samples | Action observed | Full match result (host, guest) |
| --- | ---: | ---: | --- | --- |
| Chicken Launch | 20 | 20 | Pulling back and adding power | 5–5 |
| Whack-a-Rac | 31 | 8 | Swinging the mallet | 2–2 |
| Gopher Golf | 1 | 4 | Speeding up the flyover or starting a swing | 0–0 after ten alternating turns on hole one |
| Mud Jumpers | 8 | 12 | Jump, kick or serve | 2–10003 |
| Darts | 60 | 60 | Locking aim and starting a throw | 140–50 |
| Tease the Mailman | 8 | 9 | Changing pose or ducking | 0–0 |
| Tease Mrs. Beady | 6 | 6 | Changing pose or ducking | 0–0 |
| Chasing Chicks | 3 | 3 | Firing a milk squirt | 0–0 |
| Vegetable Patch Defender | 3 | 3 | Firing a milk squirt | 20–40 |
| Bike Race | 4 | 4 | Jumping on the bike | Both finished two laps, 14:37.236 and 13:54.327 |
| Joy Ride | 5 | 7 | Changing the throttle | Both timed out |
| Cow Tipping | 6 | 6 | Starting the throw animation | 5–5 |
| Milk Bar | 8 | 7 | Filling or throwing a drink | 0–3 |
| Barnyard Pool | 7 | 3 | Moving the cue, charging or starting the stroke | 6–7 coloured balls, host won |

Two long frames affected wall-clock times in this run. In Chasing Chicks, the first shot took **1,267.558 ms** on the host and **1,074.670 ms** on the guest to reach frame submission. Both processes slowed before the shot while a 1.36 GB archive was being written to the same drive. In Joy Ride, one throttle release took **292.461 ms** to reach submission. Its native throttle change took **0.001 ms**.

We repeated both games with the same executable, network impairment, input schedule and frame-rate caps while the game drive was free of packaging work. The first Chasing Chicks shot reached submission in **3.882 ms** on the host and **4.912 ms** on the guest. Across a full match the maxima for 14 shots per player were **7.961 ms** and **12.248 ms**.

In Joy Ride, the same early guest throttle release reached submission in **3.245 ms**. Over the full race, the 95th percentiles for 47 host and 48 guest inputs were **13.077 ms** and **13.172 ms**. A later throttle release coincided with a long frame on each side, **171.138 ms** on the host and **207.462 ms** on the guest. Their native throttle changes took at most one microsecond. Both idle-priority test processes used 15.625 ms of CPU time in the same 265 ms observation window, when total CPU usage reached 100%.

We ran the full Joy Ride match a third time with the same build, inputs and frame-rate limits, changing the test processes from Windows **Idle** to **Below Normal** priority. The corresponding late releases reached submission in **3.409 ms** and **4.348 ms**. Across 47 host and 48 guest actions the maxima were **10.229 ms** and **20.864 ms**, with average system CPU usage of 98.77%. The throttle and the frame submission still landed in the same update for every measured action. The earlier stall was sensitive to scheduling conditions. Its exact wait reason was not captured during the idle-priority run.

In the Darts match both players threw 30 darts, all 60 guest aim coordinates matched the host's accepted values to eight decimal places, and both scoreboards finished at **140–50**. In Mud Jumpers both players finished at **2–10003**. The Pool test at 20/90 FPS recorded both players taking shots and 401 shared table snapshots. A separate full Pool match took **14 shots**. Both players saw **6 and 7 coloured balls pocketed**. The guest scratched on the eight ball, giving the host the win. In total 4,895 table snapshots matched. Both processes returned to the farm after each of these three matches.

In Gopher Golf each player took five tee shots on the first hole and used up their five lives. The game ended normally after ten alternating turns. We captured a frame directly from each spectator's game when the other player surfaced. The gopher was visible on the green in both captures after the players swapped turns. Each spectator also saw the underground trail and raised mounds, and the camera continued moving between network updates.

We also played **all nine holes as both host and guest** with a separate diagnostic build of the release source. An automated player aimed the stick and pressed the swing button. The game handled ball physics, hole completion and scoring. Each player sank all nine holes and spectated their partner on every hole. The host earned 362 points plus 160 for eight remaining lives. The guest earned 329 plus 120 for six lives. Both scorecards showed **522–449**, and both players returned to the farm. The timing results above come from the downloadable executable.

We checked the spectator separately from the player taking a turn. In Darts, all **6,957 board snapshots** applied by the guest matched the host's state hash for the same sequence number. During all **30 host throws**, the guest saw the dart attached to the thrower's hand and inside the camera view. In Whack-a-Rac, a raccoon's animation advanced between packets on both spectators' screens. In Chicken Launch, the guest's bird and camera also moved between packets. The one-way delay to the spectator's physical display still needs a separate measurement.

The standalone tests also target conditions that are difficult to repeat with a real controller.

| Test | Result |
| --- | --- |
| Darts aim between network updates | At 120 FPS, with fresh board updates at 30 or 60 Hz, all 240 horizontal locks matched the guest's last displayed coordinate exactly on the host. The test includes a missed board update and 150 ms of simulated delay in each direction. |
| Barnyard Pool at low frame rates | A full-power stroke made contact exactly once at each of 15, 20, 23, 30, 60 and 120 FPS. With the previous stroke calculation the cue could fail to reach the ball below 24 FPS. |
| Pool placement and first movement | At 120 FPS the guest could move the cue ball on its first local update without a new host packet. The tests also check rejected placement, a duplicate release and an 800 ms stall. |
| Windows mouse input | Two queued raw mouse events reached the current input frame exactly once in a hidden-window test. Queued focus and close events retained their order. |

All 11 standalone CTest programs passed in the Win32 Release configuration. [Regression suite](../Tools/tests/netplay/README.md) · [Source used for the game executable](https://github.com/mannin1337/barnyard-remaster/commit/be454ab1f48b9368763063a29e402b2a0a5b5632)

### Playing from an installation made with the public patch

The installer verified all 5,393 original data files and both retail runtime libraries. We launched separate host and guest processes from the patched installation with its 235 DX11 shaders. Windows' loaded-module list confirmed that both processes used the installed executable and the retail Bink and FMOD libraries. In a full Mud Jumpers match, both players finished at **6–10003** and returned to the farm.

In a second full Darts match the players finished at **35–0** after three rounds and 30 throws each. All **60 guest aim coordinates** matched the host's accepted values. Across all **60 throws**, the guest's hand model was attached, drawable and in the camera view for either thrower. Both processes returned to the farm with their saved state intact.

## Co-op names in v5.5-lan.4

The gallery sessions used **Bimbo** and **Gyurcsany** on two Windows game instances for LAN and on a single instance for couch co-op. Both names appear above their cows on the farm and in the shared barn, on the Darts scoreboard and in the Gopher Golf spectator view. The engine saved the four PNGs directly at 1920×1080 using HD textures and a locally installed optional HD font.

The five standalone Win32 tests passed. The name test covers English and Hungarian scorecards with the host and guest swapped. A playthrough of this release on a separate Linux machine running Proton is still pending. [Regression tests](../Tools/tests/netplay/README.md)

## Earlier v5.5 LAN sessions

These sessions ran two Windows game processes with different frame rates, added network delay and packet loss. The `v5.5-lan.3` patch packaged the same game executable as `v5.5-lan.2`.

| Game | What happened on both players' screens |
| --- | --- |
| Darts | The host accepted all 60 of the guest's aim-axis locks at the position shown on the first button press under 400 ms simulated round-trip latency and 2% packet loss. A three-round match installed from the public patch ended **165–143** on both screens. |
| Barnyard Pool | Both players took shots and received the same result with host/guest frame rates of **30/120 FPS** and **120/30 FPS**. The guest's power meter reacted on the first local frame. |
| Mud Jumpers | A match using a female guest character loaded the shared arena and reached the results screen in an installation built from the public patch. |
| Gopher Golf | Both players could see the spectated gopher and the trail it left on the surface. A longer run reached ten alternating turns, showed **0–0** on both screens and returned both players to the farm. The test driver used up both players' five lives on the first hole. Completion of all nine holes was recorded separately in an earlier **v5.4** developer test. |
| Bike Race | The host and guest remounted **16 times** using regular accelerate, jump and trick inputs. Physics stayed active after every remount, and both riders continued after their final fall. The guest finished two laps in **14:00.018**. The host's test driver circled checkpoint 15, so this run recorded only the guest's finish time. |
| Chicken Launch, Whack-a-Rac, Cow Tipping and Milk Bar | All four matches reached the normal results screen. |
| Tease the Mailman, Tease Mrs. Beady and Chasing Chicks | Both players finished their local challenge. |
| Vegetable Patch Defender | Both screens showed **41–20**. |
| Joy Ride | Both players ran out of time and saw the same **Did not finish** result. |

Disconnect tests covered Tease the Mailman, Tease Mrs. Beady, Chasing Chicks, Vegetable Patch Defender, Bike Race and Joy Ride. Each game started on both sides and the host returned to the farm after the guest disconnected.

The standalone LAN tests cover input timing, turns and spectator motion without loading the retail game. [Run the tests](../Tools/tests/netplay/README.md) · [LAN fixes in v5.5-lan.1](releases/v5.5-lan.1.md) · [Bike remount fix in v5.5-lan.2](releases/v5.5-lan.2.md)
