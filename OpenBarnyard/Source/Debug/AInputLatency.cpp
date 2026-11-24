#include "pch.h"
#include "Debug/AInputLatency.h"

#if defined( BARNYARD_COMMUNITY_PATCH ) && defined( TOSHI_SKU_WINDOWS ) && !defined( TOSHI_FINAL )
#include "Player/ACoopMinigame.h"
#include <Input/TInputDeviceKeyboard.h>
#include <Input/TInputDeviceMouse.h>
#include <Toshi/TScheduler.h>
#include <chrono>
#include <cstring>

TOSHI_NAMESPACE_USING

namespace AInputLatency
{
namespace
{
using Microseconds = unsigned long long;
struct Sample
{
	unsigned id, inputFrame, actionFrame;
	Microseconds inputTime, actionTime;
	const char *source, *edge, *game, *effect;
	int doodad;
	bool inputLogged, submitted;
};
Sample samples[ 128 ]{};
unsigned current, sequence;
bool pendingLogs;

bool Enabled()
{
	static const bool enabled = [] {
		const char* cmd = AGameLoader::Dev_GetCommandLine();
		constexpr char flag[] = "-devinputlatency";
		for ( const char* p = cmd; ( p = std::strstr( p, flag ) ) != nullptr; ++p )
		{
			const char end = p[ sizeof( flag ) - 1 ];
			if ( ( p == cmd || p[ -1 ] == ' ' || p[ -1 ] == '"' ) && ( !end || end == ' ' || end == '"' ) )
				return true;
		}
		return false;
	}();
	return enabled;
}

unsigned Frame() { return g_oSystemManager.GetScheduler()->GetFrameCount(); }
Microseconds Now()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(
	    std::chrono::steady_clock::now().time_since_epoch() ).count();
}

unsigned Start( const char* source, const char* edge, int doodad )
{
	if ( ++sequence == 0 ) ++sequence;
	Sample& entry = samples[ sequence % TARRAYSIZE( samples ) ];
	entry = {};
	entry.id = sequence;
	entry.inputFrame = Frame();
	entry.inputTime = Now();
	entry.source = source;
	entry.edge = edge;
	entry.doodad = doodad;
	pendingLogs = true;
	return sequence;
}

// A held stick emits a virtual event every frame. Sample the start of each
// movement, including a fresh movement after at least one idle frame.
bool MotionStarted( const TInputInterface::InputEvent& event )
{
	if ( event.GetAxisCount() < 1 ) return false;
	const float x = event.GetMagnitudeFloat( 0 );
	const float y = event.GetAxisCount() > 1 ? event.GetMagnitudeFloat( 1 ) : 0.0f;
	if ( x == 0.0f && y == 0.0f ) return false;
	struct Axis { TInputDevice* device; int doodad; unsigned frame; };
	static Axis axes[ 32 ]{};
	for ( Axis& axis : axes )
	{
		if ( axis.device && ( axis.device != event.GetSource() || axis.doodad != event.GetDoodad() ) ) continue;
		const unsigned frame = Frame();
		const bool started = !axis.device || frame - axis.frame > 1;
		axis = { event.GetSource(), event.GetDoodad(), frame };
		return started;
	}
	return false;
}
}

Dispatch::Dispatch( const TInputInterface::InputEvent& event ) : previous( current )
{
	current = 0;
	if ( !Enabled() || !ACoopMinigame::IsPlaying() ) return;
	const auto type = event.GetEventType();
	const char* edge = nullptr;
	if ( type == TInputInterface::EVENT_TYPE_GONE_DOWN ) edge = "down";
	else if ( type == TInputInterface::EVENT_TYPE_GONE_UP ) edge = "up";
	else if ( ( type == TInputInterface::EVENT_TYPE_VIRTUAL || type == TInputInterface::EVENT_TYPE_MOVED ) && MotionStarted( event ) )
		edge = "motion";
	if ( !edge ) return;
	auto* device = event.GetSource();
	const char* source = device->IsA( &TGetClass( TInputDeviceKeyboard ) ) ? "keyboard" :
	    device->IsA( &TGetClass( TInputDeviceMouse ) ) ? "mouse" : "controller";
	current = Start( source, edge, event.GetDoodad() );
}

Dispatch::~Dispatch() { current = previous; }
unsigned Current() { return current; }
unsigned Scripted( const char* game )
{
	return Enabled() && ACoopMinigame::IsPlaying() ? Start( "scripted", game, 0 ) : 0;
}

void Action( unsigned id, const char* game, const char* effect )
{
	if ( !id ) return;
	Sample& entry = samples[ id % TARRAYSIZE( samples ) ];
	if ( entry.id != id || entry.effect ) return;
	entry.actionTime = Now();
	entry.actionFrame = Frame();
	entry.game = game;
	entry.effect = effect;
	pendingLogs = true;
}

Microseconds BeforeSubmit() { return pendingLogs ? Now() : 0; }
void AfterSubmit( Microseconds stamp )
{
	if ( !stamp ) return;
	const unsigned frame = Frame();
	pendingLogs = false;
	for ( Sample& entry : samples )
	{
		if ( !entry.id || entry.submitted ) continue;
		if ( !entry.inputLogged )
		{
			TINFO( "[input-latency] stage=dispatch id=%u frame=%u us=%llu source=%s doodad=%d edge=%s\n",
			    entry.id, entry.inputFrame, entry.inputTime, entry.source, entry.doodad, entry.edge );
			entry.inputLogged = true;
		}
		if ( !entry.effect ) continue;
		if ( entry.actionTime > stamp )
		{
			// EndScene listeners can act after this scene's submission point.
			pendingLogs = true;
			continue;
		}
		TINFO( "[input-latency] stage=action id=%u frame=%u us=%llu game=%s effect=%s\n",
		    entry.id, entry.actionFrame, entry.actionTime, entry.game, entry.effect );
		TINFO( "[input-latency] stage=submit id=%u frame=%u us=%llu game=%s effect=%s\n",
		    entry.id, frame, stamp, entry.game, entry.effect );
		entry.submitted = true;
	}
}
}
#endif
