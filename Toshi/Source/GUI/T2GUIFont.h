#pragma once
#include "T2GUIMaterial.h"
#include "T2GUIFontResource.h"

TOSHI_NAMESPACE_START

class T2GUIRenderer;

struct T2GUIFontDefCharacterData
{
	TINT8   Unk1;
	TINT8   Unk2;
	TUINT16 ui16PosX;
	TUINT16 ui16PosY;
	TUINT8  ui8CharWidth;
	TUINT8  ui8CharHeight;
	TINT8   iLeftPadding;
	TINT8   iRightPadding;
	TINT8   iYOffset;
	TINT8   Unk6;
};

#ifdef BARNYARD_COMMUNITY_PATCH
// NOTE: not in the original. A letter a font doesn't have, drawn with parts of its other glyphs: the Hungarian
// letters with a double acute (o, u with two accents) are a letter of the font and its acute accent twice. The tables
// of the fonts are in T2GUIFont.cpp
struct T2GUIFontExtraGlyph
{
	struct Quad
	{
		TUINT16 uiSrcChar;       // the glyph the part is cut from (it has to be there)
		TUINT16 auiSrcRect[ 4 ]; // left, top, right, bottom in the texture, in the pixels of the glyph data
		TFLOAT  afDstRect[ 4 ];  // left, top, right, bottom from the top left of the base glyph, unscaled
	};

	TUINT16 uiChar;     // the letter
	TUINT16 uiBaseChar; // the letter has the metrics of this glyph (size, advance and position)
	Quad    aQuads[ 3 ];
};

struct T2GUIFontExtraGlyphs;
class T2GUIFontSDF;
#endif // BARNYARD_COMMUNITY_PATCH

struct T2GUIFontDef
{
	void*                      Unk1;
	const TCHAR*               szTextureNames;
	TINT8                      iLetterSpacing;
	TINT8                      iLinesMargin;
	TUINT8                     uiLineHeight;
	TUINT8                     uiNumMaterials;
	TUINT16                    ui16MaxCharacter;
	TUINT16                    ui16MinCharacter;
	TINT32                     Unk3;
	TINT32*                    pCharactersMap;
	T2GUIFontDefCharacterData* pCharactersData;
};

class MODAPI T2GUIFont
{
public:
	enum TextAlign : TUINT32
	{
		TextAlign_Left,
		TextAlign_Center,
		TextAlign_Right
	};

	// Called for every drawn character with its index in the line; may change the offset of the character (and of the rest of the line)
	using CharCallback = void( __stdcall* )( TINT a_iCharIndex, TFLOAT* a_pOffsetX, TFLOAT* a_pOffsetY );

public:
	// $Barnyard: FUNCTION 006c2ce0
	T2GUIFont() {}
	~T2GUIFont();

	// Frees the font definition if it's owned, the materials otherwise
	void Destroy();

	// Creates a font that shares the materials of this one and has its own copy of the definition
	T2GUIFont* Clone();

	T2GUIFontDef* GetFontDef() const { return m_pFontDef; }

	TBOOL Create( T2GUIFontDef* a_pFontDef );

	void DrawTextWrapped( const TWCHAR* a_wszText, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight, TUINT32 a_uiColour, TFLOAT a_fScale, TextAlign a_eAlign, void* a_fnCallback = TNULL );
	void DrawTextSingleLine( const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fX, TFLOAT a_fY, TUINT32 a_uiColour, TFLOAT a_fScale, void* a_fnCallback = TNULL );
	void DrawTextSingleLine( const TWCHAR* a_wszText, TFLOAT a_fX, TFLOAT a_fY, TUINT32 a_uiColour, TFLOAT a_fScale );

	TFLOAT GetTextHeightWrapped( const TWCHAR* a_wszText, TFLOAT a_fMaxWidth, TFLOAT a_fScale = 1.0f );
	TFLOAT GetTextWidth( const TWCHAR* a_wszText, TFLOAT a_fScale = 1.0f );
	TFLOAT GetTextWidth( const TWCHAR* a_wszText, TINT a_iTextLength, TFLOAT a_fScale = 1.0f );
	TFLOAT GetCharWidth( TWCHAR a_wcChar, TFLOAT a_fScale = 1.0f );

	TFLOAT GetLinesMargin();
	TFLOAT GetLineHeight();

#ifdef BARNYARD_LATEST
	//-----------------------------------------------------------------------------
	// Wii T2GUIFont: controller button icons in the text, a limit of characters to draw and
	// the Wii line breaking.
	// NOTE: the Wii fonts (Data/XUI/Fonts) are paged (256 pages of 256 characters, looked up
	// by 804a1358). Paged fonts aren't supported, so these functions use the PC (flat) lookup.
	//-----------------------------------------------------------------------------

	// Returns the width of an icon character or -1.0f if the character isn't an icon
	using IconWidthCallback = TFLOAT ( * )( TINT a_iChar );

	// Draws an icon character and returns TTRUE if the character is an icon
	using IconRenderCallback = TBOOL ( * )( T2GUIRenderer* a_pRenderer, TINT a_iChar, TFLOAT* a_pOutAdvance, TFLOAT a_fLineHeight, TFLOAT a_fX, TFLOAT a_fY );

	// a_bIncludeNewLine: a line that ends with a new line character includes it
	// a_iMaxChars: draws only that many characters (-1 draws the whole text)
	void DrawTextWrapped( const TWCHAR* a_wszText, TUINT32 a_uiColour, TextAlign a_eAlign, CharCallback a_fnCallback, IconWidthCallback a_fnIconWidth, IconRenderCallback a_fnIconRender, TBOOL a_bIncludeNewLine, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight, TFLOAT a_fScale, TINT a_iMaxChars );
	void DrawTextSingleLine( const TWCHAR* a_wszText, TINT a_iTextLength, TUINT32 a_uiColour, CharCallback a_fnCallback, IconWidthCallback a_fnIconWidth, IconRenderCallback a_fnIconRender, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fScale );

