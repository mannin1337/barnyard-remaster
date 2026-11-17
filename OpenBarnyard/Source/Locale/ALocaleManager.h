#pragma once
#include <Toshi/T2Allocator.h>
#include <T2Locale/T2Locale.h>

class MODAPI ALocaleManager : public Toshi::T2Locale
{
public:
	constexpr static const TCHAR* s_pLangDataFileNames[] = {
		"Data/Locale/eng.trb",
		"Data/Locale/eng-uk.trb",
		"Data/Locale/jpn.trb",
		"Data/Locale/ger.trb",
		"Data/Locale/ita.trb",
		"Data/Locale/spa.trb",
		"Data/Locale/fre.trb",
		"Data/Locale/nld.trb",
#ifdef BARNYARD_COMMUNITY_PATCH
		"Data/Locale/hun.trb",
#endif
	};

	enum Lang_ : Lang
	{
		Lang_UNKNOWN = -1,
		Lang_English,
		Lang_EnglishUK,
		Lang_Japanese,
		Lang_German,
		Lang_Italian,
		Lang_Spanish,
		Lang_French,
		Lang_Netherlandic,
#ifdef BARNYARD_COMMUNITY_PATCH
		// NOTE: not in any build. A language of texts only (Data/Locale/hun.trb, Data/Subs/HUN and Data/Locale/hun_mod.json,
		// none of them comes with the game): voices, movies and textures stay English (UK), see GetAssetLanguage
		Lang_Hungarian,
#endif
		Lang_NUMOF
	};

	constexpr static TSIZE NUM_LOCALES = TARRAYSIZE( s_pLangDataFileNames );
	TSTATICASSERT( Lang_NUMOF == NUM_LOCALES );

public:
	ALocaleManager();
	~ALocaleManager();

	virtual const TCHAR* GetLanguageFilename( Lang a_eLang ) override;

	const TCHAR* GetCurrentLanguageName();
	const TCHAR* GetVOLocaleDir( Lang a_eLang );

#ifdef BARNYARD_COMMUNITY_PATCH
	// NOTE: hides T2Locale::SetLanguage. Loads the locale table of a_eLang into a buffer of its size (the original one
	// has a fixed size and no check) and keeps the strings of the previous table readable until the next change, so a
	// text that still shows one isn't garbage before it's refreshed. A missing or broken file keeps the language that
	// was set and returns TFALSE. GetLanguage returns the language of the assets (English (UK) for Hungarian),
	// GetTextLanguage the one of the texts. Loads the texts of the mod of the language too (AModStrings)
	TBOOL SetLanguage( Lang a_eLang );

	// The language of the texts (the locale table, the subtitles and the texts of the mod)
	Lang GetTextLanguage() const { return m_eTextLanguage; }

	// The language whose voices, movies and localised textures a language uses
	static Lang GetAssetLanguage( Lang a_eLang ) { return ( a_eLang == Lang_Hungarian ) ? Lang_EnglishUK : a_eLang; }

	// TTRUE if the locale table of a language is there (Data/Locale/hun.trb isn't part of the game)
	static TBOOL IsLanguageFilePresent( Lang a_eLang );

	// The name of a language in that language, for the language menu
	static const TWCHAR* GetLanguageNativeName( Lang a_eLang );

	// The subtitle folder of a language (Data/Subs/<folder>)
	static const TCHAR* GetSubtitleFolder( Lang a_eLang );

private:
	// Bytes the bump allocator of T2Locale takes for a TRB file (its header, sections and symbols), 0 if there's no
	// such file or it's not a TRB
	static TSIZE GetTRBLoadSize( const TCHAR* a_szFileName );

	// Loads the table of a language into a buffer, TFALSE if the file couldn't be loaded
	TBOOL LoadTable( Lang a_eLang, void* a_pBuffer );

	Lang  m_eTextLanguage = Lang_UNKNOWN; // see GetTextLanguage
	void* m_pFirstBuffer  = TNULL;        // the buffer of CreateLocaleManager (GetBuffer returns it)
	void* m_pTableBuffer  = TNULL;        // the buffer of the current table
	void* m_pOldBuffer    = TNULL;        // the buffer of the previous table, freed by the next change
#endif

public:
	static const TCHAR* GetLanguageName( Lang a_eLang );

	// $Barnyard: FUNCTION 00402870
	static TBOOL IsLanguageSupported( Lang a_eLang )
	{
#ifdef BARNYARD_COMMUNITY_PATCH
		// NOTE: the PC data has every locale, the fonts lack the Japanese glyphs (AFrontEndLanguageState)
		return a_eLang >= 0 && a_eLang < Lang_NUMOF && a_eLang != Lang_Japanese;
#else
		return a_eLang == Lang_EnglishUK;
#endif
	}

	// NOTE: 004c4a20 is a shared function that returns 1
	static Lang GetDefaultLanguage() { return Lang_EnglishUK; }

	static ALocaleManager* Interface() { return TSTATICCAST( ALocaleManager, ms_pSingleton ); }
};

TSINGLETON_DECLARE_INHERITED_ALIAS( Toshi::T2Locale, ALocaleManager, LocaleManager );
