#pragma once
//-----------------------------------------------------------------------------
// HD fonts on DX11 (render.hd_fonts): draws the SDF glyphs of T2GUIFontSDF over the GUI renderer's state, with a
// pixel shader of its own for the fill, the dark outline and the soft shadow of the atlas glyphs. The GUI renderer
// (AGUI2RendererDX8) installs it while it exists.
//-----------------------------------------------------------------------------
#include <GUI/T2GUIFontSDF.h>

class AGUI2FontSDFDX11 : public Toshi::T2GUIFontSDFRenderer
{
public:
	static AGUI2FontSDFDX11& GetSingleton();

	// Installs it for T2GUIFontSDF, or takes it away and releases its shaders
	static void Install();
	static void Uninstall();

	//-----------------------------------------------------------------------------
	// Toshi::T2GUIFontSDFRenderer
	//-----------------------------------------------------------------------------
	virtual TBOOL IsEnabled() OVERRIDE;
	virtual void* CreateAtlas( TUINT a_uiWidth, TUINT a_uiHeight ) OVERRIDE;
	virtual void  UpdateAtlas( void* a_pAtlas, TUINT a_uiX, TUINT a_uiY, TUINT a_uiWidth, TUINT a_uiHeight, const TUINT8* a_pData ) OVERRIDE;
	virtual void  DestroyAtlas( void* a_pAtlas ) OVERRIDE;
	virtual void  RenderQuads( void* a_pAtlas, const Toshi::T2GUIFontSDFStyle& a_rcStyle, const Toshi::T2GUIFontSDFQuad* a_pQuads, TINT a_iNumQuads, TUINT32 a_uiColour ) OVERRIDE;

private:
	TBOOL CreateShaders();
	void  DestroyShaders();
};
