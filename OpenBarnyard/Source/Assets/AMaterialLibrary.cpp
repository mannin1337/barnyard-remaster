#include "pch.h"
#include "AMaterialLibrary.h"
#include "AMaterialLibraryManager.h"

#include <Render/TRenderInterface.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

#ifdef BARNYARD_COMMUNITY_PATCH

// Where the HD Textures setting is saved: the registry key of the launcher's settings (AOptions::LoadSettings)
static const TCHAR* s_szSettingsKey         = "Software\\THQ\\Barnyard";
static const TCHAR* s_szTextureOverridesKey = "HDTextures";

// Loads Data\Textures\<name of the texture> into a_pTexture in place of the texture of the library: any image file
// the renderer reads, of any size. Returns TFALSE if there's no such file or it can't be used, a_pTexture is then
// as it was created and the texture of the library is loaded into it
static TBOOL LoadTextureOverride( T2Texture* a_pTexture, const AMaterialLibrary::TTL::TexInfo* a_pTexInfo )
{
	TString8 strOverridePath = "Data\\Textures\\";
	strOverridePath += a_pTexInfo->szFileName;
	FixPathSlashes( strOverridePath );

	TFile* pFile = TFile::Create( strOverridePath, TFILEMODE_READ );

	if ( !pFile )
		return TFALSE;

	const TUINT uiFileSize = TUINT( pFile->GetSize() );
	void*       pFileData  = ( uiFileSize != 0 ) ? TMalloc( uiFileSize ) : TNULL;
	TBOOL       bLoaded    = pFileData && pFile->Read( pFileData, uiFileSize ) == uiFileSize;

	pFile->Destroy();

#  ifdef TRENDERINTERFACE_DX11
	// The renderer reads DDS and TGA data, whatever the name of the file: anything else is left out here, from its
	// header, before the loader fails on it with an error (and an assert)
	D3D11_TEXTURE2D_DESC     oOverrideDesc;
	D3D11_RESOURCE_DIMENSION eOverrideDimension;

	if ( bLoaded && !TRenderD3DInterface::GetTextureDescFromFileInMemory( pFileData, uiFileSize, &oOverrideDesc, &eOverrideDimension ) )
	{
		TWARN( "AMaterialLibrary: %s isn't a DDS or TGA image\n", strOverridePath.GetString() );
		bLoaded = TFALSE;
	}
#  endif // TRENDERINTERFACE_DX11

	if ( bLoaded )
	{
		a_pTexture->SetData( pFileData, uiFileSize );

#  ifdef TRENDERINTERFACE_GL
		bLoaded = a_pTexture->Load();
#  else
		bLoaded = SUCCEEDED( a_pTexture->Load() );
#  endif
	}

#  ifdef TRENDERINTERFACE_DX8
	D3DXIMAGE_INFO oOriginalInfo;

	if ( bLoaded && SUCCEEDED( D3DXGetImageInfoFromFileInMemory( a_pTexInfo->pData, a_pTexInfo->uiDataSize, &oOriginalInfo ) ) )
	{
		// The game keeps reading the image info of the texture that is replaced: the GUI turns the pixel rectangles
		// of its texture sections and font glyphs into UVs with the size (T2GUITextureSectionManager, T2GUIFont) and
		// the model loader chooses the blending of a material from the format (AModelLoader), so a replacement of
		// another size or format keeps the layout and the look. Nothing needs the size of the D3D texture itself.
		// A replacement of another kind (a cube or a volume instead of a texture) can't be used
		if ( oOriginalInfo.ResourceType == a_pTexture->GetImageInfo().ResourceType )
			a_pTexture->SetImageInfo( oOriginalInfo );
		else
			bLoaded = TFALSE;
	}
#  elif defined( TRENDERINTERFACE_DX11 )
	D3D11_TEXTURE2D_DESC     oOriginalDesc;
	D3D11_RESOURCE_DIMENSION eOriginalDimension;

	if ( bLoaded && TRenderD3DInterface::GetTextureDescFromFileInMemory( a_pTexInfo->pData, a_pTexInfo->uiDataSize, &oOriginalDesc, &eOriginalDimension ) )
	{
		// As on DX8: the game reads the description the texture that is replaced would have (its size for the UVs
		// of the GUI, its format for the blending of the models), read from the header of its DDS. A replacement of
		// another kind (a cube or a volume instead of a texture) can't be used
		const TBOOL bSameKind = eOriginalDimension == eOverrideDimension &&
		    ( oOriginalDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE ) == ( oOverrideDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE );

		if ( bSameKind )
			a_pTexture->SetImageInfo( oOriginalDesc );
		else
			bLoaded = TFALSE;
	}
#  elif defined( TRENDERINTERFACE_GL )
	if ( bLoaded )
	{
		// As on DX8: the game reads the info of the texture that is replaced. The GL loader has no reader of the info
		// alone, so the original is loaded into a texture of its own just for it (GL textures are 2D ones only)
		T2Texture oOriginal;
		oOriginal.SetData( a_pTexInfo->pData, a_pTexInfo->uiDataSize );

		if ( oOriginal.Load() )
			a_pTexture->SetImageInfo( oOriginal.GetImageInfo() );

		// The data belongs to the library, the destructor only deletes the GL texture
		oOriginal.SetData( TNULL, 0 );
	}
#  endif // TRENDERINTERFACE_GL

	// The file isn't kept: the D3D texture is a managed one with its own copy of the image (a device reset doesn't
	// lose it) and T2Texture::Load, the only reader of the data, is only called while the library is loaded.
	// It would stay in the memory block of the renderer otherwise, as the copy of the library's texture does
	a_pTexture->SetData( TNULL, 0 );

	if ( !bLoaded )
	{
		TWARN( "AMaterialLibrary: %s can't be used, the texture of the library is loaded instead\n", strOverridePath.GetString() );
		a_pTexture->Unload();
	}

	if ( pFileData )
		TFree( pFileData );

	return bLoaded;
}

