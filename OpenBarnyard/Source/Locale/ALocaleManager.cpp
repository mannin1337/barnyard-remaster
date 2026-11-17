#include "pch.h"
#include "ALocaleManager.h"

#ifdef BARNYARD_COMMUNITY_PATCH
#  include "AModStrings.h"

#  include <File/TFile.h>
#endif

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

#ifdef BARNYARD_COMMUNITY_PATCH
// The size of the buffer the original gives T2Locale
static constexpr TSIZE LOCALE_BUFFER_SIZE = 307200;
#endif

// NOTE: inlined in the original (see CreateLocaleManager, 00402740)
ALocaleManager::ALocaleManager()
    : T2Locale( Lang_NUMOF, 307200, Toshi::GetGlobalAllocator()->Malloc( 307200, 4 ) )
{
#ifdef BARNYARD_COMMUNITY_PATCH
	m_pFirstBuffer = m_pBuffer;
#endif
}

// $Barnyard: FUNCTION 00402730
// $Barnyard: FUNCTION 00402880
ALocaleManager::~ALocaleManager()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	// The TRB points into the buffers, close it before they're gone
	m_Locale.Close();
	m_StringTable = TNULL;

	// NOTE: the first buffer is freed by DestroyLocaleManager (AppBoot.cpp, GetBuffer always returns it)
	if ( m_pTableBuffer && m_pTableBuffer != m_pFirstBuffer )
		Toshi::GetGlobalAllocator()->Free( m_pTableBuffer );

	if ( m_pOldBuffer && m_pOldBuffer != m_pFirstBuffer )
		Toshi::GetGlobalAllocator()->Free( m_pOldBuffer );

	AModStrings::Unload();
#endif
}

// $Barnyard: FUNCTION 00402860
const TCHAR* ALocaleManager::GetLanguageFilename( Lang a_eLang )
{
	TASSERT( a_eLang >= 0 && a_eLang < Lang_NUMOF );
	return s_pLangDataFileNames[ a_eLang ];
}

// $Barnyard: FUNCTION 00402850
const TCHAR* ALocaleManager::GetCurrentLanguageName()
{
	return GetLanguageName( m_LangId );
}

const TCHAR* ALocaleManager::GetVOLocaleDir( Lang a_eLang )
{
	switch ( a_eLang )
	{
		case Lang_German: return "GER/";
		case Lang_Spanish: return "SPA/";
		case Lang_French: return "FRE/";
		default: return "";
	}
}

// $Barnyard: FUNCTION 004027e0
const TCHAR* ALocaleManager::GetLanguageName( Lang a_eLang )
{
	switch ( a_eLang )
	{
		case Lang_Japanese: return "JPN";
		case Lang_German: return "DEU";
		case Lang_Italian: return "ITA";
		case Lang_Spanish: return "ESL";
		case Lang_French: return "FRE";
		case Lang_Netherlandic: return "NLD";
#ifdef BARNYARD_COMMUNITY_PATCH
		case Lang_Hungarian: return "HUN";
#endif
		default: return "ENG";
	}
}

#ifdef BARNYARD_COMMUNITY_PATCH

const TWCHAR* ALocaleManager::GetLanguageNativeName( Lang a_eLang )
{
	switch ( a_eLang )
	{
		case Lang_English: return L"English (US)";
		case Lang_EnglishUK: return L"English (UK)";
		case Lang_Japanese: return L"Japanese";
		case Lang_German: return L"Deutsch";
		case Lang_Italian: return L"Italiano";
		case Lang_Spanish: return L"Espa\u00f1ol";
		case Lang_French: return L"Fran\u00e7ais";
		case Lang_Netherlandic: return L"Nederlands";
		case Lang_Hungarian: return L"Magyar";
		default: return L"?";
	}
}

const TCHAR* ALocaleManager::GetSubtitleFolder( Lang a_eLang )
{
	// NOTE: the original picks GBR for English (UK) and the language name for the rest (ASubtitleManager::LoadSubtitles)
	return ( a_eLang == Lang_EnglishUK ) ? "GBR" : GetLanguageName( a_eLang );
}

TBOOL ALocaleManager::IsLanguageFilePresent( Lang a_eLang )
{
	if ( a_eLang < 0 || a_eLang >= Lang_NUMOF )
		return TFALSE;

	return GetTRBLoadSize( s_pLangDataFileNames[ a_eLang ] ) != 0;
}

