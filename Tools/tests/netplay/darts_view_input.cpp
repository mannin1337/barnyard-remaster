#include "Player/ACoopMinigameProtocol.h"
#include <cstdio>
#include <cstdlib>
using namespace CoopMini;
int checks = 0;
void Check(bool condition, const char* name) { ++checks; if(!condition) { std::printf("FAIL %s\n",name); std::exit(1); } }
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
    std::printf("PASS %d checks\n",checks);
}