#endif // BARNYARD_COMMUNITY_PATCH

// $Barnyard: FUNCTION 00615cc0
AMaterialLibrary::AMaterialLibrary()
{
	m_iNumTextures = 0;
}

// $Barnyard: FUNCTION 00615d20
TBOOL AMaterialLibrary::LoadTTLData( void* a_pTTLData )
{
	TPROFILER_SCOPE();

#ifdef BARNYARD_COMMUNITY_PATCH

	TANONYMOUSVAR( TRenderInterface::MemBlockScope );

	// How many textures of the library were replaced from Data\Textures
	TINT iNumOverrides = 0;

#endif // BARNYARD_COMMUNITY_PATCH

	TTL* pTTL = TSTATICCAST( TTL, a_pTTLData );

	auto pLibList     = AMaterialLibraryManager::List::GetSingleton();
	TINT iNumTextures = 0;

	if ( ms_bSkipLoadedTextures )
	{
		for ( TINT i = 0; i < pTTL->iNumTextures; i++ )
		{
			if ( !pLibList->FindTexture( pTTL->pTextureInfos[ i ].szFileName, TNULL, TNULL ) )
			{
				iNumTextures++;
			}
		}
	}
	else
	{
		iNumTextures = pTTL->iNumTextures;
	}

	m_pTexturesArray = new ATexture[ iNumTextures ];
	m_pTextures      = m_pTexturesArray;
	m_iNumTextures   = iNumTextures;

	for ( TINT i = 0; i < iNumTextures; i++ )
	{
		auto pTexInfo = &pTTL->pTextureInfos[ i ];

		if ( !ms_bSkipLoadedTextures || !pLibList->FindTexture( pTexInfo->szFileName, TNULL, TNULL ) )
		{
			TASSERT( pTexInfo->eFormat == TTEX_FMT_WIN_DDS, "No support of other texture types" );
			m_pTextures[ i ].Name = pTexInfo->szFileName;

			if ( pTexInfo->eFormat == TTEX_FMT_WIN_DDS )
			{
				auto pTexture = new Toshi::T2Texture;

				if ( pTexture )
				{
					TUINT uiDataSize = pTexInfo->uiDataSize;
					void* pDataSrc   = pTexInfo->pData;

#ifdef BARNYARD_COMMUNITY_PATCH
					// The texture of the same name in Data\Textures replaces the one of the library
					if ( ms_iTextureOverrides && LoadTextureOverride( pTexture, pTexInfo ) )
					{
						iNumOverrides++;
					}
					else
#endif // BARNYARD_COMMUNITY_PATCH
					{
						void* pData = TMalloc( uiDataSize );
						Toshi::TUtil::MemCopy( pData, pDataSrc, uiDataSize );

						pTexture->SetData( pData, uiDataSize );
						pTexture->Load();
					}
				}

				m_pTextures[ i ].pTexture = pTexture;
			}
		}
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	if ( iNumOverrides > 0 )
		TINFO( "AMaterialLibrary: %d of the %d texture(s) of %s replaced from Data\\Textures\n", iNumOverrides, iNumTextures, m_Path.GetString() );
#endif // BARNYARD_COMMUNITY_PATCH

	return TTRUE;
}

#ifdef BARNYARD_COMMUNITY_PATCH

void AMaterialLibrary::LoadTextureOverridesSetting()
{
	HKEY  hKey;
	TBOOL bSaved = TFALSE;

	if ( ERROR_SUCCESS == RegOpenKeyExA( HKEY_CURRENT_USER, s_szSettingsKey, 0, KEY_READ, &hKey ) )
	{
		DWORD dwType = REG_NONE;
		DWORD dwData = 0;
		DWORD dwSize = sizeof( dwData );

		if ( ERROR_SUCCESS == RegQueryValueExA( hKey, s_szTextureOverridesKey, NULL, &dwType, (LPBYTE)&dwData, &dwSize ) &&
		     dwType == REG_DWORD )
		{
			ms_iTextureOverrides = dwData != FALSE;
			bSaved               = TTRUE;
		}

		RegCloseKey( hKey );
	}

	ms_iSavedTextureOverrides = ms_iTextureOverrides;
	TINFO( "HD textures (Data\\Textures): %s (%s)\n", ms_iTextureOverrides ? "on" : "off", bSaved ? "the saved setting" : "nothing saved, the default" );
}

TBOOL AMaterialLibrary::SetTextureOverridesEnabled( TBOOL a_bEnabled )
{
	// ms_iTextureOverrides stays: every library of this run is loaded with the same setting
	ms_iSavedTextureOverrides = a_bEnabled ? 1 : 0;

	// NOTE: the key isn't there before the launcher saved its settings
	HKEY  hKey;
	TBOOL bSaved = TFALSE;

	if ( ERROR_SUCCESS == RegCreateKeyExA( HKEY_CURRENT_USER, s_szSettingsKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL ) )
	{
		DWORD dwData = DWORD( ms_iSavedTextureOverrides );

		bSaved = ERROR_SUCCESS == RegSetValueExA( hKey, s_szTextureOverridesKey, 0, REG_DWORD, (LPBYTE)&dwData, sizeof( dwData ) );
		RegCloseKey( hKey );
	}

	TINFO( "HD textures (Data\\Textures): %s from the next start%s\n", ms_iSavedTextureOverrides ? "on" : "off", bSaved ? ", saved" : ", couldn't be saved" );
	return bSaved;
}

#endif // BARNYARD_COMMUNITY_PATCH

// $Barnyard: FUNCTION 00616000
TBOOL AMaterialLibrary::LoadTTLFile( const TCHAR* a_szFilePath )
{
	m_TRB.Load( a_szFilePath );
	LoadTTLData( m_TRB.GetSymbolAddress( "TTL" ) );
	m_TRB.Close();

	return TTRUE;
}

// $Barnyard: FUNCTION 00616040
TBOOL AMaterialLibrary::LoadTRBFile( Toshi::TTRB* a_pTRB )
{
	void* pTTL = a_pTRB->GetSymbolAddress( "TTL" );
	TASSERT( TNULL != pTTL );

	LoadTTLData( pTTL );
	a_pTRB->Close();

	return TTRUE;
}

void AMaterialLibrary::Destroy()
{
	Node::Remove();
	DestroyTextures();
	delete this;
}

// $Barnyard: FUNCTION 00615f60
void AMaterialLibrary::DestroyTextures()
{
	for ( TINT i = 0; i < m_iNumTextures; i++ )
	{
		if ( m_pTextures[ i ].pTexture )
		{
			delete m_pTextures[ i ].pTexture;
			m_pTextures[ i ].pTexture = TNULL;
		}
	}

	m_pTextures    = TNULL;
	m_iNumTextures = 0;
	delete[] m_pTexturesArray;
	m_pTexturesArray = TNULL;
}

// $Barnyard: FUNCTION 00615f10
TINT AMaterialLibrary::FindTextureIndex( const TCHAR* a_szTextureName )
{
	for ( TINT i = 0; i < m_iNumTextures; i++ )
	{
		if ( Toshi::TStringManager::String8CompareNoCase( m_pTextures[ i ].Name, a_szTextureName ) == 0 )
		{
			return i;
		}
	}

	return -1;
}
