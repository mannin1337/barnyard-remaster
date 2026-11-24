#pragma once

#include <Input/TInputInterface.h>

// The ID follows one local input through a native state change to the first
// scene submission containing it. IDs are local to this process.
namespace AInputLatency
{
#if defined( BARNYARD_COMMUNITY_PATCH ) && defined( TOSHI_SKU_WINDOWS ) && !defined( TOSHI_FINAL )
unsigned Current();
unsigned Scripted( const char* game );
void Action( unsigned id, const char* game, const char* effect );
unsigned long long BeforeSubmit();
void AfterSubmit( unsigned long long stamp );

class Dispatch
{
public:
	explicit Dispatch( const Toshi::TInputInterface::InputEvent& event );
	~Dispatch();
private:
	unsigned previous;
};
#else
inline unsigned Current() { return 0; }
inline unsigned Scripted( const char* ) { return 0; }
inline void Action( unsigned, const char*, const char* ) {}
inline unsigned long long BeforeSubmit() { return 0; }
inline void AfterSubmit( unsigned long long ) {}
struct Dispatch { explicit Dispatch( const Toshi::TInputInterface::InputEvent& ) {} };
#endif
}