	TFLOAT GetTextHeightWrapped( const TWCHAR* a_wszText, IconWidthCallback a_fnIconWidth, TBOOL a_bIncludeNewLine, TINT a_iMaxChars, TFLOAT a_fMaxWidth, TFLOAT a_fScale );
	TFLOAT GetTextWidth( const TWCHAR* a_wszText, IconWidthCallback a_fnIconWidth, TFLOAT a_fScale );
	TFLOAT GetTextWidth( const TWCHAR* a_wszText, TINT a_iTextLength, IconWidthCallback a_fnIconWidth, TFLOAT a_fScale );
	TFLOAT GetCharWidth( TWCHAR a_wcChar, IconWidthCallback a_fnIconWidth, TFLOAT a_fScale );

	// Returns the number of characters of the line that starts at *a_ppText (skips the spaces it
	// starts with) and its width, 0 at the end of the text
	TINT GetLineLength( const TWCHAR** a_ppText, TFLOAT* a_pOutWidth, IconWidthCallback a_fnIconWidth, TBOOL a_bIncludeNewLine, TFLOAT a_fMaxWidth, TFLOAT a_fScale );

	// Wii line breaking: TFALSE if a line can't end between the two characters (inside a word,
	// after an opening bracket, before closing punctuation)
	TBOOL CanBreakLine( TWCHAR a_wcChar, TWCHAR a_wcNextChar );

	// The materials DrawTextSingleLine has drawn a pass of (one bit per material)
	static void ResetDrawnMaterials();

	inline static TUINT32 ms_auiDrawnMaterials[ 8 ];

	// TFALSE draws only the first line of the wrapped texts (AGUI2XUIText sets it while it renders)
	inline static TBOOL ms_bMultipleLines = TTRUE;
#endif

private:
	// Character data of a character of the font or TNULL (the lookup the PC functions inline)
	const T2GUIFontDefCharacterData* FindCharData( TWCHAR a_wcChar ) const
	{
#ifdef BARNYARD_COMMUNITY_PATCH
		if ( IsXboxGlyph( a_wcChar ) ) a_wcChar = L'\x0F0D'; // Button outline, also used for metrics.
#endif
		if ( m_pFontDef->ui16MinCharacter <= a_wcChar && a_wcChar <= m_pFontDef->ui16MaxCharacter )
		{
			TINT iCharIndex = m_pFontDef->pCharactersMap[ a_wcChar - m_pFontDef->ui16MinCharacter ];

			if ( iCharIndex >= 0 )
				return &m_pFontDef->pCharactersData[ iCharIndex ];
		}

		return TNULL;
	}

	// Material of a character (the high byte of the character)
	// NOTE: the Wii build takes the material from the character data (Unk6) of its paged fonts
	TUINT GetCharMaterialIndex( TWCHAR a_wcChar, const T2GUIFontDefCharacterData* a_pCharData ) const
	{
		return ( m_pFontDef->uiNumMaterials > 1 ) ? ( a_wcChar >> 8 ) : 0;
	}

	T2GUIMaterial* GetCharMaterial( TWCHAR a_wcChar, const T2GUIFontDefCharacterData* a_pCharData ) const
	{
		return m_ppMaterials[ GetCharMaterialIndex( a_wcChar, a_pCharData ) ];
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	static TBOOL IsXboxGlyph( TWCHAR a_wcChar ) { return a_wcChar >= 0x0F21 && a_wcChar <= 0x0F2C; }
	void DrawXboxGlyphLabel( TWCHAR a_wcChar, TUINT32 a_uiColour, T2GUIMaterial* a_pOutlineMaterial,
	                         TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fWidth, TFLOAT a_fHeight, TFLOAT a_fScale ) const;

	// Fixes the character map of a known font and adds the letters it can draw with parts of its glyphs (Create)
	void SetupExtraGlyphs();

	// The extra glyph of a character or TNULL. The character map gives such a letter the glyph data of its base
	// glyph, so it's measured like any other character
	const T2GUIFontExtraGlyph* FindExtraGlyph( TWCHAR a_wcChar ) const;

	// Draws the parts of an extra glyph, a_fX and a_fY being the top left of its base glyph
	void DrawExtraGlyph( const T2GUIFontExtraGlyph* a_pExtraGlyph, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fScale, TFLOAT a_fXToUV, TFLOAT a_fYToUV ) const;
#endif

private:
	T2GUIFontDef*   m_pFontDef    = TNULL;
	T2GUIMaterial** m_ppMaterials = TNULL;
	TBOOL           m_bFlag       = TFALSE;
#ifdef BARNYARD_COMMUNITY_PATCH
	const T2GUIFontExtraGlyphs* m_pExtraGlyphs = TNULL; // NOTE: not in the original, see SetupExtraGlyphs
	T2GUIFontSDF*               m_pSDF         = TNULL; // NOTE: not in the original, the HD font (T2GUIFontSDF.h)
#endif
};

// NOTE: the type is unknown, it's deleted through the virtual destructor in slot 0
struct T2GUIFontRefUnknown
{
	virtual ~T2GUIFontRefUnknown() = default;
};

struct T2GUIFontRef
{
	TCHAR*               szName;
	T2GUIFont*           pFont;
	T2GUIFontRefUnknown* pUnk1;
	T2GUIFontResource*   pFontResource;
	TUINT                uiNumRefs;
};

TOSHI_NAMESPACE_END
