#pragma once
#include "Toshi/Typedefs.h"

#include <d3dx8.h>

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

	const D3DXIMAGE_INFO& GetImageInfo() const;
	TUINT                 GetWidth() const;
	TUINT                 GetHeight() const;
	TUINT                 GetMipLevels() const;

#ifdef BARNYARD_COMMUNITY_PATCH
	// Replaces the image info Load took from the data. For a texture loaded from a replacement file of another
	// size or format: the game reads the info of the original one (the GUI turns pixels into UVs with the size,
	// the model loader chooses the blending from the format), the D3D texture is the only thing that differs
	void SetImageInfo( const D3DXIMAGE_INFO& a_rcImageInfo ) { m_ImageInfo = a_rcImageInfo; }
#endif // BARNYARD_COMMUNITY_PATCH

	IDirect3DTexture8*       GetD3DTexture() { return m_pD3DTexture; }
	IDirect3DVolumeTexture8* GetD3DVolumeTexture() { return m_pD3DVolumeTexture; }
	IDirect3DCubeTexture8*   GetD3DCubeTexture() { return m_pD3DCubeTexture; }

protected:
	TUINT          m_uiDataSize;
	const void*    m_pData;
	TUINT          m_Unk1;
	TUINT          m_Unk2;
	D3DXIMAGE_INFO m_ImageInfo;
	union
	{
		IDirect3DTexture8*       m_pD3DTexture;
		IDirect3DVolumeTexture8* m_pD3DVolumeTexture;
		IDirect3DCubeTexture8*   m_pD3DCubeTexture;
	};
};

TOSHI_NAMESPACE_END
