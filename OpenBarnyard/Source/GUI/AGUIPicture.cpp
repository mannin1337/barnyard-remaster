#include "pch.h"
#include "AGUIPicture.h"
#include "AGUI2.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_INIT_NORUNTIME( AGUIPicture );

// $Barnyard: FUNCTION 0059d530
AGUIPicture::AGUIPicture()
{
	m_iUnknown1  = 0;
	m_iNumRects1 = 0;
	m_iNumRects2 = 0;
	m_bFlag      = TFALSE;
	m_bLoaded    = TFALSE;
	m_bIsVisible = TTRUE;
	m_fUnknown3  = -10.0f;
}

// NOTE: retail fills split_rules in a dynamic initializer (thunk 0059d520)
// $Barnyard: FUNCTION 0059d400
void AGUIPicture::InitialiseClass()
{
	constexpr TFLOAT TEX_SIZE = 256.0f;

	TFLOAT fValA;
	TFLOAT fValB = -240.0f;

	for ( TINT i = 0; i < 2; i++ )
	{
		fValA = ( i == 0 ) ? TEX_SIZE - 1.0f : 240.0f - fValB;

		split_rules[ i * 3 + 0 ].iImageIndex = i * 3 + 0;
		split_rules[ i * 3 + 0 ].Pos1        = { -320.0f, -65.0f };
		split_rules[ i * 3 + 0 ].Pos2        = { fValB, fValA + fValB };
		split_rules[ i * 3 + 0 ].UV1         = { 0.0f, 0.9960938f };
		split_rules[ i * 3 + 0 ].UV2         = { 0.0f, fValA / TEX_SIZE };
		split_rules[ i * 3 + 0 ].iUnk1       = 0;
		split_rules[ i * 3 + 0 ].iUnk2       = 0;

		split_rules[ i * 3 + 1 ].iImageIndex = i * 3 + 1;
		split_rules[ i * 3 + 1 ].Pos1        = { -65.0f, 190.0f };
		split_rules[ i * 3 + 1 ].Pos2        = { fValB, fValA + fValB };
		split_rules[ i * 3 + 1 ].UV1         = { 0.0f, 0.9960938f };
		split_rules[ i * 3 + 1 ].UV2         = { 0.0f, fValA / TEX_SIZE };
		split_rules[ i * 3 + 1 ].iUnk1       = 0;
		split_rules[ i * 3 + 1 ].iUnk2       = 0;

		split_rules[ i * 3 + 2 ].iImageIndex = i * 3 + 2;
		split_rules[ i * 3 + 2 ].Pos1        = { 190.0f, 320.0f };
		split_rules[ i * 3 + 2 ].Pos2        = { fValB, fValA + fValB };
		split_rules[ i * 3 + 2 ].UV1         = { 0.0f, 1.015625f };
		split_rules[ i * 3 + 2 ].UV2         = { 0.0f, fValA / TEX_SIZE };
		split_rules[ i * 3 + 2 ].iUnk1       = 0;
		split_rules[ i * 3 + 2 ].iUnk2       = 0;

		fValB += fValA;
	}

#ifdef BARNYARD_LATEST
	InitialiseWiiSplitRules();
#endif
}

#ifdef BARNYARD_LATEST

// NOTE: this is the dynamic initializer of split_rules in the Wii build
// $BarnyardWiiJP: FUNCTION 8031f830
void AGUIPicture::InitialiseWiiSplitRules()
{
	// Width of the textures of the three columns, all are 256 pixels high
	constexpr TFLOAT aTexWidths[ 3 ] = { 256.0f, 256.0f, 128.0f };
	constexpr TFLOAT TEX_HEIGHT      = 256.0f;

	TFLOAT fY = -224.0f;

	for ( TINT i = 0; i < 2; i++ )
	{
		const TFLOAT fHeight = ( i == 0 ) ? TEX_HEIGHT - 1.0f : 224.0f - fY;
		TFLOAT       fX      = -320.0f;

		for ( TINT k = 0; k < 3; k++ )
		{
			const TFLOAT fWidth = ( k < 2 ) ? aTexWidths[ k ] - 1.0f : 320.0f - fX;
			SplitRule&   rRule  = ms_aWiiSplitRules[ i * 3 + k ];

			rRule.Pos1        = { fX, fX + fWidth };
			rRule.Pos2        = { fY, fY + fHeight };
			rRule.UV1         = { 0.0f, fWidth / aTexWidths[ k ] };
			rRule.UV2         = { 0.0f, fHeight / TEX_HEIGHT };
			rRule.iImageIndex = i * 3 + k;
			rRule.iUnk1       = 0;
			rRule.iUnk2       = 0;

			fX += fWidth;
		}

		fY += fHeight;
	}
}

#endif // BARNYARD_LATEST

// NOTE: retail resets m_iUnknown1 after m_Name is destroyed
// $Barnyard: FUNCTION 0059d5e0
// $Barnyard: FUNCTION 0059d570
AGUIPicture::~AGUIPicture()
{
	m_iUnknown1 = -1;
}

