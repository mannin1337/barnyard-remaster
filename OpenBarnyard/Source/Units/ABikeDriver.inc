// QA only: feed the normal human bike input vector; never seek/teleport/set progress.
// Included after TOSHI_NAMESPACE_USING by ABikeControlComponent.cpp.
#if defined( BARNYARD_COMMUNITY_PATCH ) && !defined( TOSHI_FINAL )
namespace DevBikeDriver
{
static void Update( ABikeControlComponent* control, AUnit* owner, ABikeSteer* steer, TVector4& input, TFLOAT dt )
{
    static const TBOOL enabled = []() {
        const TCHAR* cmd = AGameLoader::Dev_GetCommandLine();
        const TCHAR* flag = TStringManager::String8FindString( cmd, "-devbikedriver" );
        return flag && ( flag == cmd || flag[-1] == ' ' ) &&
            ( flag[14] == '\0' || flag[14] == ' ' || flag[14] == '"' );
    }();
    if ( !enabled ) return;
    AGameState* state = AGameStateController::GetSingleton()->GetCurrentState();
    if ( !state || !state->IsA( &TGetClass( ABikeRaceMicroGame ) ) ) return;
    ABikeRaceMicroGame* game = TSTATICCAST( ABikeRaceMicroGame, state );
    const ABikeRaceMicroGame::Biker& biker = game->GetBiker( 0 );
    AUnitProxy* proxy = biker.pSlot ? biker.pSlot->m_pUnitProxy.Get() : TNULL;
    if ( !proxy || proxy->GetUnit() != owner || !( biker.pSlot->m_uiFlags & APlayerSlot::FLAGS_HUMAN ) ) return;
    input = TVector4::VEC_ZERO;
    if ( game->GetElapsedTime() <= 0.0f || game->IsGameOver() ||
         ARootTask::GetSingleton()->IsPaused() || !ACoopMinigame::CanUpdate( game ) ) return;
    const TINT count = game->GetNumWaypoints();
    // LoadWaypoints appends point[count] == point[0]; the native closing leg may use count.
    if ( count < 2 || biker.iWaypoint < 0 || biker.iWaypoint > count ) return;
    struct Driver { const void* owner = TNULL; TFLOAT lastTime = -1, stuck = 0, reverse = 0, jumpWait = 0, log = 0; TVector4 lastPos; TINT lap = -1; };
    static Driver driver;
    const TVector4 position = *steer->GetPosition();
    if ( driver.owner != owner || game->GetElapsedTime() < driver.lastTime )
    {
        driver = Driver(); driver.owner = owner; driver.lastPos = position;
        TINFO( "[devbikedriver] human input enabled; native physics/progress, waypoints=%d\n", count );
    }
    driver.lastTime = game->GetElapsedTime();
    const TVector4* points = game->GetWaypoints();
    const TINT current = biker.iWaypoint % count, previous = ( current + count - 1 ) % count;
    TVector4 target = points[current];
    const TFLOAT speed = TMath::Abs( steer->GetSpeed() );
    const TFLOAT lookAhead = TMath::Min( 3.0f, 1.5f + speed * 0.2f );
    TFLOAT dx = points[current].x - points[previous].x, dz = points[current].z - points[previous].z;
    const TFLOAT length = std::sqrt( dx * dx + dz * dz );
    if ( length > 0.001f )
    {
        TFLOAT along = ( ( position.x - points[previous].x ) * dx + ( position.z - points[previous].z ) * dz ) / length;
        along = TMath::Max( 0.0f, along ) + lookAhead;
        if ( along < length )
        { target.x = points[previous].x + dx * along / length; target.z = points[previous].z + dz * along / length; }
        else
        {
            const TINT next = ( current + 1 ) % count;
            dx = points[next].x - points[current].x; dz = points[next].z - points[current].z;
            const TFLOAT nextLength = std::sqrt( dx * dx + dz * dz );
            const TFLOAT fraction = nextLength > 0.001f ? TMath::Min( ( along - length ) / nextLength, 1.0f ) : 0.0f;
            target.x += dx * fraction; target.z += dz * fraction;
        }
    }
    dx = target.x - position.x; dz = target.z - position.z;
    const TVector4 direction = *steer->GetDirection();
    const TFLOAT angle = std::atan2( direction.z * dx - direction.x * dz, direction.x * dx + direction.z * dz );
    input.x = angle * 1.5f; TMath::Clip( input.x, -1.0f, 1.0f );
    const TFLOAT targetSpeed = TMath::Abs( angle ) > 0.6f ? 4.0f : 7.0f;
    input.z = speed > targetSpeed + 1.5f ? -1.0f : ( speed < targetSpeed ? 1.0f : 0.0f );
    const TFLOAT movedX = position.x - driver.lastPos.x, movedZ = position.z - driver.lastPos.z;
    if ( movedX * movedX + movedZ * movedZ > 0.25f ) { driver.lastPos = position; driver.stuck = 0; }
    else driver.stuck += dt;
    APhysicsBike* physics = steer->GetPhysics();
    driver.jumpWait -= dt;
    if ( driver.stuck > 2.0f && driver.jumpWait <= 0.0f && !steer->IsStopped() &&
         !physics->IsDriverless() && physics->m_iNumWheelsOnGround > 0 )
    {
        auto* keyboard = AInputHandler::GetSingleton()->GetKeyboardDevice();
        if ( keyboard )
        {
            TInputInterface::InputEvent down( keyboard, TInputDeviceKeyboard::KEY_SPACE, TInputInterface::EVENT_TYPE_GONE_DOWN );
            TInputInterface::InputEvent up( keyboard, TInputDeviceKeyboard::KEY_SPACE, TInputInterface::EVENT_TYPE_GONE_UP );
            control->ProcessCommand( AInputCommand_Jump, &down );
            control->ProcessCommand( AInputCommand_Jump, &up );
            keyboard->StopRepeat( TInputDeviceKeyboard::KEY_SPACE );
            driver.jumpWait = 8.0f;
            TINFO( "[devbikedriver] native jump input t=%.2f wheels=%d frozen=%d\n", game->GetElapsedTime(), physics->m_iNumWheelsOnGround, physics->m_bFrozen );
        }
    }
    if ( driver.stuck > 3.0f && driver.reverse <= 0.0f ) { driver.reverse = 1.2f; driver.stuck = 0; }
    if ( driver.reverse > 0.0f ) { driver.reverse -= dt; input.z = -1.0f; input.x = -input.x; }
    driver.log -= dt;
    if ( driver.log <= 0.0f || driver.lap != biker.iLap )
    {
        TINFO( "[devbikedriver] t=%.2f lap=%d segment=%d waypoint=%d pos=(%.2f,%.2f) speed=%.2f angle=%.2f input=(%.2f,%.2f) stopped=%d frozen=%d enabled=%d contacts=%d wheels=%d gear=%d riderless=%d\n",
            game->GetElapsedTime(), biker.iLap, biker.iSegment, biker.iWaypoint, position.x, position.z, speed, angle, input.x, input.z,
            steer->IsStopped(), physics->m_bFrozen, physics->GetBody().m_bEnabled, physics->m_iNumContacts, physics->m_iNumWheelsOnGround, physics->m_oGearBox.iGear, physics->IsDriverless() );
        driver.log = 5.0f; driver.lap = biker.iLap;
    }
}
}
#endif
