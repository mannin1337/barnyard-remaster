#pragma once
//-----------------------------------------------------------------------------
// HD fonts (BARNYARD_COMMUNITY_PATCH): the glyphs of a game font drawn from a TrueType font as signed distance
// fields, sharp at any resolution, with the fill, the dark outline and the soft shadow of the atlas glyphs.
//
// A font of Data/GUI/fonts.trb that has a layout table here (Rekord26, the comic font, from CCThatsAllFolks) draws
// the characters of the table from an SDF atlas, built from the TTF glyph by glyph the first time a character is
// drawn. The characters the table doesn't have (the button icons, the letters the TTF has no glyph for) and every
// other font still come from their atlas. The table puts each TTF glyph where the atlas glyph is: it was measured
// on the retail atlas, which was made from the same font. Accented letters are the TTF letter and the TTF accent
// fitted into the atlas glyph's letter and accent, o and u with double acute two narrow TTF acutes over the dots
// of o and u umlaut. Measuring and layout don't change (the metrics of the font are used as before), only the look
// of the glyphs does.
//
// The renderer draws the glyphs: the DX11 GUI renderer installs a T2GUIFontSDFRenderer (render.hd_fonts), without
// one (DX8, GL) every glyph comes from the atlas.
//-----------------------------------------------------------------------------
#ifdef BARNYARD_COMMUNITY_PATCH

TOSHI_NAMESPACE_START

struct T2GUIFontDef;

// A glyph quad: the corners in the coordinates of the GUI (like T2GUIRenderer::RenderRectangle) and in the atlas
struct T2GUIFontSDFQuad
{
	TFLOAT afPos[ 4 ]; // left, top, right, bottom
	TFLOAT afUV[ 4 ];  // left, top, right, bottom
};

// How the atlas is read and the look of the glyphs, distances in font pixels (the pixels of the glyph data of the
// game font, unscaled)
struct T2GUIFontSDFStyle
{
	TFLOAT fDistanceScale;    // distance from the outline of the TTF glyph = texel * scale + bias (texel 0..1),
	TFLOAT fDistanceBias;     // positive inside
	TFLOAT fFillGrow;         // the fill reaches this far outside the outline of the TTF glyph
	TFLOAT fOutlineAlpha;     // the dark outline: its alpha at the fill,
	TFLOAT fOutlineSlope;     // minus this much per font pixel away from the fill,
	TFLOAT fOutlineFadeStart; // fading out (smoothstep) from here
	TFLOAT fOutlineFadeEnd;   // to here, font pixels from the fill
};

// What draws the glyphs (the DX11 GUI renderer)
class T2GUIFontSDFRenderer
{
public:
	virtual ~T2GUIFontSDFRenderer() = default;

	// The glyphs are drawn from the TTF (the setting is on)
	virtual TBOOL IsEnabled() = 0;

	// A texture of one 8 bit channel cleared to 0, TNULL if it can't be created
	virtual void* CreateAtlas( TUINT a_uiWidth, TUINT a_uiHeight ) = 0;

	// Copies a_uiWidth x a_uiHeight bytes (rows one after the other) into the atlas
	virtual void UpdateAtlas( void* a_pAtlas, TUINT a_uiX, TUINT a_uiY, TUINT a_uiWidth, TUINT a_uiHeight, const TUINT8* a_pData ) = 0;

	virtual void DestroyAtlas( void* a_pAtlas ) = 0;

	// Draws the quads in their order with the material, transform and scissor the GUI renderer has set,
	// a_uiColour being the colour of the fill (ARGB) and its alpha that of the whole glyph
	virtual void RenderQuads( void* a_pAtlas, const T2GUIFontSDFStyle& a_rcStyle, const T2GUIFontSDFQuad* a_pQuads, TINT a_iNumQuads, TUINT32 a_uiColour ) = 0;
};

// Where a character of the game font comes from: up to three TTF glyphs, each scaled and placed in the glyph of the
// game font (font pixels from the top left of the quad T2GUIFont draws for it, y down)
struct T2GUIFontSDFLayout
{
	struct Piece
	{
		TUINT16 uiTTFChar;
		TFLOAT  fScaleX;  // font pixels per TTF unit
		TFLOAT  fScaleY;
		TFLOAT  fOriginX; // where the origin of the TTF glyph (on its baseline) goes
		TFLOAT  fOriginY;
	};

	TUINT16 uiChar;
	TINT16  aiGlyph[ 4 ]; // width, height, left padding and y offset of the glyph data the layout was made for
	TINT    iNumPieces;
	Piece   aPieces[ 3 ];
};

class T2GUIFontSDF
{
public:
	static constexpr TINT MAX_QUADS = 64; // per draw

	struct Desc; // a game font and its TTF (T2GUIFontSDF.cpp)
	struct GlyphCache;

public:
	// The renderer of the glyphs, TNULL: none (the atlases of the fonts are released)
	static void                  SetRenderer( T2GUIFontSDFRenderer* a_pRenderer );
	static T2GUIFontSDFRenderer* GetRenderer() { return ms_pRenderer; }

	// The HD font of a game font (by its texture), TNULL if it has none. Call it once the character map of the font
	// is final (T2GUIFont::Create, after SetupExtraGlyphs): the characters whose glyph data isn't the one of the
	// layout table are drawn from the atlas
	static T2GUIFontSDF* FindForFont( const T2GUIFontDef* a_pFontDef );

	// The TTF of an HD font is there (the default of the setting)
	static TBOOL IsTTFThere();

	// The glyphs can be drawn now: a renderer that has them on and the TTF (loaded the first time, without an
	// error if it isn't there)
	TBOOL IsActive();

	// The glyphs of one line: Begin, then AddGlyph for every character (a_fX, a_fY: the top left of the quad
	// T2GUIFont would draw for it), Flush before anything else is drawn and at the end
	void  Begin( TUINT32 a_uiColour );
	TBOOL AddGlyph( TWCHAR a_wcChar, TFLOAT a_fX, TFLOAT a_fY, TFLOAT a_fScale ); // TFALSE: draw the atlas glyph
	void  Flush();

private:
	T2GUIFontSDF( const Desc* a_pDesc );

	TBOOL Load();
	void  Unload();
	TBOOL MakeGlyph( TINT a_iIndex );
	TINT  FindLayout( TWCHAR a_wcChar ) const;

private:
	inline static T2GUIFontSDFRenderer* ms_pRenderer = TNULL;

	const Desc*  m_pDesc;
	TBOOL        m_bTriedLoad   = TFALSE;
	TBOOL        m_bLoaded      = TFALSE;
	TUINT8*      m_pTTFData     = TNULL;
	void*        m_pFontInfo    = TNULL; // stbtt_fontinfo
	void*        m_pAtlas       = TNULL; // of the renderer
	TUINT        m_uiShelfX     = 0;     // packing: the free place on the current shelf
	TUINT        m_uiShelfY     = 0;
	TUINT        m_uiShelfH     = 0;
	TBOOL        m_bAtlasFull   = TFALSE;
	GlyphCache*  m_pGlyphs      = TNULL; // one per layout
	TUINT32      m_uiColour     = 0;
	TINT         m_iNumQuads    = 0;
	TFLOAT       m_fMakeSeconds = 0.0f; // building glyphs so far, for the log
	TINT         m_iNumMade     = 0;

	T2GUIFontSDFQuad m_aQuads[ MAX_QUADS ];
};

TOSHI_NAMESPACE_END

#endif // BARNYARD_COMMUNITY_PATCH
