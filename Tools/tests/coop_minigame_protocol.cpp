#include "../../OpenBarnyard/Source/Player/ACoopMinigameProtocol.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace CoopMini;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
Command command(U32 action, const State& s, int score=0, bool final=false)
{
    Command c; c.action=action; c.match=s.match; c.turn=s.turn; c.score=score; c.final=final; return c;
}
int main()
{
    State host, guest;
    CHECK(!Bound(0,2,0,2)); CHECK(!Bound(11,22,12,22)); CHECK(!Bound(11,23,11,22)); CHECK(Bound(11,22,11,22));
    CHECK(!Begin(host,0,0)); CHECK(Begin(host,Chicken,0)); CHECK(!Begin(host,Chicken,0));
    CHECK(Newer(host,guest)); guest=host;
    CHECK(!Apply(host,0,command(Accept,host))); CHECK(Apply(host,1,command(Accept,host)));
    CHECK(host.phase==Loading); CHECK(!Apply(host,1,command(Accept,host)));
    CHECK(Apply(host,1,command(Ready,host))); CHECK(host.phase==Loading);
    CHECK(Apply(host,0,command(Ready,host))); CHECK(host.phase==Playing && host.player==0);
    const ViewStamp liveView{host.match, host.turn, host.game, 42};
    CHECK(CanWatchView(host, 1, liveView));
    CHECK(!CanWatchView(host, 0, liveView));
    CHECK(!CanWatchView(host, 2, liveView));
    CHECK(!CanWatchView(host, 1, ViewStamp{host.match, host.turn, Golf, 42}));
    CHECK(!CanWatchView(host, 1, ViewStamp{host.match + 1, host.turn, host.game, 42}));
    CHECK(!CanWatchView(host, 1, ViewStamp{host.match, host.turn + 1, host.game, 42}));
    CHECK(!CanWatchView(host, 1, ViewStamp{host.match, host.turn, host.game, 0}));
    CHECK(!NewerView(liveView, liveView));
    CHECK(NewerView(ViewStamp{host.match, host.turn, host.game, 43}, liveView));
    CHECK(!NewerView(ViewStamp{host.match, host.turn, host.game, 41}, liveView));
    CHECK(NewerView(ViewStamp{host.match, host.turn + 1, host.game, 1}, liveView));
    CHECK(PeerCanPlay(Golf, 0) && !PeerCanPlay(Mud, 0));
    // An older turn-based Mud peer must never negotiate the new simultaneous game.
    CHECK(!PeerCanPlay(Mud, ExpandedGames) && !PeerCanPlay(Darts, ExpandedGames));
    CHECK(!PeerCanPlay(Mud, MudRealtime)); // display snapshots are required too
    CHECK(PeerCanPlay(Mud, Spectator | ExpandedGames | MudRealtime));
    CHECK(!PeerCanPlay(Darts, Spectator | ExpandedGames | MudRealtime));
    CHECK(!PeerCanPlay(Darts, DartsPvp));
    CHECK(PeerCanPlay(Darts, Spectator | ExpandedGames | DartsPvp));
    CHECK(!PeerCanPlay(Darts + 1, ExpandedGames));
    CHECK(!Newer(guest,host)); CHECK(!Apply(host,1,command(RoundDone,host,999,true)));
    U32 serial=0;
    auto live=command(Score,host,30); live.sequence=2;
    CHECK(ApplyOrdered(host,0,live,serial)); CHECK(serial==2);
    live.sequence=4; CHECK(!ApplyOrdered(host,0,live,serial)); CHECK(serial==4);
    live.sequence=3; live.score=5; CHECK(!ApplyOrdered(host,0,live,serial)); CHECK(host.scores[0]==30);
    auto first=command(RoundDone,host,42,true);
    CHECK(Apply(host,0,first)); CHECK(host.turn==1 && host.player==1 && host.scores[0]==42);
    CHECK(!CanWatchView(host, 0, liveView));
    CHECK(!Apply(host,0,first)); CHECK(host.turn==1); // duplicated/lost ACK
    auto bad=command(RoundDone,host,100,true); bad.match--; CHECK(!Apply(host,1,bad));
    bad=command(RoundDone,host,-1,true); CHECK(!Apply(host,1,bad));
    bad=command(RoundDone,host,10000001,true); CHECK(!Apply(host,1,bad));
    CHECK(Apply(host,1,command(RoundDone,host,75,true))); CHECK(host.phase==Results && host.finished==3);
    CHECK(host.scores[0]==42 && host.scores[1]==75);
    CHECK(!Apply(host,1,command(Score,host,999))); // late live score cannot rewrite final result
    CHECK(Apply(host,1,command(Leave,host))); CHECK(host.phase==Leaving);
    CHECK(!Apply(host,0,command(Leave,host))); CHECK(Apply(host,0,command(Farm,host)));
    CHECK(Apply(host,1,command(Farm,host))); CHECK(host.farm==3);
    // Multi-round games alternate levels/holes, skipping a player who is finished.
    State rounds; CHECK(Begin(rounds,Whack,1)); CHECK(Apply(rounds,0,command(Accept,rounds)));
    CHECK(Apply(rounds,0,command(Ready,rounds))); CHECK(Apply(rounds,1,command(Ready,rounds)));
    CHECK(Apply(rounds,0,command(RoundDone,rounds,100))); CHECK(rounds.player==1 && rounds.turn==1);
    CHECK(Apply(rounds,1,command(RoundDone,rounds,80,true))); CHECK(rounds.player==0 && rounds.finished==2);
    CHECK(Apply(rounds,0,command(RoundDone,rounds,200))); CHECK(rounds.player==0 && rounds.turn==3);
    CHECK(Apply(rounds,0,command(RoundDone,rounds,240,true))); CHECK(rounds.phase==Results);
    State reconnect=rounds; reconnect.match++; reconnect.phase=Playing;
    CHECK(!Newer(reconnect,rounds)); // no joining mid-match by replaying a snapshot
    reconnect.phase=Offer; CHECK(Newer(reconnect,rounds));
    State cancelled; CHECK(Begin(cancelled,Golf,0)); CHECK(Apply(cancelled,1,command(Reject,cancelled)));
    CHECK(cancelled.phase==Leaving && cancelled.ready==0);
    // The guest can miss every Offer datagram when it sends the invitation.
    State acceptingHost, waitingGuest;
    CHECK(Begin(acceptingHost,Golf,1));
    CHECK(Apply(acceptingHost,0,command(Accept,acceptingHost)));
    CHECK(!Newer(acceptingHost,waitingGuest));
    CHECK(!Newer(acceptingHost,waitingGuest,Whack));
    CHECK(Newer(acceptingHost,waitingGuest,Golf));
    waitingGuest=acceptingHost;
    CHECK(Apply(acceptingHost,1,command(Ready,waitingGuest)));
    CHECK(Apply(acceptingHost,0,command(Ready,acceptingHost)));
    CHECK(Newer(acceptingHost,waitingGuest));
    CHECK(acceptingHost.phase==Playing);
    State unseenGuest;
    CHECK(Newer(cancelled,unseenGuest));
    unseenGuest=cancelled;
    CHECK(Apply(cancelled,0,command(Farm,cancelled)));
    CHECK(Apply(cancelled,1,command(Farm,unseenGuest)));
    CHECK(cancelled.farm==3);
    // Mud is one host-owned match: guest score/round messages cannot take a turn
    // or write either result, and both final scores appear in one revision.
    State mud; CHECK(Begin(mud,Mud,1)); CHECK(Apply(mud,0,command(Accept,mud)));
    CHECK(Apply(mud,0,command(Ready,mud))); CHECK(Apply(mud,1,command(Ready,mud)));
    CHECK(!Apply(mud,0,command(Score,mud,55)));
    CHECK(!Apply(mud,1,command(RoundDone,mud,999,true)));
    CHECK(!CompleteMud(mud,1,60,80)); CHECK(!CompleteMud(mud,0,-1,80));
    CHECK(!CompleteMud(mud,0,60,10000001));
    CHECK(mud.phase==Playing && mud.turn==0 && mud.finished==0);
    CHECK(!SetMudScores(mud,1,20,30)); CHECK(!SetMudScores(mud,0,20,-1));
    CHECK(SetMudScores(mud,0,20,30)); CHECK(mud.scores[0]==20 && mud.scores[1]==30);
    CHECK(!SetMudScores(mud,0,20,30));
    const ViewStamp mudView{mud.match,0,Mud,1};
    CHECK(CanWatchView(mud,1,mudView)); CHECK(!CanWatchView(mud,0,mudView));

    // Event counters recover a dropped tap even after release, reject duplicate
    // or reordered datagrams, and keep held input neutral after 500 ms silence.
    MudInputBuffer input;
    MudInputPacket packet; packet.sender=11; packet.receiver=22;
    packet.match=mud.match; packet.sequence=2; packet.jumpPresses=1;
    MudControls controls;
    CHECK(!AcceptMudInput(mud,1,&packet,sizeof(packet),11,22,100,input));
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet)-1,11,22,100,input));
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet)+1,11,22,100,input));
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),12,22,100,input));
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,23,100,input));
    packet.match++; CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,100,input)); packet.match--;
    packet.magic=0; CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,100,input)); packet.magic=MUD_INPUT_MAGIC;
    packet.jumpHeld=2; CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,100,input)); packet.jumpHeld=0;
    packet.kickMode=3; CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,100,input)); packet.kickMode=0;
    packet.jumpPresses=257; CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,100,input)); packet.jumpPresses=1;
    CHECK(AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,100,input));
    CHECK(ConsumeMudInput(mud,input,110,controls));
    CHECK(controls.jumpPressed && !controls.jumpHeld && !controls.kickPressed);
    CHECK(ConsumeMudInput(mud,input,120,controls) && !controls.jumpPressed);
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,140,input));
    packet.sequence=1; CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,150,input));
    packet.sequence=3; packet.jumpPresses=0;
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,160,input));
    packet.jumpPresses=2; packet.jumpHeld=1; packet.kickPresses=1; packet.kickMode=2;
    CHECK(AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,200,input));
    CHECK(ConsumeMudInput(mud,input,220,controls));
    CHECK(controls.jumpPressed && controls.jumpHeld && controls.kickPressed && controls.directionalKick && !controls.kickLeft);
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,600,input)); // duplicate cannot refresh expiry
    CHECK(!ConsumeMudInput(mud,input,701,controls));
    CHECK(!controls.jumpHeld && !controls.jumpPressed && !controls.kickPressed);
    packet.sequence=4; packet.jumpHeld=0;
    CHECK(AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,710,input));
    CHECK(ConsumeMudInput(mud,input,720,controls) && !controls.jumpPressed && !controls.kickPressed && !controls.jumpHeld);
    // Wrong match/nonce with a large sequence cannot poison ordering.
    packet.sequence=100; packet.match++;
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,730,input)); packet.match--;
    packet.receiver=23; CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,730,input)); packet.receiver=22;
    packet.sequence=5; packet.kickPresses=2; packet.kickMode=1;
    CHECK(AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,740,input));
    CHECK(ConsumeMudInput(mud,input,750,controls) && controls.kickPressed && controls.directionalKick && controls.kickLeft);
    packet.sequence=6; packet.jumpPresses=3; packet.jumpHeld=1;
    CHECK(AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,800,input));
    CHECK(!ConsumeMudInput(mud,input,1301,controls) && !controls.jumpPressed && !controls.jumpHeld);
    packet.sequence=7;
    CHECK(AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,1310,input));
    CHECK(ConsumeMudInput(mud,input,1320,controls) && !controls.jumpPressed && controls.jumpHeld);
    State otherGame=mud; otherGame.game=Chicken;
    CHECK(!CompleteMud(otherGame,0,1,2)); CHECK(!SetMudScores(otherGame,0,1,2));
    packet.sequence=8; CHECK(!AcceptMudInput(otherGame,0,&packet,sizeof(packet),11,22,1330,input));
    const auto mudRevision=mud.revision;
    CHECK(CompleteMud(mud,0,60,80));
    CHECK(mud.phase==Results && mud.finished==3 && mud.turn==0 && mud.player==0);
    CHECK(mud.revision==mudRevision+1 && mud.scores[0]==60 && mud.scores[1]==80);
    CHECK(CanWatchView(mud,1,mudView)); // the last native pose may arrive after results
    CHECK(!CompleteMud(mud,0,1,2));
    CHECK(!SetMudScores(mud,0,999,999));
    CHECK(!AcceptMudInput(mud,0,&packet,sizeof(packet),11,22,1340,input));
    CHECK(!ConsumeMudInput(mud,input,1340,controls) && !controls.jumpHeld && !controls.kickPressed);
    CHECK(!CompleteMud(host,0,1,2)); // turn-based games do not expose this authority path
    CHECK(!CanWatchView(host,1,ViewStamp{host.match,host.turn,host.game,1}));
    CHECK(Apply(mud,1,command(Leave,mud))); CHECK(!CanWatchView(mud,1,mudView));
    // Darts has one host board. Each input edge belongs to one native throw AND
    // one aiming stage; a delayed X tap must never lock Y or a later dart.
    State darts; CHECK(Begin(darts,Darts,0)); CHECK(Apply(darts,1,command(Accept,darts)));
    CHECK(Apply(darts,0,command(Ready,darts))); CHECK(Apply(darts,1,command(Ready,darts)));
    CHECK(!Apply(darts,0,command(Score,darts,999)));
    CHECK(!Apply(darts,1,command(RoundDone,darts,999,true)));
    DartsTurn throwState;
    CHECK(!SetDartsState(darts,1,throwState,1,0,2,1,10,20));
    CHECK(!SetDartsState(darts,0,throwState,2,0,2,1,10,20));
    CHECK(!SetDartsState(darts,0,throwState,1,3,2,1,10,20));
    CHECK(!SetDartsState(darts,0,throwState,1,0,0,1,10,20));
    CHECK(!SetDartsState(darts,0,throwState,1,0,2,3,10,20));
    CHECK(!SetDartsState(darts,0,throwState,1,0,2,1,-1,20));
    CHECK(SetDartsState(darts,0,throwState,1,0,0,0,10,20));
    CHECK(SetDartsState(darts,0,throwState,1,0,2,1,10,20));
    CHECK(darts.player==1 && darts.turn==0 && darts.scores[0]==10 && darts.scores[1]==20);
    CHECK(!SetDartsState(darts,0,throwState,1,0,2,1,10,20));
    const ViewStamp dartsView{darts.match,0,Darts,1};
    CHECK(CanWatchView(darts,1,dartsView)); CHECK(!CanWatchView(darts,0,dartsView));
    CHECK(!CanWatchView(darts,2,dartsView));
    DartsInputBuffer dartInput;
    DartsInputPacket tap; tap.sender=11; tap.receiver=22; tap.match=darts.match;
    tap.sequence=2; tap.throwSerial=2; tap.stage=1;
    CHECK(!AcceptDartsInput(darts,1,throwState,&tap,sizeof(tap),11,22,100,dartInput));
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap)-1,11,22,100,dartInput));
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap)+1,11,22,100,dartInput));
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),12,22,100,dartInput));
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,23,100,dartInput));
    tap.magic=0; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,100,dartInput)); tap.magic=DARTS_INPUT_MAGIC;
    tap.match++; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,100,dartInput)); tap.match--;
    tap.stage=0; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,100,dartInput));
    tap.stage=2; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,100,dartInput));
    tap.stage=1; tap.throwSerial=4; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,100,dartInput)); tap.throwSerial=2;
    CHECK(AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,100,dartInput));
    CHECK(ConsumeDartsInput(darts,throwState,dartInput,110));
    CHECK(!ConsumeDartsInput(darts,throwState,dartInput,120));
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,130,dartInput));
    tap.sequence=3; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,140,dartInput));
    CHECK(SetDartsState(darts,0,throwState,1,0,2,2,10,20));
    CHECK(!ConsumeDartsInput(darts,throwState,dartInput,150));
    tap.sequence=100; // stale tuple must not poison later sequence acceptance
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,160,dartInput));
    tap.sequence=4; tap.stage=2;
    CHECK(AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,170,dartInput));
    tap.sequence=3; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,180,dartInput));
    CHECK(ConsumeDartsInput(darts,throwState,dartInput,190));
    CHECK(SetDartsState(darts,0,throwState,0,0,3,1,10,30));
    tap.sequence=5; tap.throwSerial=3; tap.stage=1;
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,200,dartInput)); // host owns this dart
    CHECK(SetDartsState(darts,0,throwState,1,0,4,1,20,30));
    tap.throwSerial=2; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,210,dartInput));
    tap.throwSerial=4;
    CHECK(AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,220,dartInput));
    CHECK(!ConsumeDartsInput(darts,throwState,dartInput,721)); // expired edge cannot reappear
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,730,dartInput));
    // A new human click may retry an expired edge on this same aiming stage.
    tap.sequence=6; CHECK(AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,731,dartInput));
    CHECK(ConsumeDartsInput(darts,throwState,dartInput,732));
    CHECK(SetDartsState(darts,0,throwState,1,1,22,1,50,60));
    CHECK(darts.turn==1 && darts.player==1);
    CHECK(!CanWatchView(darts,1,dartsView));
    tap.throwSerial=22; tap.match++; tap.sequence=100;
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,740,dartInput)); tap.match--;
    tap.receiver=23; CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,750,dartInput)); tap.receiver=22;
    tap.sequence=7; CHECK(AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,760,dartInput));
    CHECK(SetDartsState(darts,0,throwState,1,1,22,2,50,60));
    CHECK(!ConsumeDartsInput(darts,throwState,dartInput,770)); // state changed before dispatch
    CHECK(!CompleteDarts(darts,1,90,120)); CHECK(!CompleteDarts(darts,0,90,-1));
    CHECK(CompleteDarts(darts,0,90,120));
    CHECK(darts.phase==Results && darts.finished==3 && darts.scores[0]==90 && darts.scores[1]==120);
    CHECK(!SetDartsState(darts,0,throwState,0,2,41,1,999,999));
    CHECK(!CompleteDarts(darts,0,999,999));
    CHECK(!ConsumeDartsInput(darts,throwState,dartInput,780));
    CHECK(!AcceptDartsInput(darts,0,throwState,&tap,sizeof(tap),11,22,780,dartInput));
    CHECK(CanWatchView(darts,1,ViewStamp{darts.match,1,Darts,2}));
    CHECK(!CompleteDarts(mud,0,1,2));
    CHECK(Apply(darts,1,command(Leave,darts)));
    CHECK(!CanWatchView(darts,1,ViewStamp{darts.match,1,Darts,2}));
    // Independent native attempts run concurrently; the guest need not wait for
    // a host turn, and neither completed result may be rewritten by late input.
    CHECK(!PeerCanPlay(Mailman,Spectator|ExpandedGames|MudRealtime|DartsPvp));
    CHECK(PeerCanPlay(Mailman,AllMinigames)); CHECK(PeerCanPlay(Barntender,AllMinigames));
    CHECK(!PeerCanPlay(Darts,AllMinigames)); CHECK(!PeerCanPlay(LastGame+1,~0u));
    State duel; CHECK(Begin(duel,Mailman,1));
    CHECK(Apply(duel,0,command(Accept,duel)));
    CHECK(Apply(duel,0,command(Ready,duel))); CHECK(Apply(duel,1,command(Ready,duel)));
    CHECK(Apply(duel,1,command(Score,duel,70)));
    CHECK(Apply(duel,0,command(Score,duel,60)));
    CHECK(!Apply(duel,1,command(RoundDone,duel,80)));
    CHECK(Apply(duel,1,command(RoundDone,duel,80,true)));
    CHECK(duel.phase==Playing && duel.finished==2 && duel.turn==0);
    CHECK(!Apply(duel,1,command(Score,duel,999)));
    CHECK(!Apply(duel,1,command(RoundDone,duel,999,true)));
    CHECK(!CanWatchView(duel,1,ViewStamp{duel.match,0,6,1}));
    CHECK(!CanWatchView(duel,0,ViewStamp{duel.match,0,6,1}));
    CHECK(Apply(duel,0,command(RoundDone,duel,100,true)));
    CHECK(duel.phase==Results && duel.finished==3 && duel.scores[0]==100 && duel.scores[1]==80);
    CHECK(!Apply(duel,0,command(Score,duel,999)));
    CHECK(ResultWinner(duel)==0);
    duel.game=Bike; CHECK(ResultWinner(duel)==1);
    duel.game=JoyRide; duel.scores[1]=10000000; CHECK(ResultWinner(duel)==0);
    duel.scores[0]=10000000; CHECK(ResultWinner(duel)==-1);
    for (U32 game=Mailman;game<=Barntender;++game)
    {
        State attempt; CHECK(Begin(attempt,game,0));
        CHECK(Apply(attempt,1,command(Accept,attempt)));
        CHECK(Apply(attempt,0,command(Ready,attempt))); CHECK(Apply(attempt,1,command(Ready,attempt)));
        CHECK(Apply(attempt,1,command(RoundDone,attempt,45,true)));
        CHECK(Apply(attempt,0,command(RoundDone,attempt,30,true)));
        CHECK(attempt.phase==Results && attempt.finished==3 && attempt.turn==0);
    }
    State pool; CHECK(Begin(pool,Pool,0));
    CHECK(!PeerCanPlay(Pool,AllMinigames|Spectator|DartsPvp));
    CHECK(!PeerCanPlay(Pool,PoolPvp));
    CHECK(PeerCanPlay(Pool,AllMinigames|Spectator|PoolPvp));
    CHECK(Apply(pool,1,command(Accept,pool)));
    CHECK(Apply(pool,0,command(Ready,pool))); CHECK(Apply(pool,1,command(Ready,pool)));
    CHECK(!Apply(pool,0,command(Score,pool,999)));
    CHECK(!Apply(pool,1,command(RoundDone,pool,999,true)));
    PoolTurn poolTurn;
    CHECK(!SetPoolState(pool,1,poolTurn,1,2,2,3,2));
    CHECK(!SetPoolState(pool,0,poolTurn,2,2,2,3,2));
    CHECK(!SetPoolState(pool,0,poolTurn,1,0,2,3,2));
    CHECK(!SetPoolState(pool,0,poolTurn,1,2,4,3,2));
    CHECK(!SetPoolState(pool,0,poolTurn,1,2,2,-1,2));
    CHECK(SetPoolState(pool,0,poolTurn,1,2,2,3,2));
    CHECK(!SetPoolState(pool,0,poolTurn,1,2,2,3,2));
    CHECK(pool.player==1 && pool.turn==1 && pool.scores[0]==3 && pool.scores[1]==2);
    CHECK(CanWatchView(pool,1,ViewStamp{pool.match,1,Pool,1}));
    CHECK(!CanWatchView(pool,0,ViewStamp{pool.match,1,Pool,1}));
    PoolInputPacket cue; cue.sender=11; cue.receiver=22; cue.match=pool.match;
    cue.sequence=2; cue.shotSerial=2; cue.aimX=-0.5f; cue.aimY=1.0f;
    cue.shootPresses=1; cue.shootReleases=1; // quick down/up may arrive together
    PoolInputBuffer cueInput; PoolControls cueControls;
    CHECK(!AcceptPoolInput(pool,1,poolTurn,&cue,sizeof(cue),11,22,100,cueInput));
    CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue)-1,11,22,100,cueInput));
    CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue)+1,11,22,100,cueInput));
    CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),12,22,100,cueInput));
    CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,23,100,cueInput));
    cue.magic=0; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput)); cue.magic=POOL_INPUT_MAGIC;
    cue.match++; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput)); cue.match--;
    cue.sequence=0; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput)); cue.sequence=2;
    cue.shotSerial=1; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput)); cue.shotSerial=2;
    cue.aimX=1.01f; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput));
    cue.aimX=std::numeric_limits<float>::quiet_NaN(); CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput)); cue.aimX=-0.5f;
    cue.aimY=std::numeric_limits<float>::infinity(); CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput)); cue.aimY=1.0f;
    cue.shootPresses=257; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput)); cue.shootPresses=1;
    CHECK(AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,100,cueInput));
    CHECK(ConsumePoolInput(pool,poolTurn,cueInput,110,cueControls));
    CHECK(cueControls.aimX==-0.5f && cueControls.aimY==1.0f && cueControls.shootPressed && cueControls.shootReleased);
    CHECK(ConsumePoolInput(pool,poolTurn,cueInput,120,cueControls));
    CHECK(!cueControls.shootPressed && !cueControls.shootReleased);
    CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,140,cueInput));
    cue.sequence=1; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,150,cueInput));
    cue.sequence=3; cue.shootPresses=0; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,160,cueInput));
    cue.shootPresses=2; cue.shootReleases=2;
    // Press captured while AIM may be released after host advances to POWER;
    // unlike Darts, both edges belong to the same shot and must survive this.
    CHECK(SetPoolState(pool,0,poolTurn,1,2,3,3,2));
    CHECK(AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,200,cueInput));
    CHECK(ConsumePoolInput(pool,poolTurn,cueInput,210,cueControls) && cueControls.shootPressed && cueControls.shootReleased);
    CHECK(!ConsumePoolInput(pool,poolTurn,cueInput,701,cueControls));
    CHECK(cueControls.aimX==0 && cueControls.aimY==0 && !cueControls.shootPressed && !cueControls.shootReleased);
    cue.sequence=4;
    CHECK(AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,710,cueInput));
    CHECK(ConsumePoolInput(pool,poolTurn,cueInput,720,cueControls) && !cueControls.shootPressed && !cueControls.shootReleased);
    CHECK(SetPoolState(pool,0,poolTurn,1,3,2,3,3));
    cue.sequence=100; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,730,cueInput));
    cue.shotSerial=3; cue.match++; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,730,cueInput)); cue.match--;
    cue.receiver=23; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,730,cueInput)); cue.receiver=22;
    cue.sequence=5; cue.shootPresses=1; cue.shootReleases=0; // counters restart for a new shot
    CHECK(AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,740,cueInput));
    CHECK(!ConsumePoolInput(pool,poolTurn,cueInput,1241,cueControls));
    cue.sequence=6;
    CHECK(AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,1250,cueInput));
    CHECK(ConsumePoolInput(pool,poolTurn,cueInput,1260,cueControls) && !cueControls.shootPressed);
    CHECK(SetPoolState(pool,0,poolTurn,0,4,2,4,3));
    cue.sequence=7; cue.shotSerial=4;
    CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,1270,cueInput));
    CHECK(SetPoolState(pool,0,poolTurn,1,5,0,4,3));
    cue.shotSerial=5; CHECK(!AcceptPoolInput(pool,0,poolTurn,&cue,sizeof(cue),11,22,1280,cueInput));
    CHECK(!CompletePool(pool,1,0,7,2)); CHECK(!CompletePool(pool,0,2,7,2));
    CHECK(!CompletePool(pool,0,1,-1,2));
    CHECK(CompletePool(pool,0,1,7,2)); // illegal eight: more pots can still lose
    CHECK(pool.phase==Results && pool.finished==3 && ResultWinner(pool)==1 && pool.scores[0]==7 && pool.scores[1]==2);
    CHECK(!CompletePool(pool,0,0,999,999));
    CHECK(!SetPoolState(pool,0,poolTurn,0,6,2,999,999));
    CHECK(!ConsumePoolInput(pool,poolTurn,cueInput,1290,cueControls));
    CHECK(CanWatchView(pool,1,ViewStamp{pool.match,4,Pool,2}));
    CHECK(Apply(pool,1,command(Leave,pool)));
    CHECK(!CanWatchView(pool,1,ViewStamp{pool.match,4,Pool,2}));
    PoolInputPacket localCue;
    UpdatePoolInput(localCue,2,0.5f,-1.0f,true,false);
    UpdatePoolInput(localCue,2,0.5f,-1.0f,false,true);
    CHECK(localCue.shotSerial==2 && localCue.shootPresses==1 && localCue.shootReleases==1);
    // An unacknowledged tap from the old shot must not ride the next shot's
    // otherwise neutral heartbeat, even when packets were dropped at handoff.
    UpdatePoolInput(localCue,3,0.0f,0.0f,false,false);
    CHECK(localCue.shotSerial==3 && localCue.shootPresses==0 && localCue.shootReleases==0);
    UpdatePoolInput(localCue,3,1.0f,0.0f,true,false);
    CHECK(localCue.shootPresses==1 && localCue.shootReleases==0 && localCue.aimX==1.0f);
    // A view outage can suspend input in the middle of POWER, without changing
    // the native shot. A new epoch must cancel charge before accepting input.
    State resumed; CHECK(Begin(resumed,Pool,0));
    CHECK(Apply(resumed,1,command(Accept,resumed)));
    CHECK(Apply(resumed,0,command(Ready,resumed))); CHECK(Apply(resumed,1,command(Ready,resumed)));
    PoolTurn resumedTurn; CHECK(SetPoolState(resumed,0,resumedTurn,1,2,3,0,0));
    PoolInputBuffer resumedInput;
    PoolInputPacket oldCue; oldCue.sender=11; oldCue.receiver=22; oldCue.match=resumed.match;
    oldCue.sequence=10; oldCue.shotSerial=2; oldCue.shootPresses=1;
    CHECK(AcceptPoolInput(resumed,0,resumedTurn,&oldCue,sizeof(oldCue),11,22,100,resumedInput));
    CHECK(ConsumePoolInput(resumed,resumedTurn,resumedInput,110,cueControls) && cueControls.shootPressed);
    PoolInputPacket resumeCue=oldCue;
    resumeCue.sequence=11; resumeCue.inputEpoch=2; resumeCue.shootPresses=0;
    CHECK(AcceptPoolInput(resumed,0,resumedTurn,&resumeCue,sizeof(resumeCue),11,22,120,resumedInput));
    CHECK(!ConsumePoolInput(resumed,resumedTurn,resumedInput,121,cueControls)); // explicit charge cancellation
    CHECK(!cueControls.shootPressed && !cueControls.shootReleased && cueControls.aimX==0);
    CHECK(ConsumePoolInput(resumed,resumedTurn,resumedInput,122,cueControls) && !cueControls.shootPressed);
    oldCue.sequence=100; oldCue.shootReleases=1;
    CHECK(!AcceptPoolInput(resumed,0,resumedTurn,&oldCue,sizeof(oldCue),11,22,130,resumedInput));
    resumeCue.sequence=12; resumeCue.inputEpoch=0;
    CHECK(!AcceptPoolInput(resumed,0,resumedTurn,&resumeCue,sizeof(resumeCue),11,22,130,resumedInput));
    resumeCue.inputEpoch=2; resumeCue.shootReleases=1;
    CHECK(AcceptPoolInput(resumed,0,resumedTurn,&resumeCue,sizeof(resumeCue),11,22,140,resumedInput));
    CHECK(ConsumePoolInput(resumed,resumedTurn,resumedInput,141,cueControls) && !cueControls.shootPressed && cueControls.shootReleased);
    // Even when the neutral resume packet was lost, the next genuine press is
    // retained after one cancellation frame, never conflated with the old press.
    resumeCue.inputEpoch=3; resumeCue.sequence=13; resumeCue.shootPresses=1; resumeCue.shootReleases=0;
    CHECK(AcceptPoolInput(resumed,0,resumedTurn,&resumeCue,sizeof(resumeCue),11,22,150,resumedInput));
    CHECK(!ConsumePoolInput(resumed,resumedTurn,resumedInput,151,cueControls));
    CHECK(ConsumePoolInput(resumed,resumedTurn,resumedInput,152,cueControls) && cueControls.shootPressed && !cueControls.shootReleased);
    CHECK(ConsumePoolInput(resumed,resumedTurn,resumedInput,153,cueControls) && !cueControls.shootPressed);
    std::puts("PASS: binding/capabilities, ready/legacy turns, 8 score/time duels, Mud/Darts/Pool authority, input loss/reorder/expiry, stage/shot ownership, results/farm");
    return 0;
}