TSIZE ALocaleManager::GetTRBLoadSize( const TCHAR* a_szFileName )
{
	TFile* pFile = TFile::Create( a_szFileName, TFILEMODE_READ );

	if ( !pFile )
		return 0;

	// TTRB::ProcessForm (TTRB.cpp) gets from the allocator: the HDRX hunk, every section (its size in HDRX, the
	// decompressed size for SECC) and the SYMB hunk. HEAD (the old header) gets a header and its sections the same way
	const TSIZE uiFileSize = pFile->GetSize();
	TSIZE       uiSize     = 0;
	TUINT32     aHeader[ 3 ];

	if ( pFile->Read( aHeader, sizeof( aHeader ) ) == sizeof( aHeader ) && aHeader[ 0 ] == TFourCC( "TSFL" ) && aHeader[ 2 ] == TFourCC( "TRBF" ) )
	{
		TSIZE uiPos = sizeof( aHeader );

		while ( uiPos + 8 <= uiFileSize )
		{
			TUINT32 aHunk[ 2 ];

			if ( !pFile->Seek( TINT( uiPos ), TSEEK_SET ) || pFile->Read( aHunk, sizeof( aHunk ) ) != sizeof( aHunk ) )
				break;

			const TUINT32 uiName     = aHunk[ 0 ];
			const TUINT32 uiHunkSize = aHunk[ 1 ];

			if ( uiName == TFourCC( "FORM" ) )
			{
				// A nested form is read like the outer one, its hunks follow its type
				uiPos += 8 + 4;
				continue;
			}

			if ( uiName == TFourCC( "HDRX" ) || uiName == TFourCC( "HEAD" ) )
			{
				const TBOOL bHDRX          = uiName == TFourCC( "HDRX" );
				const TSIZE uiSectionsPos  = bHDRX ? 8 : 4;  // after the version and the count / after the count
				const TSIZE uiSectionSize  = bHDRX ? 16 : 12; // TTRB::SecInfo / the HEAD entry
				TINT32      aCount[ 2 ]    = { 0, 0 };

				pFile->Read( aCount, bHDRX ? 8 : 4 );
				const TINT32 iNumSections = bHDRX ? aCount[ 1 ] : aCount[ 0 ];

				uiSize += bHDRX ? uiHunkSize : 8 + 16 * TSIZE( TMath::Max( iNumSections, 0 ) );

				for ( TINT32 i = 0; i < iNumSections && i < 4096; i++ )
				{
					TUINT32 aSection[ 2 ];

					if ( !pFile->Seek( TINT( uiPos + 8 + uiSectionsPos + i * uiSectionSize ), TSEEK_SET ) || pFile->Read( aSection, sizeof( aSection ) ) != sizeof( aSection ) )
						break;

					// TINT16 unused, TINT16 alignment, TUINT32 size
					uiSize += aSection[ 1 ];
				}
			}
			else if ( uiName == TFourCC( "SYMB" ) )
			{
				uiSize += uiHunkSize;
			}

			uiPos += 8 + ( ( uiHunkSize + 3 ) & ~3 );
		}
	}

	pFile->Destroy();

	// Some room for what this doesn't see
	return ( uiSize != 0 ) ? uiSize + 1024 : 0;
}

TBOOL ALocaleManager::LoadTable( Lang a_eLang, void* a_pBuffer )
{
	// The allocator of T2Locale takes from m_BufferPos
	m_Locale.Close();
	m_pBuffer   = a_pBuffer;
	m_BufferPos = a_pBuffer;

	const TBOOL bLoaded = m_Locale.Load( GetLanguageFilename( a_eLang ) ) == TTRB::ERROR_OK;
	m_StringTable       = bLoaded ? m_Locale.CastSymbol<LocaleStrings>( "LocaleStrings" ) : TNULL;

	// GetBuffer returns the first buffer again: DestroyLocaleManager (AppBoot.cpp) frees what it returns
	m_pBuffer   = m_pFirstBuffer;
	m_BufferPos = m_pFirstBuffer;

	return m_StringTable != TNULL && m_StringTable->m_numstrings > 0;
}

TBOOL ALocaleManager::SetLanguage( Lang a_eLang )
{
	if ( a_eLang < 0 || a_eLang >= Lang_NUMOF )
		return TFALSE;

	const Lang eAssetLanguage = GetAssetLanguage( a_eLang );

	if ( a_eLang == m_eTextLanguage && eAssetLanguage == m_LangId && m_StringTable )
		return TTRUE;

	const TSIZE uiSize = GetTRBLoadSize( GetLanguageFilename( a_eLang ) );

	if ( uiSize == 0 )
	{
		TWARN( "ALocaleManager: %s is missing, the language stays %s\n", GetLanguageFilename( a_eLang ), m_StringTable ? GetLanguageName( m_eTextLanguage ) : "unset" );
		return TFALSE;
	}

	const TBOOL bFirstTable = m_StringTable == TNULL;

	if ( bFirstTable && uiSize <= LOCALE_BUFFER_SIZE )
	{
		// The first table goes into the buffer of the original
		if ( !LoadTable( a_eLang, m_pFirstBuffer ) )
		{
			TERROR( "ALocaleManager: couldn't load %s\n", GetLanguageFilename( a_eLang ) );
			return TFALSE;
		}

		m_pTableBuffer = m_pFirstBuffer;
	}
	else
	{
		// A buffer of its own: the strings of the current table stay where they are until the next change, a text that
		// still points at one shows it until it's refreshed
		void* pNewBuffer = Toshi::GetGlobalAllocator()->Malloc( uiSize, 4 );

		if ( !pNewBuffer || !LoadTable( a_eLang, pNewBuffer ) )
		{
			TERROR( "ALocaleManager: couldn't load %s (%u bytes)\n", GetLanguageFilename( a_eLang ), TUINT( uiSize ) );

			// Back to the previous table, the same file goes to the same place
			if ( !bFirstTable )
				LoadTable( m_eTextLanguage, m_pTableBuffer );

			if ( pNewBuffer )
				Toshi::GetGlobalAllocator()->Free( pNewBuffer );

			return TFALSE;
		}

		// The buffer before the previous one can go, the previous one is kept. The first buffer stays, it's freed by
		// DestroyLocaleManager
		if ( m_pOldBuffer && m_pOldBuffer != m_pFirstBuffer )
			Toshi::GetGlobalAllocator()->Free( m_pOldBuffer );

		m_pOldBuffer   = m_pTableBuffer;
		m_pTableBuffer = pNewBuffer;
	}

	m_LangId        = eAssetLanguage;
	m_eTextLanguage = a_eLang;

	TINFO( "ALocaleManager: language %s (%d strings, %u bytes of %s), assets of %s\n", GetLanguageName( a_eLang ), GetNumStrings(), TUINT( uiSize ), GetLanguageFilename( a_eLang ), GetLanguageName( eAssetLanguage ) );

	AModStrings::Load( GetLanguageFilename( a_eLang ) );
	return TTRUE;
}

#endif // BARNYARD_COMMUNITY_PATCH
