#include "pch.h"
#include "AModStrings.h"

#include <File/TFile.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

// Every text of the mod: the id (the key in the JSON file, the ids of _tools/locale_tool.py), the English text, what
// the translator has to know and the most characters that fit (0: no limit). Keep one entry per line, the list of the
// translator (forditas/mod_strings_en*.json) is made from it
struct ModStringInfo
{
	const TCHAR*  szId;
	const TWCHAR* wszEnglish;
	const TCHAR*  szContext;
	TINT          iMaxLength;
};

static const ModStringInfo s_aModStrings[] = {
	{ "mainmenu.coop", L"Co-op", "main menu button that opens the co-op menu (big comic font, capitals only)", 14 },
	{ "coop.title", L"Co-op", "title of the co-op menu (big comic font, capitals only)", 14 },
	{ "coop.host", L"Host LAN Game", "co-op menu button: this computer hosts a game on the local network (big comic font)", 22 },
	{ "coop.join", L"Join LAN Game", "co-op menu button: join a game hosted on the local network (big comic font)", 22 },
	{ "coop.samescreen", L"Same Screen", "co-op menu button: two players on this computer (big comic font)", 22 },
	{ "coop.controller", L"Player 2: controller %d", "co-op menu line below the buttons, %d = number of the controller of player 2", 40 },
	{ "coop.nocontroller", L"Player 2: connect a controller.", "co-op menu line below the buttons, two lines at most (a controller plugged in later is found without a restart)", 80 },
	{ "coopnet.hosting_connected", L"Hosting. Player 2 is connected (%s).", "LAN status line, %s = IP address of the other computer", 60 },
	{ "coopnet.hosting_on", L"Hosting on %s. Waiting for player 2...", "LAN status line, %s = IP address of this computer", 60 },
	{ "coopnet.hosting", L"Hosting. Waiting for player 2...", "LAN status line", 60 },
	{ "coopnet.connected", L"Connected to the host at %s.", "LAN status line, %s = IP address of the host", 60 },
	{ "coopnet.looking", L"Looking for a host on the LAN...", "LAN status line", 60 },
	{ "options.hd_on", L"HD Textures: On", "options menu button (big comic font)", 26 },
	{ "options.hd_off", L"HD Textures: Off", "options menu button (big comic font)", 26 },
	{ "options.hd_on_restart", L"HD Textures: On (restart)", "options menu button, the setting applies after a restart (big comic font)", 34 },
	{ "options.hd_off_restart", L"HD Textures: Off (restart)", "options menu button, the setting applies after a restart (big comic font)", 34 },
	{ "options.language.restart", L"(restart)", "added to the Language button of the options menu while the picked language applies after a restart (big comic font)", 14 },
	{ "options.language.missing", L"(%s is missing)", "added to the Language button of the options menu and to the language in the language menu, %s = the missing file (hun.trb)", 26 },
	{ "controls.rawmouse", L"Raw mouse input", "controls menu row: the mouse is read without the acceleration of Windows (big comic font, shrunk to fit)", 24 },
	{ "controls.padsensitivity", L"Pad sensitivity", "controls menu row: slider of the camera speed of a gamepad (big comic font, shrunk to fit)", 24 },
	{ "antics.title", L"CHOOSE PLAYERS!", "title of the player count menu of Barnyard Antics (big comic font, capitals only)", 20 },
	{ "antics.nocontrollers", L"Insufficient controllers detected for Barnyard Antics!", "Barnyard Antics player count menu, shown when there are fewer controllers than players", 70 },
	{ "minigame.player", L"P%d", "label above a human player in the minigames of several players, %d = number of the player (1-4)", 7 },
	{ "minigame.computer", L"Com", "label above a computer player in the minigames of several players", 7 },
	{ "startmenu.partner", L"Go to partner", "pause menu button in a LAN co-op game: takes the player's cow to the cow of the other player (big comic font, capitals only)", 20 },
	{ "partner.name", L"Partner", "name tag over the cow of the other player of a LAN co-op game while their name isn't known (small font)", 15 },
	{ "partner.waiting", L"Waiting for your partner...", "LAN co-op line at the top of the screen: the other player isn't connected yet (small font, one line)", 50 },
	{ "partner.loading", L"Partner: loading...", "LAN co-op line at the top of the screen: what the other player does now, their game is loading (small font, one line)", 50 },
	{ "partner.mainmenu", L"Partner: in the main menu", "LAN co-op line at the top of the screen: what the other player does now (small font, one line)", 50 },
	{ "partner.playing", L"Partner: playing %s", "LAN co-op line at the top of the screen: what the other player does now, %s = title of the minigame they play (small font, one line)", 30 },
	{ "partner.minigame", L"Partner: playing a minigame", "LAN co-op line at the top of the screen: what the other player does now, a minigame without a title (small font, one line)", 50 },
	{ "partner.scene", L"Partner: watching a scene", "LAN co-op line at the top of the screen: what the other player does now, a cutscene of the story (small font, one line)", 50 },
	{ "partner.shop", L"Partner: in the gopher shop", "LAN co-op line at the top of the screen: what the other player does now (small font, one line)", 50 },
	{ "partner.cooking", L"Partner: cooking", "LAN co-op line at the top of the screen: what the other player does now, the cooking minigame (small font, one line)", 50 },
	{ "partner.sleeping", L"Partner: sleeping", "LAN co-op line at the top of the screen: what the other player does now, their cow sleeps to pass the time (small font, one line)", 50 },
	{ "partner.conversation", L"Partner: in a conversation", "LAN co-op line at the top of the screen: what the other player does now, they talk to an animal (small font, one line)", 50 },
	{ "partner.menu", L"Partner: in the menu", "LAN co-op line at the top of the screen: what the other player does now, the pause menu or the inventory (small font, one line)", 50 },
	{ "partner.farm", L"Partner: on the farm", "LAN co-op line at the top of the screen: what the other player does now, shown while this player is somewhere else (small font, one line)", 50 },
	{ "partner.barn", L"Partner: in the barn", "LAN co-op line at the top of the screen: what the other player does now, shown while this player is not in the barn (small font, one line)", 50 },
	{ "partner.barn_night", L"Partner: in the barn at night", "LAN co-op line at the top of the screen: what the other player does now, shown while this player is in the barn too (each game has its own time of day) (small font, one line)", 50 },
	{ "partner.barn_day", L"Partner: in the barn by day", "LAN co-op line at the top of the screen: what the other player does now, shown while this player is in the barn too (each game has its own time of day) (small font, one line)", 50 },
	{ "partner.goto_away", L"Your partner can't be reached right now", "LAN co-op line at the top of the screen for a few seconds after 'Go to partner' was chosen in the pause menu: it can't be done because the other player is loading, in a minigame, a scene, a shop... (small font, one line)", 60 },
	{ "partner.goto_busy", L"You can't go to your partner right now", "LAN co-op line at the top of the screen for a few seconds after 'Go to partner' was chosen in the pause menu: it can't be done because this player's cow is busy (carries something, swims, is in a conversation, a mission scene or a micro game) (small font, one line)", 60 },
	{ "partner.goto_barn_closed", L"The barn is closed for you now", "LAN co-op line at the top of the screen for a few seconds after 'Go to partner' was chosen in the pause menu: the other player is in the barn, but in this player's story the barn isn't open at night yet (small font, one line)", 60 },
	{ "partner.goto_barn_time", L"Your partner's barn is at another time of day", "LAN co-op line at the top of the screen for a few seconds after 'Go to partner' was chosen in the pause menu: the other player is in the barn by night and it's day in this game, or the other way round (the barn by day and the barn at night are two places) (small font, one line)", 60 },
	{ "partner.goto_barn_floor", L"Your partner is on another floor of the barn", "LAN co-op line at the top of the screen for a few seconds after 'Go to partner' was chosen in the pause menu: both players are in the barn, on different floors (the loft, the ground floor, the basement) (small font, one line)", 60 },
	{ "coopstory.partner_finished", L"Your partner finished %s", "LAN co-op story, line 1 of 2 of the offer to skip a mission the other player has completed, %s = name of the mission (in English, from the game data) (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.partner_finished_more", L"Your partner finished %s and %d more", "LAN co-op story, line 1 of 2 of the offer to skip several missions, %s = name of the mission (in English, from the game data), %d = how many other missions (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.partner_ahead", L"Your partner is %d missions ahead of you", "LAN co-op story, line 1 of 2 of the offer to catch up, %d = number of missions (small font, one row)", 60 },
	{ "coopstory.control_left_stick", L"K or the left stick button", "LAN co-op story, the skip control, put into the %s of the 'Hold %s to ...' lines: the K key of the keyboard or pressing the left stick of a controller", 30 },
	{ "coopstory.control_both_sticks", L"K or both stick buttons", "LAN co-op story, the skip control where the left stick button does something else, put into the %s of the 'Hold %s to ...' lines: the K key or pressing both sticks of a controller", 30 },
	{ "coopstory.hold_to_skip_it", L"Hold %s to skip it (tap to hide)", "LAN co-op story, line 2 of 2 under 'Your partner finished ...': holding the control completes the mission here too, a short press hides the offer; %s = the skip control ('K or the left stick button') (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.hold_to_skip_them", L"Hold %s to skip them (tap to hide)", "LAN co-op story, line 2 of 2 under 'Your partner finished ... and ... more' (several missions); %s = the skip control ('K or the left stick button') (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.hold_to_catch_up", L"Hold %s to catch up: %d missions (tap to hide)", "LAN co-op story, line 2 of 2 under 'Your partner is ... missions ahead of you'; %s = the skip control ('K or the left stick button'), %d = number of missions (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 46 },
	{ "coopstory.hold_to_skip_minigame", L"Hold %s to skip this minigame (tap to hide)", "LAN co-op story, line 2 of 2 under 'Your partner finished ...' in the minigame of that mission: the minigame ends as won; %s = the skip control ('K or the left stick button') (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 46 },
	{ "coopstory.keep_holding", L"Keep holding to skip... %d%%", "LAN co-op story, line 2 of 2 while the skip control is held, %d = progress in percent, %% = the percent sign (small font, one row)", 60 },
	{ "coopstory.catching_up", L"Catching up with your partner...", "LAN co-op story, line 1 of 2 while the missions are skipped one after the other (small font, one row)", 60 },
	{ "coopstory.missions_to_go", L"%d missions to go", "LAN co-op story, line 2 of 2 under 'Catching up with your partner...', %d = missions left (2 or more) (small font, one row)", 50 },
	{ "coopstory.mission_to_go", L"1 mission to go", "LAN co-op story, line 2 of 2 under 'Catching up with your partner...', one mission left (small font, one row)", 50 },
	{ "coopstory.cant_skip_end", L"The end of the story can't be skipped: play it yourself", "LAN co-op story, why a mission the other player has completed can't be skipped (the last missions of the story), put into the %s of '%s (tap K to hide)'", 55 },
	{ "coopstory.cant_skip_not_story", L"It isn't part of the story", "LAN co-op story, why a mission the other player has completed can't be skipped (it isn't a story mission), put into the %s of '%s (tap K to hide)'", 55 },
	{ "coopstory.tap_to_hide", L"%s (tap K to hide)", "LAN co-op story, line 2 of 2 under 'Your partner finished ...' when the mission can't be skipped, %s = the reason (a whole sentence), K = the K key of the keyboard (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 20 },
	{ "coopstory.skipped", L"Skipped: %s", "LAN co-op story, line 1 of 2 of the note after a skip, %s = name of the mission (in English, from the game data) (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.skipped_more", L"Skipped: %s and %d more", "LAN co-op story, line 1 of 2 of the note after a skip of several missions, %s = name of the mission (in English, from the game data), %d = how many other missions (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.counts_as_won", L"It counts as won", "LAN co-op story, line 2 of 2 under 'Skipped: ...' after a minigame was skipped (small font, one row)", 60 },
	{ "coopstory.your_story", L"Your story: act %d, tier %d", "LAN co-op story, line 2 of 2 under 'Skipped: ...': where the story of this player is now, %d %d = number of the act and of the tier (a group of missions) in it (small font, one row)", 60 },
	{ "coopstory.your_story_more", L"Your story: act %d, tier %d (%d more to catch up)", "LAN co-op story, line 2 of 2 under 'Skipped: ...', %d %d = act and tier of this player's story, the third %d = missions still to skip (small font, one row)", 66 },
	{ "coopstory.catching_up_stopped", L"Catching up stopped", "LAN co-op story, line 1 of 2 of a note: the missions left can't be skipped (small font, one row)", 60 },
	{ "coopstory.story_stays", L"Your story stays at act %d, tier %d: play the rest of it", "LAN co-op story, line 2 of 2 under 'Catching up stopped', %d %d = act and tier of this player's story (small font, one row)", 68 },
	{ "coopstory.partner_skipped", L"Your partner skipped %s", "LAN co-op story, line 1 of 2 of the note when the other player skipped a mission this player has completed, %s = name of the mission (in English, from the game data); the sentence goes on in line 2 ('to catch up with you ...') (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.partner_skipped_more", L"Your partner skipped %s and %d more", "LAN co-op story, line 1 of 2 of the note when the other player skipped several missions, %s = name of the mission (in English, from the game data), %d = how many other missions; the sentence goes on in line 2 ('to catch up with you ...') (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.partner_story", L"to catch up with you (their story: act %d, tier %d)", "LAN co-op story, line 2 of 2, the end of the sentence 'Your partner skipped ...', %d %d = act and tier of the other player's story (small font, one row)", 68 },
	{ "coopstory.data_differs", L"Co-op: your games have different quest data", "LAN co-op story, line 1 of 2 of a note: the two games don't have the same mission files (small font, one row)", 66 },
	{ "coopstory.data_differs_2", L"Missions can't be skipped between them", "LAN co-op story, line 2 of 2 under 'Co-op: your games have different quest data' (small font, one row)", 66 },
	{ "coopstory.saved_both", L"Saved", "LAN co-op story, line 1 of 2 of the note after this player saved the game (small font, one row)", 60 },
	{ "coopstory.saved_both_2", L"Your partner saved too", "LAN co-op story, line 2 of 2 under 'Saved': the game of the other player was saved as well (small font, one row)", 60 },
	{ "coopstory.saved_for_partner", L"Your partner saved", "LAN co-op story, line 1 of 2 of the note when the other player saved the game; the sentence goes on in line 2 ('so this game was saved too') (small font, one row)", 60 },
	{ "coopstory.saved_for_partner_2", L"so this game was saved too", "LAN co-op story, line 2 of 2, the end of the sentence 'Your partner saved' (small font, one row)", 60 },
	{ "coopstory.save_waits", L"Your partner saved", "LAN co-op story, line 1 of 2 of the note when the other player saved the game while this player is in a mission (small font, one row)", 60 },
	{ "coopstory.save_waits_2", L"This game saves when your quest is over", "LAN co-op story, line 2 of 2 under 'Your partner saved': this game is saved after the mission (small font, one row)", 60 },
	{ "coopstory.clock_follows", L"Your clock follows your partner's", "LAN co-op story, line 1 of 2 of the note when the clock of this game was set to the clock of the other player's game (the game that joined follows the time of day of the host) (small font, one row)", 60 },
	{ "coopstory.clock_follows_2", L"It's %d:%02d now", "LAN co-op story, line 2 of 2 under the line about the clock ('Your clock follows ...'): the new time of day, %d = hour (0 to 23), %02d = minute with two digits (small font, one row)", 60 },
	{ "coopstory.partner_step", L"Your partner did a step of %s", "LAN co-op story, line 1 of 2 of the note when the other player did a step of the mission both players are doing (the step is done in this game too), %s = name of the mission (in English, from the game data) (small font, the whole line has to fit in one row: about 70 characters with the %s filled in)", 44 },
	{ "coopstory.partner_step_2", L"%d of its %d steps are done", "LAN co-op story, line 2 of 2 under 'Your partner did a step of ...', the first %d = steps done, the second %d = steps of the mission (small font, one row)", 60 },
	{ "graphics.page.graphics", L"Graphics", "options menu button and the title of its page: resolution, display mode, anti-aliasing... (big comic font, capitals only)", 14 },
	{ "graphics.page.detail", L"Detail", "options menu button and the title of its page: draw distance and grass density (big comic font, capitals only)", 14 },
	{ "graphics.page.effects", L"Effects", "options menu button and the title of its page: shadows, lights, fog... (big comic font, capitals only)", 14 },
	{ "graphics.row.resolution", L"Resolution", "row name on the Graphics page of the options (big comic font, capitals only): the size of the picture in pixels; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.displaymode", L"Display Mode", "row name on the Graphics page of the options (big comic font, capitals only): fullscreen, borderless or windowed; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.refreshrate", L"Refresh Rate", "row name on the Graphics page of the options (big comic font, capitals only): of the display, in Hz; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.vsync", L"V-Sync", "row name on the Graphics page of the options (big comic font, capitals only): vertical sync; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.fpslimit", L"FPS Limit", "row name on the Graphics page of the options (big comic font, capitals only): the most frames per second; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.antialiasing", L"Anti-Aliasing", "row name on the Graphics page of the options (big comic font, capitals only): smoothing of the edges (2x, 4x, 8x); keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.texturefilter", L"Texture Filter", "row name on the Graphics page of the options (big comic font, capitals only): trilinear or anisotropic filtering of the textures; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.hdtextures", L"HD Textures", "row name on the Graphics page of the options (big comic font, capitals only): the high resolution textures of the mod; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.drawdistance", L"Draw Distance", "row name on the Detail page of the options (big comic font, capitals only): how far the trees and objects are drawn; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.grassdensity", L"Grass Density", "row name on the Detail page of the options (big comic font, capitals only): how much grass is drawn; keep it short: about 13 characters fill the width of a 800x600 screen", 14 },
	{ "graphics.row.ao", L"Ambient Occlusion", "row name on the Effects page of the options (big comic font, capitals only): soft shadows in corners and where things touch (the values HBAO and GTAO are the names of two techniques); keep it short: this page is already a little wider than a 800x600 screen", 17 },
	{ "graphics.row.aoquality", L"AO Quality", "row name on the Effects page of the options (big comic font, capitals only): quality of the ambient occlusion (AO), half or full; keep it short: this page is already a little wider than a 800x600 screen", 17 },
	{ "graphics.row.bloom", L"Bloom", "row name on the Effects page of the options (big comic font, capitals only): the glow around bright things; keep it short: this page is already a little wider than a 800x600 screen", 17 },
	{ "graphics.row.dynamiclights", L"Dynamic Lights", "row name on the Effects page of the options (big comic font, capitals only): the lights of the night (the headlight of the bike, lamps); keep it short: this page is already a little wider than a 800x600 screen", 17 },
	{ "graphics.row.lightshadows", L"Light Shadows", "row name on the Effects page of the options (big comic font, capitals only): the shadows cast by the dynamic lights; keep it short: this page is already a little wider than a 800x600 screen", 17 },
	{ "graphics.row.sunshadows", L"Sun Shadows", "row name on the Effects page of the options (big comic font, capitals only): the shadows cast by the sun; keep it short: this page is already a little wider than a 800x600 screen", 17 },
	{ "graphics.row.sunshafts", L"Sun Shafts", "row name on the Effects page of the options (big comic font, capitals only): the rays of the sun; keep it short: this page is already a little wider than a 800x600 screen", 17 },
	{ "graphics.row.volumetricfog", L"Volumetric Fog", "row name on the Effects page of the options (big comic font, capitals only): fog lit by the sun; keep it short: this page is already a little wider than a 800x600 screen", 17 },
	{ "graphics.value.off", L"Off", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): the setting of the row is switched off (V-Sync, FPS Limit, Anti-Aliasing, HD Textures, Bloom, shadows, fog, grass...)", 12 },
	{ "graphics.value.on", L"On", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): the setting of the row is switched on", 12 },
	{ "graphics.value.fullscreen", L"Fullscreen", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Display Mode", 12 },
	{ "graphics.value.borderless", L"Borderless", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Display Mode, a window without a border that covers the screen", 12 },
	{ "graphics.value.windowed", L"Windowed", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Display Mode", 12 },
	{ "graphics.value.default", L"Default", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Refresh Rate, the rate the display has by default", 12 },
	{ "graphics.value.trilinear", L"Trilinear", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Texture Filter, trilinear filtering", 12 },
	{ "graphics.value.aniso", L"Aniso %dx", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Texture Filter, anisotropic filtering, %d = 4, 8 or 16", 10 },
	{ "graphics.value.ao_half", L"Half", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): AO Quality, worked out at half of the resolution", 12 },
	{ "graphics.value.ao_full", L"Full", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): AO Quality, worked out at the full resolution", 12 },
	{ "graphics.value.fog_additive", L"Additive", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Volumetric Fog, the fog adds light where the sun reaches", 12 },
	{ "graphics.value.fog_darken", L"Darken", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Volumetric Fog, the fog darkens what is in the shadow", 12 },
	{ "graphics.value.distance_low", L"Low", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Draw Distance, 50 percent of the normal one", 12 },
	{ "graphics.value.distance_medium", L"Medium", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Draw Distance, 75 percent of the normal one", 12 },
	{ "graphics.value.distance_normal", L"Normal", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Draw Distance, as in the original game", 12 },
	{ "graphics.value.distance_far", L"Far", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Draw Distance, 125 percent of the normal one", 12 },
	{ "graphics.value.distance_veryfar", L"Very Far", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Draw Distance, 150 percent of the normal one", 12 },
	{ "graphics.value.grass_low", L"Low", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Grass Density, less grass than in the original game", 12 },
	{ "graphics.value.grass_normal", L"Normal", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Grass Density, as in the original game", 12 },
	{ "graphics.value.grass_high", L"High", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Grass Density, more grass", 12 },
	{ "graphics.value.grass_ultra", L"Ultra", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Grass Density, the most grass", 12 },
	{ "graphics.value.percent", L"%d percent", "value in the Graphics, Detail and Effects pages of the options (big comic font, capitals only; the box is sized for '0000 x 0000', keep it short): Draw Distance set from the command line, %d = percent of the normal distance (the word, as in the English text)", 11 },
	{ "graphics.info.restart", L"Applies at the next start: %s", "line below the rows of the Graphics, Detail and Effects pages of the options (small font, one row of about 55 characters), %s = the names of the rows that need a restart, with commas ('Resolution, HD Textures')", 32 },
	{ "graphics.info.windowed", L"Windowed while the game is started with -windowed", "line below the rows of the Graphics, Detail and Effects pages of the options (small font, one row of about 55 characters): Fullscreen is picked but the game runs in a window, -windowed is a command line switch and stays as it is", 55 },
	{ "graphics.info.minigame", L"A minigame sets its own draw distance", "line below the rows of the Graphics, Detail and Effects pages of the options (small font, one row of about 55 characters): the Draw Distance row can't be changed in a minigame", 55 },
	{ "xinput.y", L"[Y]", "name of the Y button of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.b", L"[B]", "name of the B button of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.a", L"[A]", "name of the A button of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.x", L"[X]", "name of the X button of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.lb", L"[LB]", "name of the left bumper of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.lt", L"[LT]", "name of the left trigger of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.rb", L"[RB]", "name of the right bumper of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.rt", L"[RT]", "name of the right trigger of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.back", L"[Back]", "name of the Back button of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.start", L"[Start]", "name of the Start button of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.ls", L"[LS]", "name of the left stick button (pressing the left stick) of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },
	{ "xinput.rs", L"[RS]", "name of the right stick button (pressing the right stick) of an Xbox controller in the button hints of the texts (the fonts have no pictures of the Xbox buttons). It normally stays as it is; keep the square brackets", 8 },

	// LAN minigames
	{ "coopmini.menu", L"LAN Minigames", "Pause menu and LAN minigame selection title", 22 },
	{ "coopmini.chicken", L"Chicken Launch", "Minigame name", 22 },
	{ "coopmini.whack", L"Whack-a-Rac", "Minigame name", 22 },
	{ "coopmini.golf", L"Gopher Golf", "Minigame name", 22 },
	{ "coopmini.invite_title", L"LAN Invite", "Invitation popup title", 20 },
	{ "coopmini.result_title", L"LAN Results", "Results popup title", 20 },
	{ "coopmini.invite", L"Your partner invites you to %s.\nTake turns on your own computers.\nAccept?", "Invitation popup, %s is the minigame title", 125 },
	{ "coopmini.draw", L"It's a draw!", "Result of a tied match", 22 },
	{ "coopmini.host_wins", L"P1 wins!", "The host won the match", 22 },
	{ "coopmini.guest_wins", L"P2 wins!", "The guest won the match", 22 },
	{ "coopmini.results", L"%s\nP1 (host): %d\nP2 (guest): %d\n%s\nOK: both return to the farm", "Scorecard, game title, host score, guest score and winner line in that order", 135 },
	{ "coopmini.wait_reply", L"LAN: %s - waiting for a reply", "HUD after inviting, %s is the minigame title", 60 },
	{ "coopmini.wait_players", L"LAN: %s - waiting for both players", "HUD while loading, %s is the minigame title", 65 },
	{ "coopmini.playing", L"LAN: %s | P1: %d   P2: %d\nRound %u - %s", "HUD, title, host score, guest score, round number and turn status", 115 },
	{ "coopmini.your_turn", L"Your turn", "HUD current turn status", 36 },
	{ "coopmini.partner_turn", L"Your partner is playing", "HUD current turn status", 36 },
	{ "coopmini.leaving", L"LAN: returning to the farm", "HUD while returning to the farm", 36 },
	{ "coopmini.mud", L"Mud Jumpers", "LAN minigame title", 22 },
	{ "coopmini.darts", L"Darts", "LAN minigame title", 22 },
	{ "coopmini.watching", L"Watching your partner", "LAN minigame spectator HUD", 36 },
	{ "controls.pad_icons", L"Controller icons", "Control options row", 24 },
	{ "controls.pad_icons_auto", L"Automatic", "Choose controller icons based on the input device", 14 },
	{ "coopmini.mud_invite", L"Your partner invites you to %s.\nPlay against each other at the same time, with two AI opponents.\nAccept?", "Simultaneous Mud Jumpers invitation, %s is the minigame title", 150 },
	{ "coopmini.mud_playing", L"LAN: %s | P1: %d   P2: %d\nPlaying together - you control P%u", "Simultaneous Mud Jumpers HUD: title, host score, guest score, local player number", 125 },
	{ "coopmini.host_player", L"P1 / Host", "LAN versus", 220 },
	{ "coopmini.guest_player", L"P2 / Guest", "LAN versus", 220 },
	{ "coopmini.darts_invite", L"Your partner invites you to %s.\nTake turns throwing one dart at a time on the same board.\nAccept?", "LAN versus", 220 },
	{ "coopmini.darts_playing", L"LAN: %s | P1: %d   P2: %d\nRound %u - %s", "LAN versus", 220 },
	{ "coopmini.duel_invite", L"Your partner invites you to %s.\nPlay the same challenge on your own computers and compare your scores or finish times.\nAccept?", "LAN versus", 220 },
	{ "coopmini.duel_playing", L"LAN duel: %s | P1: %d   P2: %d\n%s", "LAN versus", 220 },
	{ "coopmini.duel_running", L"Complete your challenge", "LAN versus", 220 },
	{ "coopmini.duel_waiting", L"Finished - waiting for your partner", "LAN versus", 220 },
	{ "coopmini.time_results", L"%s\nP1 (host): %s\nP2 (guest): %s\n%s\nOK: both return to the farm", "LAN versus", 220 },
	{ "coopmini.next_page", L"Next page", "LAN versus", 220 },
	{ "coopmini.dnf", L"Did not finish", "LAN versus", 220 },
	{ "coopmini.pool_invite", L"Your partner invites you to %s.\nTake turns shooting on the same pool table.\nAccept?", "LAN versus", 220 },
	{ "coopmini.pool_playing", L"LAN: %s | P1: %d   P2: %d\nShot %u - %s", "LAN versus", 220 },
	{ "coopmini.time_playing", L"LAN time trial: %s\nP1: %s   P2: %s | %s", "LAN versus", 220 },
};

// One entry for every MODSTRING, in the order of the enum
TSTATICASSERT( TARRAYSIZE( s_aModStrings ) == MODSTRING_NUMOF );

// The texts of the current language (TNULL: English) and of the previous one, which stay readable until the next load
static TWCHAR* s_apTexts[ MODSTRING_NUMOF ];
static TWCHAR* s_apOldTexts[ MODSTRING_NUMOF ];

const TWCHAR* AModStrings::Get( MODSTRING a_eString )
{
	TASSERT( a_eString >= 0 && a_eString < MODSTRING_NUMOF );
	return s_apTexts[ a_eString ] ? s_apTexts[ a_eString ] : s_aModStrings[ a_eString ].wszEnglish;
}

const TWCHAR* AModStrings::GetEnglish( MODSTRING a_eString )
{
	return s_aModStrings[ a_eString ].wszEnglish;
}

const TCHAR* AModStrings::GetId( MODSTRING a_eString )
{
	return s_aModStrings[ a_eString ].szId;
}

TINT AModStrings::Format( TWCHAR* a_wszOut, TINT a_iSize, const TWCHAR* a_wszFormat, ... )
{
	TASSERT( a_wszOut && a_iSize > 0 && a_wszFormat );

	va_list args;
	va_start( args, a_wszFormat );

	// NOTE: not T2String16::FormatV, it asserts when the text doesn't fit. -1: it didn't fit and has no end
	TINT iLength = _vsnwprintf( a_wszOut, a_iSize, a_wszFormat, args );

	va_end( args );

	if ( iLength < 0 || iLength >= a_iSize )
		iLength = a_iSize - 1;

	a_wszOut[ iLength ] = L'\0';
	return iLength;
}

TINT AModStrings::GetNumTranslated()
{
	TINT iNum = 0;

	for ( TINT i = 0; i < MODSTRING_NUMOF; i++ )
		iNum += s_apTexts[ i ] ? 1 : 0;

	return iNum;
}

static void FreeTexts( TWCHAR* ( &a_rTexts )[ MODSTRING_NUMOF ] )
{
	for ( TINT i = 0; i < MODSTRING_NUMOF; i++ )
	{
		if ( a_rTexts[ i ] )
		{
			TFree( a_rTexts[ i ] );
			a_rTexts[ i ] = TNULL;
		}
	}
}

void AModStrings::Unload()
{
	FreeTexts( s_apTexts );
	FreeTexts( s_apOldTexts );
}

// The printf conversions of a text ("ds" for "%d ... %s"), %% isn't one
static void GetPrintfCodes( const TWCHAR* a_wszText, TCHAR* a_szOut, TINT a_iOutSize )
{
	TINT iLength = 0;

	for ( const TWCHAR* p = a_wszText; *p != L'\0'; p++ )
	{
		if ( *p != L'%' )
			continue;

		p++;

		if ( *p == L'%' )
			continue;

		// Flags, width, precision and size, then the conversion
		while ( *p != L'\0' && ( ( *p >= L'0' && *p <= L'9' ) || *p == L'.' || *p == L'-' || *p == L'+' || *p == L' ' || *p == L'#' || *p == L'l' || *p == L'h' ) )
			p++;

		if ( *p == L'\0' )
		{
			// A lone % at the end
			if ( iLength < a_iOutSize - 1 )
				a_szOut[ iLength++ ] = '!';

			break;
		}

		if ( iLength < a_iOutSize - 1 )
			a_szOut[ iLength++ ] = ( *p < 0x80 ) ? TCHAR( *p ) : '?';
	}

	a_szOut[ iLength ] = '\0';
}

//-----------------------------------------------------------------------------
// A small JSON reader for the files of the texts: the string members of the top level object, or the "id" and
// "hu" (or "text", "translation", "tr") members of the objects of the top level list
//-----------------------------------------------------------------------------
class AModStringsJSONReader
{
public:
	static constexpr TINT KEY_SIZE   = 128;
	static constexpr TINT VALUE_SIZE = 2048;

	AModStringsJSONReader( const TCHAR* a_pText, TSIZE a_uiSize )
	    : m_p( a_pText ), m_pEnd( a_pText + a_uiSize ) {}

	// Calls a_fnOnString( id, text, length of the text ) for every text. TFALSE if the file is broken
	template <class FN>
	TBOOL Read( FN a_fnOnString )
	{
		// UTF-8 BOM
		if ( m_pEnd - m_p >= 3 && TUINT8( m_p[ 0 ] ) == 0xEF && TUINT8( m_p[ 1 ] ) == 0xBB && TUINT8( m_p[ 2 ] ) == 0xBF )
			m_p += 3;

		SkipSpaces();

		if ( m_p < m_pEnd && *m_p == '[' )
			return ReadList( a_fnOnString );

		return ReadMembers( [ & ]( const TWCHAR* a_wszKey ) {
			if ( m_p < m_pEnd && *m_p == '"' )
			{
				TINT iLength;

				if ( !ReadString( m_wszValue, VALUE_SIZE, iLength ) )
					return TFALSE;

				a_fnOnString( a_wszKey, m_wszValue, iLength );
				return TTRUE;
			}

			return SkipValue( 0 );
		} );
	}

private:
	// [ { "id": ..., "hu": ... }, ... ]
	template <class FN>
	TBOOL ReadList( FN a_fnOnString )
	{
		m_p++;
		SkipSpaces();

		if ( Accept( ']' ) )
			return TTRUE;

		while ( TTRUE )
		{
			SkipSpaces();

			if ( m_p < m_pEnd && *m_p == '{' )
			{
				TWCHAR wszId[ KEY_SIZE ];
				TINT   iLength  = -1;
				wszId[ 0 ]      = L'\0';
				m_wszText[ 0 ]  = L'\0';

				const TBOOL bRead = ReadMembers( [ & ]( const TWCHAR* a_wszKey ) {
					if ( m_p >= m_pEnd || *m_p != '"' )
						return SkipValue( 0 );

					TINT iValueLength;

					if ( !ReadString( m_wszValue, VALUE_SIZE, iValueLength ) )
						return TFALSE;

					if ( TStringManager::String16Compare( a_wszKey, L"id" ) == 0 )
					{
						TStringManager::String16CopySafe( wszId, m_wszValue, KEY_SIZE );
					}
					else if ( iLength < 0 && ( TStringManager::String16Compare( a_wszKey, L"hu" ) == 0 || TStringManager::String16Compare( a_wszKey, L"text" ) == 0 ||
					                           TStringManager::String16Compare( a_wszKey, L"translation" ) == 0 || TStringManager::String16Compare( a_wszKey, L"tr" ) == 0 ) )
					{
						TStringManager::String16CopySafe( m_wszText, m_wszValue, VALUE_SIZE );
						iLength = iValueLength;
					}

					return TTRUE;
				} );

				if ( !bRead )
					return TFALSE;

				if ( wszId[ 0 ] != L'\0' && iLength >= 0 )
					a_fnOnString( wszId, m_wszText, iLength );
			}
			else if ( !SkipValue( 0 ) )
			{
				return TFALSE;
			}

			SkipSpaces();

			if ( Accept( ',' ) )
				continue;

			return Accept( ']' );
		}
	}

	// { "key": value, ... }: a_fnOnMember( key ) reads the value
	template <class FN>
	TBOOL ReadMembers( FN a_fnOnMember )
	{
		if ( !Accept( '{' ) )
			return TFALSE;

		SkipSpaces();

		if ( Accept( '}' ) )
			return TTRUE;

		while ( TTRUE )
		{
			SkipSpaces();

			TWCHAR wszKey[ KEY_SIZE ];
			TINT   iKeyLength;

			if ( !ReadString( wszKey, KEY_SIZE, iKeyLength ) )
				return TFALSE;

			SkipSpaces();

			if ( !Accept( ':' ) )
				return TFALSE;

			SkipSpaces();

			if ( !a_fnOnMember( wszKey ) )
				return TFALSE;

			SkipSpaces();

			if ( Accept( ',' ) )
				continue;

			return Accept( '}' );
		}
	}

	void SkipSpaces()
	{
		while ( m_p < m_pEnd && ( *m_p == ' ' || *m_p == '\t' || *m_p == '\r' || *m_p == '\n' ) )
			m_p++;
	}

	TBOOL Accept( TCHAR a_cChar )
	{
		if ( m_p < m_pEnd && *m_p == a_cChar )
		{
			m_p++;
			return TTRUE;
		}

		return TFALSE;
	}

	static TINT HexValue( TCHAR a_cChar )
	{
		if ( a_cChar >= '0' && a_cChar <= '9' ) return a_cChar - '0';
		if ( a_cChar >= 'a' && a_cChar <= 'f' ) return a_cChar - 'a' + 10;
		if ( a_cChar >= 'A' && a_cChar <= 'F' ) return a_cChar - 'A' + 10;
		return -1;
	}

	// A JSON string into UTF-16 (cut at a_iSize - 1 units)
	TBOOL ReadString( TWCHAR* a_wszOut, TINT a_iSize, TINT& a_rLength )
	{
		a_rLength = 0;

		if ( !Accept( '"' ) )
			return TFALSE;

		while ( m_p < m_pEnd )
		{
			const TUINT8 c      = TUINT8( *m_p++ );
			TUINT        uiChar = 0;

			if ( c == '"' )
			{
				a_wszOut[ a_rLength ] = L'\0';
				return TTRUE;
			}
			else if ( c == '\\' )
			{
				if ( m_p >= m_pEnd )
					return TFALSE;

				const TCHAR cEscape = *m_p++;

				switch ( cEscape )
				{
					case 'n': uiChar = '\n'; break;
					case 't': uiChar = '\t'; break;
					case 'r': uiChar = '\r'; break;
					case 'b': uiChar = '\b'; break;
					case 'f': uiChar = '\f'; break;
					case 'u':
					{
						if ( m_pEnd - m_p < 4 )
							return TFALSE;

						for ( TINT i = 0; i < 4; i++ )
						{
							const TINT iDigit = HexValue( m_p[ i ] );

							if ( iDigit < 0 )
								return TFALSE;

							uiChar = ( uiChar << 4 ) | TUINT( iDigit );
						}

						// A surrogate pair stays two UTF-16 units
						m_p += 4;
						break;
					}
					default: uiChar = TUINT8( cEscape ); break; // " \ /
				}
			}
			else if ( c < 0x80 )
			{
				uiChar = c;
			}
			else
			{
				// UTF-8
				const TINT iMore = ( c >= 0xF0 ) ? 3 : ( c >= 0xE0 ) ? 2 : ( c >= 0xC0 ) ? 1 : -1;

				if ( iMore < 0 || m_pEnd - m_p < iMore )
					return TFALSE;

				uiChar = c & ( 0x3F >> iMore );

				for ( TINT i = 0; i < iMore; i++ )
					uiChar = ( uiChar << 6 ) | ( TUINT8( *m_p++ ) & 0x3F );
			}

			if ( uiChar >= 0x10000 )
			{
				// The fonts have no such characters, they're kept as a pair anyway
				uiChar -= 0x10000;

				if ( a_rLength < a_iSize - 2 )
				{
					a_wszOut[ a_rLength++ ] = TWCHAR( 0xD800 + ( uiChar >> 10 ) );
					a_wszOut[ a_rLength++ ] = TWCHAR( 0xDC00 + ( uiChar & 0x3FF ) );
				}
			}
			else if ( a_rLength < a_iSize - 1 )
			{
				a_wszOut[ a_rLength++ ] = TWCHAR( uiChar );
			}
		}

		return TFALSE;
	}

	// Any value: string, number, true, false, null, object or list
	TBOOL SkipValue( TINT a_iDepth )
	{
		if ( a_iDepth > 64 || m_p >= m_pEnd )
			return TFALSE;

		if ( *m_p == '"' )
		{
			TINT iLength;
			return ReadString( m_wszValue, VALUE_SIZE, iLength );
		}

		if ( *m_p == '{' || *m_p == '[' )
		{
			const TCHAR cClose = ( *m_p == '{' ) ? '}' : ']';
			m_p++;
			SkipSpaces();

			if ( Accept( cClose ) )
				return TTRUE;

			while ( TTRUE )
			{
				SkipSpaces();

				if ( cClose == '}' )
				{
					TINT iLength;

					if ( !ReadString( m_wszValue, VALUE_SIZE, iLength ) )
						return TFALSE;

					SkipSpaces();

					if ( !Accept( ':' ) )
						return TFALSE;

					SkipSpaces();
				}

				if ( !SkipValue( a_iDepth + 1 ) )
					return TFALSE;

				SkipSpaces();

				if ( Accept( ',' ) )
					continue;

				return Accept( cClose );
			}
		}

		// A number or a literal
		const TCHAR* pStart = m_p;

		while ( m_p < m_pEnd && *m_p != ',' && *m_p != '}' && *m_p != ']' && *m_p != ' ' && *m_p != '\t' && *m_p != '\r' && *m_p != '\n' )
			m_p++;

		return m_p != pStart;
	}

private:
	const TCHAR* m_p;
	const TCHAR* m_pEnd;
	TWCHAR       m_wszValue[ VALUE_SIZE ];
	TWCHAR       m_wszText[ VALUE_SIZE ];
};

// Reads a whole file, TNULL if there's no such file (TFree the result)
static TCHAR* ReadWholeFile( const TString8& a_rcFileName, TSIZE& a_ruiSize )
{
	TFile* pFile = TFile::Create( a_rcFileName, TFILEMODE_READ );

	if ( !pFile )
		return TNULL;

	a_ruiSize    = pFile->GetSize();
	TCHAR* pText = ( a_ruiSize > 0 && a_ruiSize < 64 * 1024 * 1024 ) ? TSTATICCAST( TCHAR, TMalloc( a_ruiSize ) ) : TNULL;

	if ( pText && pFile->Read( pText, a_ruiSize ) != a_ruiSize )
	{
		TFree( pText );
		pText = TNULL;
	}

	pFile->Destroy();

	if ( !pText )
		TWARN( "AModStrings: couldn't read %s\n", a_rcFileName.GetString() );

	return pText;
}

void AModStrings::Load( const TCHAR* a_szLocaleFileName )
{
	// The texts of the language before the previous one can go
	FreeTexts( s_apOldTexts );

	for ( TINT i = 0; i < MODSTRING_NUMOF; i++ )
	{
		s_apOldTexts[ i ] = s_apTexts[ i ];
		s_apTexts[ i ]    = TNULL;
	}

	// "Data/Locale/hun.trb" -> "Data/Locale/hun_mod.json", then "Data/Locale/mod_strings_hun.json"
	TString8   strBase  = a_szLocaleFileName;
	const TINT iDot     = strBase.FindReverse( '.', -1 );
	const TINT iSlash   = strBase.FindReverse( '/', -1 );

	if ( iDot > iSlash )
		strBase.Truncate( iDot );

	TString8 strFileName = strBase;
	strFileName += "_mod.json";

	TSIZE  uiSize = 0;
	TCHAR* pText  = ReadWholeFile( strFileName, uiSize );

	if ( !pText )
	{
		strFileName = strBase;
		strFileName.Truncate( iSlash + 1 );
		strFileName += "mod_strings_";
		strFileName += strBase.GetString() + iSlash + 1;
		strFileName += ".json";

		pText = ReadWholeFile( strFileName, uiSize );
	}

	if ( !pText )
		return;

	TINT iNumWrongCodes = 0;

	AModStringsJSONReader* pReader = new AModStringsJSONReader( pText, uiSize );

	const TBOOL bValid = pReader->Read( [ & ]( const TWCHAR* a_wszKey, const TWCHAR* a_wszValue, TINT a_iValueLength ) {
		// The id, "mod:" in front is optional
		TCHAR szKey[ AModStringsJSONReader::KEY_SIZE ];
		TStringManager::StringUnicodeToChar( szKey, a_wszKey, -1 );

		const TCHAR* szId = ( TStringManager::String8CompareNoCase( szKey, "mod:", 4 ) == 0 ) ? szKey + 4 : szKey;

		for ( TINT i = 0; i < MODSTRING_NUMOF; i++ )
		{
			if ( TStringManager::String8Compare( szId, s_aModStrings[ i ].szId ) != 0 )
				continue;

			// The same printf codes as the English text, or the text would break the formatting
			TCHAR szCodes[ 16 ];
			TCHAR szEnglishCodes[ 16 ];
			GetPrintfCodes( a_wszValue, szCodes, sizeof( szCodes ) );
			GetPrintfCodes( s_aModStrings[ i ].wszEnglish, szEnglishCodes, sizeof( szEnglishCodes ) );

			if ( TStringManager::String8Compare( szCodes, szEnglishCodes ) != 0 )
			{
				TWARN( "AModStrings: %s: the codes (%s) aren't those of the English text (%s), it stays English\n", szId, szCodes, szEnglishCodes );
				iNumWrongCodes++;
				break;
			}

			if ( a_iValueLength == 0 )
				break;

			if ( s_apTexts[ i ] )
				TFree( s_apTexts[ i ] );

			s_apTexts[ i ] = TSTATICCAST( TWCHAR, TMalloc( ( a_iValueLength + 1 ) * sizeof( TWCHAR ) ) );
			TStringManager::String16CopySafe( s_apTexts[ i ], a_wszValue, a_iValueLength + 1 );
			break;
		}
	} );

	delete pReader;
	TFree( pText );

	if ( !bValid )
		TWARN( "AModStrings: %s isn't valid JSON, the texts read before the error are used\n", strFileName.GetString() );

	TINFO( "AModStrings: %d of the %d texts of the mod from %s%s\n", GetNumTranslated(), TINT( MODSTRING_NUMOF ), strFileName.GetString(), iNumWrongCodes ? " (some have wrong codes)" : "" );
}
