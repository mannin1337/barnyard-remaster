#pragma once
#include "Toshi/Typedefs.h"

#include <d3d11.h>

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

	HRESULT Load();
	void    Unload();

	const D3D11_TEXTURE2D_DESC& GetImageInfo() const;
	TUINT                       GetWidth() const;
	TUINT                       GetHeight() const;
	TUINT                       GetMipLevels() const;

#ifdef BARNYARD_COMMUNITY_PATCH
	// Replaces the description Load took from the texture. For a texture loaded from a replacement file of another
	// size or format: the game reads the description of the original one (the GUI turns pixels into UVs with the
	// size, the model loader chooses the blending from the format), the D3D texture is the only thing that differs
	void SetImageInfo( const D3D11_TEXTURE2D_DESC& a_rcImageInfo ) { m_ImageInfo = a_rcImageInfo; }
#endif // BARNYARD_COMMUNITY_PATCH

	ID3D11ShaderResourceView* GetD3DTexture() { return m_pD3DTexture; }

protected:
	TUINT                     m_uiDataSize;
	const void*               m_pData;
	TUINT                     m_Unk1;
	TUINT                     m_Unk2;
	D3D11_TEXTURE2D_DESC      m_ImageInfo;
	ID3D11ShaderResourceView* m_pD3DTexture;
};

TOSHI_NAMESPACE_END
