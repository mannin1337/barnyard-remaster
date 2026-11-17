# Multiplayer playtest results

These v5.5 LAN sessions ran two Windows game processes with different frame rates, added network delay and packet loss. Physical SteamOS-to-Windows play is the remaining cross-platform test. The public `v5.5-lan.3` patch packages the same game executable as `v5.5-lan.2`.

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