void AGUIPicture::DeinitialiseClass()
{
}

// $Barnyard: FUNCTION 0059d2b0
void AGUIPicture::Delete()
{
	Flush();
	delete this;
}

// $Barnyard: FUNCTION 0059d2a0
void AGUIPicture::SetFlag()
{
	m_bFlag = TTRUE;
}

#ifdef BARNYARD_COMMUNITY_PATCH

// The pictures that are only a background behind a menu (a wall of wooden planks): on screens that aren't 4:3 they cover
// the screen and lose a bit of their edges. The other pictures have content up to their edges (the texts of the loading
// and instruction screens, the art of the gallery, the logo slides) and are shown whole, with black bars.
static const TCHAR* s_aCoverPictures[] = {
	"bkg_End_wood", // and its variants (bkg_End_woodF)
};

TBOOL AGUIPicture::IsCoverPicture() const
{
	for ( TINT i = 0; i < TARRAYSIZE( s_aCoverPictures ); i++ )
	{
		if ( TStringManager::String8CompareNoCase( m_Name.GetString(), s_aCoverPictures[ i ], TStringManager::String8Length( s_aCoverPictures[ i ] ) ) == 0 )
			return TTRUE;
	}

	return TFALSE;
}

#endif // BARNYARD_COMMUNITY_PATCH

// $Barnyard: FUNCTION 0059d310
void AGUIPicture::Render()
{
	if ( m_bIsVisible )
	{
		TFLOAT fWidth, fHeight;
		AGUI2::GetSingleton()->GetDimensions( fWidth, fHeight );

#ifdef BARNYARD_COMMUNITY_PATCH
		// The pictures are 4:3 (a 638x477 picture on 640x480 tiles). On other screens they keep their aspect: the 4:3 box
		// in the middle of the screen gets the scale the whole screen had (unchanged on 4:3 screens).
		TFLOAT fScreenWidth  = fWidth;
		TFLOAT fScreenHeight = fHeight;
		AGUI2::Get4By3Box( fScreenWidth, fScreenHeight, IsCoverPicture(), fWidth, fHeight );
#endif

		T2GUITransform transform;
		transform.SetTranslation( 0.0f, 0.0f );

#ifdef BARNYARD_LATEST
		// The Wii build (8031f69c) scales by 638x445
		if ( m_bWiiLayout )
			transform.Scale( fWidth / 638.0f, fHeight / 445.0f );
		else
#endif
			transform.Scale( fWidth / 638.0f, fHeight / 477.0f );

		auto pRenderer = AGUI2::GetRenderer();
		pRenderer->PushTransform( transform, TVector2::VEC_ZERO, TVector2::VEC_ZERO );

		for ( TINT i = 0; i < m_iNumRects1; i++ )
		{
			pRenderer->SetMaterial( *m_pRectangles[ i ].ppMaterial );
			pRenderer->RenderRectangle( m_pRectangles[ i ].Pos1, m_pRectangles[ i ].Pos2, m_pRectangles[ i ].UV1, m_pRectangles[ i ].UV2 );
		}

		pRenderer->PopTransform();

#ifdef BARNYARD_COMMUNITY_PATCH
		// Black bars beside (or above and below) the picture: the screen isn't cleared under it, and the tiles reach a
		// unit past the 638x477 picture. Drawn after the picture, so they cut that off too. Only while the picture has
		// its tiles: a visible picture that was flushed draws nothing (e.g. in the sim after its loading screen).
		const TFLOAT fHalfScreenWidth  = fScreenWidth * 0.5f;
		const TFLOAT fHalfScreenHeight = fScreenHeight * 0.5f;
		const TFLOAT fHalfWidth        = fWidth * 0.5f;
		const TFLOAT fHalfHeight       = fHeight * 0.5f;

		if ( m_iNumRects1 > 0 && ( fWidth < fScreenWidth || fHeight < fScreenHeight ) )
		{
			pRenderer->SetColour( 0xff000000 );

			if ( fWidth < fScreenWidth )
			{
				pRenderer->RenderFilledRectangle( TVector2( -fHalfScreenWidth, -fHalfScreenHeight ), TVector2( -fHalfWidth, fHalfScreenHeight ) );
				pRenderer->RenderFilledRectangle( TVector2( fHalfWidth, -fHalfScreenHeight ), TVector2( fHalfScreenWidth, fHalfScreenHeight ) );
			}

			if ( fHeight < fScreenHeight )
			{
				pRenderer->RenderFilledRectangle( TVector2( -fHalfScreenWidth, -fHalfScreenHeight ), TVector2( fHalfScreenWidth, -fHalfHeight ) );
				pRenderer->RenderFilledRectangle( TVector2( -fHalfScreenWidth, fHalfHeight ), TVector2( fHalfScreenWidth, fHalfScreenHeight ) );
			}

			// The colour AGUI2's renderer starts the GUI with (the picture is drawn first)
			pRenderer->SetColour( 0xffffffff );
		}
#endif
	}
}
