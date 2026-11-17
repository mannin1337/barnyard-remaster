#pragma once
#include "Toshi/Typedefs.h"

#include "Platform/GL/TRenderInterface_GL.h"

TOSHI_NAMESPACE_START

class MODAPI T2Texture
{
public:
	T2Texture();
	~T2Texture();

	void SetData( const void* a_pData, TUINT a_uiDataSize );
	void GetData( const void*& a_rData, TUINT& a_rDataSize )
	{
		a_rData     = m_pData;
		a_rDataSize = m_uiDataSize;
	}

	TBOOL Load();
	void  Unload();

	const TGLTextureInfo& GetImageInfo() const;
	TUINT                 GetWidth() const;
	TUINT                 GetHeight() const;
	TUINT                 GetMipLevels() const;

#ifdef BARNYARD_COMMUNITY_PATCH
	// Replaces the info Load took from the data. For a texture loaded from a replacement file of another size or
	// format: the game reads the info of the original one (the GUI turns pixels into UVs with the size, the model
	// loader chooses the blending from the format), the GL texture is the only thing that differs
	void SetImageInfo( const TGLTextureInfo& a_rcImageInfo ) { m_ImageInfo = a_rcImageInfo; }
#endif // BARNYARD_COMMUNITY_PATCH

	GLuint GetGLTexture() { return m_uiTexture; }

protected:
	TUINT                     m_uiDataSize;
	const void*               m_pData;
	TUINT                     m_Unk1;
	TUINT                     m_Unk2;
	TGLTextureInfo m_ImageInfo;
	GLuint         m_uiTexture;
};

TOSHI_NAMESPACE_END
