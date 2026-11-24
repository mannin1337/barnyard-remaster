#include "Player/ACoopMinigameProtocol.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using TUINT32 = std::uint32_t;
using TINT32 = std::int32_t;
using TINT16 = std::int16_t;
using TFLOAT = float;
#include "MiniGame/darts_spectator.inc"
using namespace CoopMini;
int checks = 0;
void Check(bool condition, const char* name) { ++checks; if(!condition) { std::printf("FAIL %s\n",name); std::exit(1); } }
void FirstClickMatchesLastRender(int networkHz)
{
    DartsViewMotion motion;
    DartsAimHistory guest, host;
    DartsViewFrame packet{};
    packet.currentPlayer=1; packet.throwSerial=8; packet.state=packet.aimStage=1;
    State game; game.game=Darts; game.phase=Playing; game.match=5; game.player=1;
    DartsTurn turn; turn.player=1; turn.serial=8; turn.stage=1;
    DartsInputPacket press; press.sender=17; press.receiver=19; press.match=5;
    press.throwSerial=8; press.stage=1;
    U32 hostSeq=0, lastReceived=0;
    float worst=0, last=0;
    int movingBetweenPackets=0;
    for (int i=0; i<120; ++i)
    {
        const U32 now=1000+i*1000/120;
        const bool newView=i%(120/networkHz)==0 && i!=60;
        if (i%(120/networkHz)==0)
        {
            ++hostSeq;
            auto next=packet;
            next.aimY=std::cos(float(i)*0.12f);
            host.Record(next,hostSeq,5,now-150);
            if (newView)
            {
                lastReceived=hostSeq;
                packet=next;
                guest.Record(packet,lastReceived,5,now);
            }
        }
        motion.Update(packet,lastReceived,5,1.0f/120.0f,true);
        const float beforeClick=motion.shown.aimY;
        if (!newView && i>5 && beforeClick!=last) ++movingBetweenPackets;
        last=beforeClick;
        float guestLock=0, hostLock=0;
        Check(guest.ResolveClick(motion.sequence,motion.previousFirstView,motion.firstBlend,5,8,1,now,guestLock) &&
              guestLock==beforeClick,"local lock keeps the rendered bar coordinate");
        ++press.sequence;
        const DartsBlendedInputPacket blendPacket{press,motion.sequence,motion.previousFirstView,motion.firstBlend};
        const DartsViewInputPacket oneSample{press,motion.sequence};
        DartsInputBuffer input;
        const bool accepted=motion.previousFirstView ?
            AcceptDartsInput(game,0,turn,&blendPacket,sizeof(blendPacket),17,19,now+150,input,true,true,true) :
            AcceptDartsInput(game,0,turn,&oneSample,sizeof(oneSample),17,19,now+150,input,true);
        U32 view=0, previous=0, fraction=0;
        Check(accepted && ConsumeDartsInput(game,turn,input,now+150,&view,nullptr,&previous,&fraction) &&
              host.ResolveClick(view,previous,fraction,5,8,1,now+150,hostLock),
              "host resolves both real samples after two 150 ms network legs");
        const float error=std::abs(hostLock-beforeClick);
        if (error>worst) worst=error;
    }
    std::printf("first axis %d Hz views / 120 Hz render: max pre-click axis error %.9f, moving inter-packet frames %d\n",
                networkHz,worst,movingBetweenPackets);
    Check(worst==0 && movingBetweenPackets>40,"all first-axis clicks preserve the last rendered sub-frame");
    float untouched=9;
    Check(!host.ResolveClick(hostSeq,hostSeq,32000,5,8,1,2000,untouched) &&
          !host.ResolveClick(hostSeq-1,hostSeq,32000,5,8,1,2000,untouched) &&
          !host.ResolveClick(hostSeq,hostSeq-1,65536,5,8,1,2000,untouched) &&
          !host.ResolveClick(hostSeq,hostSeq-1,30000,5,8,2,2000,untouched) &&
          !host.ResolveClick(hostSeq,hostSeq-1,30000,6,8,1,2000,untouched) &&
          !host.ResolveClick(hostSeq,hostSeq-1,30000,5,10,1,2000,untouched) &&
          !host.ResolveClick(hostSeq,hostSeq-1,30000,5,8,1,3500,untouched) &&
          !host.ResolveClick(hostSeq,hostSeq-networkHz/3,30000,5,8,1,2000,untouched) &&
          untouched==9,"invalid bracket, phase, match, dart or age cannot move host aim");
}
void LocalThrow()
{
    DartsViewFrame host{};
    host.currentPlayer=1; host.throwSerial=8; host.aimStage=host.state=1; host.aimY=-0.25f;
    host.presentTargets=15; host.players[1].score=10;
    DartsLocalAim local;
    local.Begin(5,8,77,host.aimY,1000);
    auto visual=host;
    local.Present(visual);
    Check(visual.state==2 && visual.aimY==-0.25f && visual.aimX==1,"first press reveals second bar immediately");
    local.Update(host,5,true,1050,0.050f,2.0f);
    Check(!local.Fire(),"duplicate press inside native 100ms guard ignored");
    local.Update(host,5,true,1150,0.100f,2.0f);
    visual=host; local.Present(visual);
    Check(visual.aimStage==2 && std::abs(visual.aimX-0.58778525f)<0.00001f,"second sweep advances before any host reply");
    const float displayed=visual.aimX;
    Check(local.Fire(),"150ms second press accepted before first reply");
    Check(!local.Fire() && !local.TakeSecond(host,1150),"duplicate suppressed and first press still retransmittable");
    visual=host; local.Present(visual);
    Check(visual.players[1].animation==2 && visual.state==3 && !visual.aimStage &&
          visual.aimX==displayed,"second press starts hand throw and ends aiming immediately");
    Check(visual.players[1].score==10 && visual.presentTargets==15 && host.state==1,"only host owns hits and scores");
    host.state=2;
    host.aimStage=0;
    local.Update(host,5,true,1350,0.20f,2.0f);
    Check(!local.TakeSecond(host,1350),"first reply can precede host second-stage gate");
    host.aimStage=2;
    Check(local.TakeSecond(host,1450) && !local.TakeSecond(host,1451),"eligible host view sends buffered second click once");
    float resolved=-99.0f;
    const DartsFirstLock accepted{5,8,77};
    Check(accepted.ResolveSecond(5,8,77,local.phaseMs,0.20f,2.0f,resolved) && resolved==displayed,"host reconstructs exact displayed coordinate");
    Check(!accepted.ResolveSecond(6,8,77,150,0.20f,2.0f,resolved) &&
          !accepted.ResolveSecond(5,10,77,150,0.20f,2.0f,resolved) &&
          !accepted.ResolveSecond(5,8,78,150,0.20f,2.0f,resolved),"other match, throw and first lock rejected");
    Check(!accepted.ResolveSecond(5,8,77,99,0.2f,2.0f,resolved) &&
          !accepted.ResolveSecond(5,8,77,60001,60.1f,2.0f,resolved) &&
          !accepted.ResolveSecond(5,8,77,2200,0.2f,2.0f,resolved),"host rejects impossible phase times");
    local.Update(host,5,true,1460,0.016f,2.0f);
    Check(local.fired && local.phaseMs==150,"late aiming packet cannot restart a throw");
    host.state=9; host.aimStage=0;
    local.Update(host,5,true,2200,0.016f,2.0f);
    Check(local.fired,"host throwing confirmation retains local arm clip");
    host.state=11;
    local.Update(host,5,true,2300,0.016f,2.0f);
    Check(!local.first.dart,"host flight takes over");
    local.FireSnapshot(5,8,displayed,-0.25f,2310);
    local.released=true; // Real game receives hd_throw_bp from the guest's throw clip.
    host.state=2;
    local.Update(host,5,true,2360,0.05f,2.0f);
    visual=host; local.Present(visual);
    Check(visual.state==11 && local.flight==0.5f,"released dart moves halfway before any flight reply");
    host.state=11;
    local.Update(host,5,true,2420,0.06f,2.0f);
    Check(local.released && local.flight==1,"delayed host flight cannot rewind the predicted dart");
    host.state=12;
    local.Update(host,5,true,2500,0.01f,2.0f);
    Check(!local.first.dart,"host landing replaces speculative board-plane pose");
    host.state=2;
    local.FireSnapshot(5,8,-0.1f,0.2f,3000);
    local.Update(host,5,true,3900,0.016f,2.0f);
    Check(!local.first.dart,"unconfirmed second press reopens host aiming after timeout");
    host.state=1;
    local.Begin(5,8,77,0.2f,4000);
    local.Update(host,5,true,4801,0.016f,2.0f);
    Check(!local.first.dart,"lost first click expires without leaving a local throw");
    local.Begin(5,8,77,-0.3f,4900);
    host.state=2;
    local.Update(host,5,true,4930,0.03f,2.0f);
    Check(!local.first.dart,"automatic host first lock supersedes an unaccepted prediction");
    Check(DartsPhysicalPress(true,false,500)==DartsPressMode::Wait &&
          DartsPhysicalPress(true,false,801)==DartsPressMode::HostTime,"legacy retry gets one host-time fallback");
    for (int reason=0; reason<4; ++reason)
    {
        host.throwSerial=reason==1 ? 10 : 8; host.finished=reason==2;
        local.Begin(5,8,77,0.2f,5000);
        local.Update(host,reason==0 ? 6 : 5,reason!=3,5016,0.016f,2.0f);
        Check(!local.first.dart,"match, dart, result and ownership changes clear prediction");
    }
    std::printf("darts first/second press: 0 host updates needed to draw; second accepted at 150 ms before ACK, host axis error %.8f\n",
                std::abs(resolved-displayed));
}
int main()
{
    State s; s.game=Darts; s.phase=Playing; s.match=5; s.player=1;
    DartsTurn turn; turn.player=1; turn.serial=8; turn.stage=1;
    // The old wire format remains exactly 28 bytes.
    DartsInputPacket legacy; legacy.sender=17; legacy.receiver=19; legacy.match=5;
    legacy.sequence=1; legacy.throwSerial=8; legacy.stage=1;
    DartsInputBuffer buffer;
    Check(sizeof(legacy)==28,"legacy layout");
    Check(AcceptDartsInput(s,0,turn,&legacy,sizeof(legacy),17,19,100,buffer),"legacy accepted");
    Check(ConsumeDartsInput(s,turn,buffer,110),"legacy consumed");
    DartsViewInputPacket packet{legacy,77};
    buffer={};
    Check(!AcceptDartsInput(s,0,turn,&packet,sizeof(packet),17,19,100,buffer),"unnegotiated extension rejected");
    Check(AcceptDartsInput(s,0,turn,&packet,sizeof(packet),17,19,100,buffer,true),"snapshot-bound click accepted");
    U32 view=0;
    Check(ConsumeDartsInput(s,turn,buffer,110,&view) && view==77,"view sequence delivered");
    Check(!ConsumeDartsInput(s,turn,buffer,120,&view) && !view,"single consumption");
    Check(DiscardDartsViewInput(buffer,8,1),"invalid history can retry");
    Check(!AcceptDartsInput(s,0,turn,&packet,sizeof(packet),17,19,120,buffer,true),"discard cannot replay same click");
    Check(!ConsumeDartsInput(s,turn,buffer,120,&view),"discard cannot resurrect consumed edge");
    packet.input.sequence=2; packet.viewSequence=78;
    Check(AcceptDartsInput(s,0,turn,&packet,sizeof(packet),17,19,130,buffer,true),"fresh click may retry");
    Check(ConsumeDartsInput(s,turn,buffer,140,&view) && view==78,"retry delivers newer view");
    packet.input.sequence=3;
    Check(!AcceptDartsInput(s,0,turn,&packet,sizeof(packet),17,19,150,buffer,true),"already committed stage cannot double fire");
    buffer={}; packet.viewSequence=0;
    Check(!AcceptDartsInput(s,0,turn,&packet,sizeof(packet),17,19,150,buffer,true),"zero view invalid");
    packet.viewSequence=79; packet.input.match=6;
    Check(!AcceptDartsInput(s,0,turn,&packet,sizeof(packet),17,19,150,buffer,true),"wrong match invalid");
    packet.input.match=5;
    Check(!AcceptDartsInput(s,0,turn,&packet,sizeof(packet),18,19,150,buffer,true),"wrong peer invalid");
    Check(AcceptDartsInput(s,0,turn,&packet,sizeof(packet),17,19,150,buffer,true),"valid before expiry");
    Check(!ConsumeDartsInput(s,turn,buffer,700,&view) && !view,"expired click cannot compensate");
    Check(!ConsumeDartsInput(s,turn,buffer,701,&view),"expired click stays spent");
    buffer={}; legacy.sequence=4;
    Check(AcceptDartsInput(s,0,turn,&legacy,sizeof(legacy),17,19,800,buffer,true),"negotiated peer may send legacy");
    Check(ConsumeDartsInput(s,turn,buffer,801,&view) && !view,"legacy uses no view reference");
    Check(!DiscardDartsViewInput(buffer,8,1),"legacy consumed click cannot rearm");
    LocalThrow();
    FirstClickMatchesLastRender(30);
    FirstClickMatchesLastRender(60);
    std::printf("PASS %d checks\n",checks);
}
