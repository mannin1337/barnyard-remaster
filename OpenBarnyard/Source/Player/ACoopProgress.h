#pragma once

//-----------------------------------------------------------------------------
// Purpose: the story in network co-op (ACoopNet, not in retail). Each of the
// two games plays its own story, this keeps them together:
//  - twice a second each game tells the other how far its story is: the act and
//    the tier, the completed quests of every tier, the assigned quest and the
//    time of the day (CHANNEL_PROGRESS);
//  - a quest the other player has completed and this one hasn't can be skipped
//    here 45 seconds (real time) after this game learned about it. A line at
//    the top of the screen says so: holding K or the left stick button skips
//    (both stick buttons where the left one does something else: the bike's
//    bell, the horn of the joy rides), a short press hides the line until the
//    other player completes something else. One skip catches up with every
//    quest the other player had completed by then, tier by tier (the scenes
//    between the tiers play as usual). What the other player had already done
//    when the two stories met (a game joined late, a new game next to a later
//    one) can be caught up with right away;
//  - a skip is applied on the farm or in the barn while nothing else is going
//    on there (no conversation, quest scene, menu, micro game or loading). The
//    quest ends the way a completed quest does (AQuestManager::RemoveQuest):
//    completed in its tier, its quest event for the scripts, its offer and its
//    spawns gone (on the farm once it's back, for a skip in the barn), the next
//    tier once the tier is complete, and what it gives for good
//    (AGameLoader::GiveQuestRewards). Its end scenes don't play;
//  - in the minigame of the assigned quest (while the game itself runs) the
//    line offers that quest only: the skip ends the minigame as won
//    (AMiniGameState::EndAsWon), the story completes the quest the way a won
//    minigame does once it's back (with its end scenes). The other quests wait
//    for the farm or the barn;
//  - a quest going on here is undone first, the way the game gives up a quest
//    (AQuestManager::AbandonQuest): its "<quest>_uninit" script takes its items
//    back, detaches Pip from the player's head..., then it's completed. Sleepy
//    Cows frees the bike and the player as its end scene does; Root (Night)
//    Barn Detailing keeps the bought barn items (its script would take them);
//  - the end of the story can't be skipped, the line says so. The other game
//    hears about every skip (CHANNEL_SKIP) and shows it;
//  - the same quest going on in both games shares its steps: the scripted
//    steps and the places to find ("dummy", "locate" elements) the other player
//    has done are done here too, never the last one (the quest is completed by
//    playing it or by a skip), not the items to give nor the minigames;
//  - the game that joined follows the host's clock (the day phase decides the
//    quests on offer): on the farm, once they're more than a quarter of an
//    hour apart and no quest is going on there, from 6:00 of the host's day
//    (from the night into the day the dawn comes first: the day's animals and
//    quests). The date isn't followed;
//  - saves: when one game saves its game (the bed, the scenes that save, the
//    end of a quest...), the other one saves its own game into its own slot at
//    its next safe moment with no quest going on, and both say so. Each game
//    keeps its own save file and slot, picked by its player (New Game / Load
//    Game, which ask before a slot in use is overwritten), so both saves load as
//    single player games too.
// The games compare the quests by their index in AQuestManager, so they need the
// same quest data: a checksum of it goes with the progress, nothing can be
// skipped while it differs.
//
// It registers itself with ACoopNet at startup and only runs while a network
// session does. Development switches (not in Final builds):
//  "-cooplog"                 logs what is sent, received, offered and applied
//  "-coopskip [s]"            accepts the offer s seconds after it is shown (3)
//  "-coopdelay s"             offers a skip s seconds after this game learned
//                             about the quest instead of 45 (quicker tests)
//  "-coopcomplete s[:q][,..]" completes a quest s seconds after the farm is first
//                             up, as if the player did: quest index q, else the
//                             assigned quest, else the first open quest of the
//                             tier that can be skipped
//  "-coopassign s:q[,..]"     assigns the quest q s seconds after the farm is up
//  "-coopsave s[,..]"         saves the game s seconds after the farm is up (as
//                             the bed does), once no quest is assigned
//  "-coopminigame s:name"     starts the minigame name (AMiniGameManager's, as a
//                             quest giver does) s seconds after the farm is up;
//                             its instructions are dismissed 3 seconds after
//                             they're shown
//  "-coopgamestate s:name"    goes to the game state name from the farm (the barn:
//                             ABarnExploreMinigameState, as its door does)
//  "-coopfinish s"            leaves the barn (as its door does)
//  "-coopgive s:item"         gives the player an item of that type (a quest item
//                             it would have picked up)
//  "-cooptime s:h"            sets this game's clock to h o'clock
//  "-coopstep s:e"            completes the step (element, 1-based) e of the
//                             assigned quest, as its script would
//  "-coopshots <prefix>"      saves <prefix>_<event><n>.png frames of the prompt,
//                             of a skip, of a completion and of a save
// The times of the timed switches count from the first safe moment of the farm,
// they're applied at safe moments (farm or barn).
//-----------------------------------------------------------------------------
class ACoopProgress
{
public:
	// Seconds after this game learned that the other player completed a quest before it can be skipped here
	inline static constexpr TFLOAT SKIP_DELAY = 45.0f;

	// How long the skip button is held to skip (seconds). A shorter press hides the prompt
	inline static constexpr TFLOAT HOLD_TIME = 1.0f;

	// Progress messages sent per second
	inline static constexpr TFLOAT SEND_RATE = 2.0f;

	// The other game's progress is forgotten after this long without a message (seconds)
	inline static constexpr TFLOAT PROGRESS_TIMEOUT = 5.0f;

	// The story waits this long on the farm (in the barn, in a minigame) without anything going on before a skip is
	// applied or offered (seconds)
	inline static constexpr TFLOAT SETTLE_TIME = 1.5f;

	// Quests of AQuestManager this module can track
	inline static constexpr TINT MAX_QUESTS = 128;

public:
	// TTRUE while the story can change here: the farm (ASimState) or the barn (ABarnExploreMinigameState) is the current
	// state with nothing on top or inside it, no loading or minigame is coming, the player isn't busy with an
	// interaction and no quest scene runs. a_ppOutReason receives what is going on otherwise
	static TBOOL IsSafeMoment( const TCHAR** a_ppOutReason = TNULL );

	// Completes the quest a_iQuest (index in AQuestManager) the way the story ends a completed quest, without its
	// end scenes (see the top). Only when IsSafeMoment(). Returns TFALSE if it's unknown or already complete.
	// NOTE: a quest going on here should be undone first (the skips do it, see the top)
	static TBOOL CompleteQuest( TINT a_iQuest, const TCHAR* a_szReason );

	// Why the quest a_iQuest can't be skipped (a line for the player in the language of the game: the end of the
	// story), TNULL if it can
	static const TWCHAR* GetSkipBlockReason( TINT a_iQuest );

	// SaveLoadSKU: the game has been saved into the current save slot (not only the settings)
	static void OnGameSaved();

	// Development: logs the story and the items of this game (AGameLoader's -devsave / -devload lines)
	static void Dev_LogStory( const TCHAR* a_szTag );
};
