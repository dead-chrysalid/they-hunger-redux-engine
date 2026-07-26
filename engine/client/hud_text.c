/*
hud_text.c - engine-owned TrueType rendering for classic HUD text
*/
#include "common.h"
#include "filesystem.h"
#include "client.h"
#include "hud_text.h"

#if XASH_WIN32
#include <wchar.h>
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define HUD_TEXT_REFERENCE_HEIGHT 768.0f
#define HUD_TEXT_STYLE_COUNT 4
#define HUD_TEXT_GLYPH_CACHE_SIZE 2048
#define HUD_TEXT_ATLAS_SIZE 512
#define HUD_TEXT_ATLAS_GLYPH_PAD 2
#define HUD_TEXT_MAX_ATLAS_PAGES 64
#define HUD_TEXT_MIN_SIZE 4
#define HUD_TEXT_MAX_SIZE 96

#define HUD_TEXT_DEFAULT_FONT "resource/fonts/Roboto-SemiBold.ttf"

typedef enum hud_text_backend_e
{
	HUD_TEXT_BACKEND_STB = 0,
	HUD_TEXT_BACKEND_WIN32_GDI
} hud_text_backend_t;

typedef struct hud_text_font_s
{
	char path[MAX_SYSPATH];
	byte *data;
	fs_offset_t dataLength;
	stbtt_fontinfo info;
	float scale;
	int designSize;
	int pixelSize;
	int height;
	int ascent;
	hud_text_backend_t backend;
#if XASH_WIN32
	HANDLE fontResource;
	HDC deviceContext;
	HFONT fontHandle;
	HGDIOBJ previousFont;
	char faceName[LF_FACESIZE];
#endif
	qboolean ready;
} hud_text_font_t;

typedef struct hud_text_glyph_s
{
	qboolean occupied;
	qboolean uploaded;
	int style;
	int ch;
	int atlasPage;
	int atlasX;
	int atlasY;
	int advance;
	int xOffset;
	int yOffset;
	int width;
	int height;
} hud_text_glyph_t;

typedef struct hud_text_atlas_page_s
{
	qboolean occupied;
	qboolean dirty;
	int style;
	int texture;
	int cursorX;
	int cursorY;
	int rowHeight;
	byte *pixels;
	char textureName[64];
} hud_text_atlas_page_t;

static hud_text_font_t hudTextFonts[HUD_TEXT_STYLE_COUNT];
static hud_text_glyph_t hudTextGlyphs[HUD_TEXT_GLYPH_CACHE_SIZE];
static hud_text_atlas_page_t hudTextAtlasPages[HUD_TEXT_MAX_ATLAS_PAGES];
static unsigned int hudTextGeneration;
static unsigned int hudTextStbFallbackGlyphs;
static qboolean hudTextReady;
static qboolean hudTextTestVisible;

static qboolean CL_HudTextPrewarmAtlases( void );

static hud_text_style_t CL_HudTextNormalizeStyle( hud_text_style_t style )
{
	if( style == HUD_TEXT_STYLE_SMALL || style == HUD_TEXT_STYLE_LARGE )
		return style;
	return HUD_TEXT_STYLE_MEDIUM;
}

#if XASH_WIN32
static qboolean CL_HudTextWideFaceNamesMatch( const wchar_t *a, const wchar_t *b )
{
	wchar_t normalizedA[LF_FACESIZE];
	wchar_t normalizedB[LF_FACESIZE];
	int readIndex;
	int writeIndex;
	wchar_t ch;

	if( !a || !b )
		return false;

	writeIndex = 0;
	for( readIndex = 0; a[readIndex] && writeIndex + 1 < LF_FACESIZE; readIndex++ )
	{
		ch = a[readIndex];
		if( ch >= L'A' && ch <= L'Z' )
			ch = ch - L'A' + L'a';
		if(( ch >= L'a' && ch <= L'z' ) || ( ch >= L'0' && ch <= L'9' ))
			normalizedA[writeIndex++] = ch;
	}
	normalizedA[writeIndex] = L'\0';

	writeIndex = 0;
	for( readIndex = 0; b[readIndex] && writeIndex + 1 < LF_FACESIZE; readIndex++ )
	{
		ch = b[readIndex];
		if( ch >= L'A' && ch <= L'Z' )
			ch = ch - L'A' + L'a';
		if(( ch >= L'a' && ch <= L'z' ) || ( ch >= L'0' && ch <= L'9' ))
			normalizedB[writeIndex++] = ch;
	}
	normalizedB[writeIndex] = L'\0';

	return normalizedA[0] && normalizedB[0] && !wcscmp( normalizedA, normalizedB );
}

static qboolean CL_HudTextAppendWideCandidate( wchar_t candidates[][LF_FACESIZE],
	int *candidateCount, int maxCandidates, const wchar_t *candidate )
{
	int i;

	if( !candidate || !candidate[0] || *candidateCount >= maxCandidates )
		return false;

	for( i = 0; i < *candidateCount; i++ )
	{
		if( CL_HudTextWideFaceNamesMatch( candidates[i], candidate ))
			return false;
	}

	for( i = 0; candidate[i] && i + 1 < LF_FACESIZE; i++ )
		candidates[*candidateCount][i] = candidate[i];
	candidates[*candidateCount][i] = L'\0';
	( *candidateCount )++;
	return true;
}

static qboolean CL_HudTextAppendStbNameCandidate( hud_text_font_t *font,
	wchar_t candidates[][LF_FACESIZE], int *candidateCount, int maxCandidates,
	int platform, int encoding, int language, int nameId )
{
	const char *name;
	int nameLength = 0;
	int wideLength;
	int i;
	wchar_t converted[LF_FACESIZE];

	name = stbtt_GetFontNameString( &font->info, &nameLength,
		platform, encoding, language, nameId );
	if( !name || nameLength < 2 )
		return false;

	wideLength = Q_min( nameLength / 2, LF_FACESIZE - 1 );
	for( i = 0; i < wideLength; i++ )
		converted[i] = (wchar_t)(((unsigned char)name[i * 2] << 8) | (unsigned char)name[i * 2 + 1] );
	converted[wideLength] = L'\0';

	return CL_HudTextAppendWideCandidate( candidates, candidateCount, maxCandidates, converted );
}

static int CL_HudTextCollectWin32FaceNames( hud_text_font_t *font,
	wchar_t candidates[][LF_FACESIZE], int maxCandidates )
{
	static const int nameIds[] = { 16, 1 };
	static const int encodings[] = { 1, 10 };
	char fileBase[MAX_SYSPATH];
	wchar_t converted[LF_FACESIZE];
	int candidateCount = 0;
	int encodingIndex;
	int nameIndex;

	for( encodingIndex = 0; encodingIndex < ARRAYSIZE( encodings ); encodingIndex++ )
	{
		for( nameIndex = 0; nameIndex < ARRAYSIZE( nameIds ); nameIndex++ )
		{
			CL_HudTextAppendStbNameCandidate( font, candidates, &candidateCount,
				maxCandidates, 3, encodings[encodingIndex], 0x0409, nameIds[nameIndex] );
		}
	}

	for( nameIndex = 0; nameIndex < ARRAYSIZE( nameIds ); nameIndex++ )
	{
		CL_HudTextAppendStbNameCandidate( font, candidates, &candidateCount,
			maxCandidates, 0, 3, 0, nameIds[nameIndex] );
	}

	COM_FileBase( font->path, fileBase );
	if( MultiByteToWideChar( CP_UTF8, 0, fileBase, -1, converted, ARRAYSIZE( converted )) <= 0 )
		MultiByteToWideChar( CP_ACP, 0, fileBase, -1, converted, ARRAYSIZE( converted ));
	converted[ARRAYSIZE( converted ) - 1] = L'\0';
	CL_HudTextAppendWideCandidate( candidates, &candidateCount, maxCandidates, converted );

	return candidateCount;
}

static void CL_HudTextFreeWin32Font( hud_text_font_t *font )
{
	if( font->deviceContext && font->previousFont )
		SelectObject( font->deviceContext, font->previousFont );
	if( font->fontHandle )
		DeleteObject( font->fontHandle );
	if( font->deviceContext )
		DeleteDC( font->deviceContext );
	if( font->fontResource )
		RemoveFontMemResourceEx( font->fontResource );

	font->fontResource = NULL;
	font->deviceContext = NULL;
	font->fontHandle = NULL;
	font->previousFont = NULL;
	font->faceName[0] = '\0';
}

static qboolean CL_HudTextTryLoadWin32Font( hud_text_font_t *font )
{
	wchar_t candidates[16][LF_FACESIZE];
	wchar_t actualFace[LF_FACESIZE];
	TEXTMETRICW metrics;
	DWORD numFonts = 0;
	int candidateCount;
	int i;
	int utf8Length;
	HGDIOBJ previous;

	memset( candidates, 0, sizeof( candidates ));
	font->fontResource = AddFontMemResourceEx( font->data, (DWORD)font->dataLength, NULL, &numFonts );
	if( !font->fontResource || !numFonts )
	{
		CL_HudTextFreeWin32Font( font );
		return false;
	}

	font->deviceContext = CreateCompatibleDC( NULL );
	if( !font->deviceContext )
	{
		CL_HudTextFreeWin32Font( font );
		return false;
	}

	candidateCount = CL_HudTextCollectWin32FaceNames( font, candidates, ARRAYSIZE( candidates ));
	for( i = 0; i < candidateCount; i++ )
	{
		font->fontHandle = CreateFontW( font->pixelSize, 0, 0, 0, FW_DONTCARE,
			FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, PROOF_QUALITY, DEFAULT_PITCH | FF_DONTCARE, candidates[i] );
		if( !font->fontHandle )
			continue;

		previous = SelectObject( font->deviceContext, font->fontHandle );
		actualFace[0] = L'\0';
		if( GetTextFaceW( font->deviceContext, ARRAYSIZE( actualFace ), actualFace ) > 0
			&& CL_HudTextWideFaceNamesMatch( actualFace, candidates[i] ))
		{
			font->previousFont = previous;
			break;
		}

		if( previous )
			SelectObject( font->deviceContext, previous );
		DeleteObject( font->fontHandle );
		font->fontHandle = NULL;
	}

	if( !font->fontHandle )
	{
		CL_HudTextFreeWin32Font( font );
		return false;
	}

	SetMapMode( font->deviceContext, MM_TEXT );
	memset( &metrics, 0, sizeof( metrics ));
	if( !GetTextMetricsW( font->deviceContext, &metrics ))
	{
		CL_HudTextFreeWin32Font( font );
		return false;
	}

	font->height = metrics.tmHeight;
	font->ascent = metrics.tmAscent;
	font->backend = HUD_TEXT_BACKEND_WIN32_GDI;
	utf8Length = WideCharToMultiByte( CP_UTF8, 0, actualFace, -1,
		font->faceName, sizeof( font->faceName ), NULL, NULL );
	if( utf8Length <= 0 )
		Q_strncpy( font->faceName, "<unavailable>", sizeof( font->faceName ));
	return font->height > 0;
}
#endif

static void CL_HudTextSetDefaults( void )
{
	int i;

	for( i = 0; i < HUD_TEXT_STYLE_COUNT; i++ )
		Q_strncpy( hudTextFonts[i].path, HUD_TEXT_DEFAULT_FONT, sizeof( hudTextFonts[i].path ));

	hudTextFonts[HUD_TEXT_STYLE_DEFAULT].designSize = 28;
	hudTextFonts[HUD_TEXT_STYLE_SMALL].designSize = 18;
	hudTextFonts[HUD_TEXT_STYLE_MEDIUM].designSize = 28;
	hudTextFonts[HUD_TEXT_STYLE_LARGE].designSize = 36;
}

static void CL_HudTextSetFont( hud_text_style_t style, const char *path )
{
	if( COM_CheckString( path ))
		Q_strncpy( hudTextFonts[style].path, path, sizeof( hudTextFonts[style].path ));
}

static void CL_HudTextSetSize( hud_text_style_t style, const char *value )
{
	int size = value ? atoi( value ) : hudTextFonts[style].designSize;
	hudTextFonts[style].designSize = bound( HUD_TEXT_MIN_SIZE, size, HUD_TEXT_MAX_SIZE );
}

static qboolean CL_HudTextLoadScheme( void )
{
	char token[1024];
	char value[1024];
	char *file = (char *)g_fsapi.LoadFile( "gfx/shell/fonts.lst", NULL, false );
	char *cursor = file;

	CL_HudTextSetDefaults();

	if( !file )
		return false;

	while(( cursor = COM_ParseFile( cursor, token, sizeof( token ))) != NULL )
	{
		cursor = COM_ParseFile( cursor, value, sizeof( value ));
		if( !cursor )
			break;

		if( !Q_stricmp( token, "SMALL_FONT" ))
			CL_HudTextSetFont( HUD_TEXT_STYLE_SMALL, value );
		else if( !Q_stricmp( token, "SMALL_SIZE" ))
			CL_HudTextSetSize( HUD_TEXT_STYLE_SMALL, value );
		else if( !Q_stricmp( token, "MEDIUM_FONT" ))
		{
			CL_HudTextSetFont( HUD_TEXT_STYLE_DEFAULT, value );
			CL_HudTextSetFont( HUD_TEXT_STYLE_MEDIUM, value );
		}
		else if( !Q_stricmp( token, "MEDIUM_SIZE" ))
		{
			CL_HudTextSetSize( HUD_TEXT_STYLE_DEFAULT, value );
			CL_HudTextSetSize( HUD_TEXT_STYLE_MEDIUM, value );
		}
		else if( !Q_stricmp( token, "LARGE_FONT" ))
			CL_HudTextSetFont( HUD_TEXT_STYLE_LARGE, value );
		else if( !Q_stricmp( token, "LARGE_SIZE" ))
			CL_HudTextSetSize( HUD_TEXT_STYLE_LARGE, value );
	}

	Mem_Free( file );
	return true;
}

static void CL_HudTextLoadLegacyFallback( void )
{
	char token[1024];
	char value[1024];
	char font[MAX_SYSPATH];
	char *file = (char *)g_fsapi.LoadFile( "gfx/shell/colors.lst", NULL, false );
	char *cursor = file;
	int size = hud_truetype_size ? (int)( hud_truetype_size->value + 0.5f ) : 28;
	int i;

	Q_strncpy( font, HUD_TEXT_DEFAULT_FONT, sizeof( font ));
	size = bound( HUD_TEXT_MIN_SIZE, size, HUD_TEXT_MAX_SIZE );

	if( file )
	{
		while(( cursor = COM_ParseFile( cursor, token, sizeof( token ))) != NULL )
		{
			if( !Q_stricmp( token, "MENU_FONT" ))
			{
				cursor = COM_ParseFile( cursor, value, sizeof( value ));
				if( !cursor )
					break;
				Q_strncpy( font, value, sizeof( font ));
			}
		}
		Mem_Free( file );
	}

	for( i = 0; i < HUD_TEXT_STYLE_COUNT; i++ )
	{
		Q_strncpy( hudTextFonts[i].path, font, sizeof( hudTextFonts[i].path ));
		hudTextFonts[i].designSize = size;
	}
}

static void CL_HudTextClearGlyphs( void )
{
	int i;

	if( ref.initialized )
	{
		for( i = 0; i < HUD_TEXT_MAX_ATLAS_PAGES; i++ )
		{
			if( hudTextAtlasPages[i].occupied && hudTextAtlasPages[i].texture > 0 )
				ref.dllFuncs.GL_FreeTexture( hudTextAtlasPages[i].texture );
		}
	}

	for( i = 0; i < HUD_TEXT_MAX_ATLAS_PAGES; i++ )
	{
		if( hudTextAtlasPages[i].pixels )
			Mem_Free( hudTextAtlasPages[i].pixels );
	}

	memset( hudTextGlyphs, 0, sizeof( hudTextGlyphs ));
	memset( hudTextAtlasPages, 0, sizeof( hudTextAtlasPages ));
}

static void CL_HudTextFreeFonts( void )
{
	int i;

	for( i = 0; i < HUD_TEXT_STYLE_COUNT; i++ )
	{
#if XASH_WIN32
		CL_HudTextFreeWin32Font( &hudTextFonts[i] );
#endif
		if( hudTextFonts[i].data )
			Mem_Free( hudTextFonts[i].data );
		hudTextFonts[i].data = NULL;
		hudTextFonts[i].dataLength = 0;
		hudTextFonts[i].ready = false;
	}
	hudTextReady = false;
}

static qboolean CL_HudTextLoadFont( hud_text_font_t *font )
{
	int ascent, descent;
	fs_offset_t length = 0;
	float screenScale = refState.height > 0 ? refState.height / HUD_TEXT_REFERENCE_HEIGHT : 1.0f;

	font->pixelSize = bound( HUD_TEXT_MIN_SIZE, (int)( font->designSize * screenScale + 0.5f ), 256 );
	font->data = g_fsapi.LoadFile( font->path, &length, false );
	if( !font->data )
	{
		Con_Printf( S_WARN "HUD text: unable to load font %s\n", font->path );
		return false;
	}

	if( !stbtt_InitFont( &font->info, font->data, stbtt_GetFontOffsetForIndex( font->data, 0 )))
	{
		Con_Printf( S_WARN "HUD text: invalid TrueType font %s\n", font->path );
		Mem_Free( font->data );
		font->data = NULL;
		return false;
	}

	font->dataLength = length;
	font->scale = stbtt_ScaleForPixelHeight( &font->info, (float)font->pixelSize );
	stbtt_GetFontVMetrics( &font->info, &ascent, &descent, NULL );
	font->ascent = (int)( ascent * font->scale + 0.5f );
	font->height = (int)(( ascent - descent ) * font->scale + 0.5f );
	font->backend = HUD_TEXT_BACKEND_STB;

#if XASH_WIN32
	if( length > 0 && length <= UINT_MAX && CL_HudTextTryLoadWin32Font( font ))
	{
		font->ready = true;
		return true;
	}
	Con_Printf( S_WARN "HUD text: Win32 hinted rasterizer unavailable for %s, using stb_truetype\n",
		font->path );
#endif

	font->ready = font->height > 0;
	return font->ready;
}

void CL_HudTextVidInit( void )
{
	int i;

	CL_HudTextClearGlyphs();
	CL_HudTextFreeFonts();
	memset( hudTextFonts, 0, sizeof( hudTextFonts ));
	hudTextGeneration++;
	hudTextStbFallbackGlyphs = 0;

	if( !CL_HudTextLoadScheme() )
	{
		CL_HudTextLoadLegacyFallback();
		Con_DPrintf( "HUD text: fonts.lst not found, using legacy universal font fallback\n" );
	}

	for( i = HUD_TEXT_STYLE_SMALL; i <= HUD_TEXT_STYLE_LARGE; i++ )
	{
		if( !CL_HudTextLoadFont( &hudTextFonts[i] ))
			continue;
		Con_DPrintf( "HUD text: loaded %s at %ipx for style %i with %s\n",
			hudTextFonts[i].path, hudTextFonts[i].pixelSize, i,
			hudTextFonts[i].backend == HUD_TEXT_BACKEND_WIN32_GDI ? "Win32 GDI hinting" : "stb_truetype" );
	}

	hudTextReady = hudTextFonts[HUD_TEXT_STYLE_SMALL].ready
		&& hudTextFonts[HUD_TEXT_STYLE_MEDIUM].ready
		&& hudTextFonts[HUD_TEXT_STYLE_LARGE].ready;
	if( hudTextReady && !CL_HudTextPrewarmAtlases() )
	{
		Con_Printf( S_WARN "HUD text: unable to build immutable font atlases, using legacy bitmap fallback\n" );
		CL_HudTextClearGlyphs();
		hudTextReady = false;
	}
}

void CL_HudTextShutdown( void )
{
	CL_HudTextClearGlyphs();
	CL_HudTextFreeFonts();
	memset( hudTextFonts, 0, sizeof( hudTextFonts ));
}

qboolean CL_HudTextReady( void )
{
	return hudTextReady;
}

static unsigned int CL_HudTextGlyphHash( int style, int ch )
{
	return ((unsigned int)ch * 2654435761u + (unsigned int)style * 97u ) % HUD_TEXT_GLYPH_CACHE_SIZE;
}

static int CL_HudTextGetGlyphAdvance( hud_text_font_t *font, int ch )
{
	int advance;
	int bearing;

#if XASH_WIN32
	if( font->backend == HUD_TEXT_BACKEND_WIN32_GDI && ch <= 0xFFFF )
	{
		ABC widths;
		SIZE size;
		wchar_t character = (wchar_t)ch;

		memset( &widths, 0, sizeof( widths ));
		if( GetCharABCWidthsW( font->deviceContext, ch, ch, &widths ))
			return Q_max( 1, widths.abcA + (int)widths.abcB + widths.abcC );
		if( GetTextExtentPoint32W( font->deviceContext, &character, 1, &size ))
			return Q_max( 1, size.cx );
	}
#endif

	stbtt_GetCodepointHMetrics( &font->info, ch, &advance, &bearing );
	return Q_max( 1, (int)( advance * font->scale + 0.5f ));
}

static hud_text_glyph_t *CL_HudTextFindGlyph( hud_text_style_t style, int ch, qboolean create )
{
	hud_text_font_t *font;
	hud_text_glyph_t *glyph;
	unsigned int index;
	unsigned int probe;

	style = CL_HudTextNormalizeStyle( style );
	font = &hudTextFonts[style];
	if( !font->ready || ch <= 0 )
		return NULL;

	index = CL_HudTextGlyphHash( style, ch );
	for( probe = 0; probe < HUD_TEXT_GLYPH_CACHE_SIZE; probe++ )
	{
		glyph = &hudTextGlyphs[( index + probe ) % HUD_TEXT_GLYPH_CACHE_SIZE];
		if( glyph->occupied )
		{
			if( glyph->style == style && glyph->ch == ch )
				return glyph;
			continue;
		}

		if( !create )
			return NULL;

		memset( glyph, 0, sizeof( *glyph ));
		glyph->occupied = true;
		glyph->style = style;
		glyph->ch = ch;
		glyph->atlasPage = -1;
		glyph->advance = CL_HudTextGetGlyphAdvance( font, ch );
		return glyph;
	}

	return NULL;
}

static byte *CL_HudTextRasterizeStbGlyph( hud_text_font_t *font, int ch,
	int *width, int *height, int *xOffset, int *yOffset )
{
	byte *stbBitmap;
	byte *bitmap;
	size_t bitmapSize;

	stbBitmap = stbtt_GetCodepointBitmap( &font->info, font->scale, font->scale,
		ch, width, height, xOffset, yOffset );
	if( !stbBitmap || *width <= 0 || *height <= 0 )
	{
		if( stbBitmap )
			stbtt_FreeBitmap( stbBitmap, NULL );
		return NULL;
	}

	bitmapSize = (size_t)*width * (size_t)*height;
	bitmap = Mem_Malloc( host.mempool, bitmapSize );
	memcpy( bitmap, stbBitmap, bitmapSize );
	stbtt_FreeBitmap( stbBitmap, NULL );
	return bitmap;
}

#if XASH_WIN32
static byte *CL_HudTextRasterizeWin32Glyph( hud_text_font_t *font, int ch,
	int *width, int *height, int *xOffset, int *yOffset )
{
	GLYPHMETRICS metrics;
	MAT2 transform = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
	DWORD bytesNeeded;
	byte *gdiBitmap;
	byte *bitmap;
	int sourceStride;
	int x;
	int y;

	if( ch > 0xFFFF )
		return NULL;

	memset( &metrics, 0, sizeof( metrics ));
	bytesNeeded = GetGlyphOutlineW( font->deviceContext, ch, GGO_GRAY8_BITMAP,
		&metrics, 0, NULL, &transform );
	if( bytesNeeded == GDI_ERROR || bytesNeeded == 0
		|| metrics.gmBlackBoxX == 0 || metrics.gmBlackBoxY == 0 )
		return NULL;

	gdiBitmap = Mem_Malloc( host.mempool, bytesNeeded );
	if( GetGlyphOutlineW( font->deviceContext, ch, GGO_GRAY8_BITMAP,
		&metrics, bytesNeeded, gdiBitmap, &transform ) == GDI_ERROR )
	{
		Mem_Free( gdiBitmap );
		return NULL;
	}

	*width = metrics.gmBlackBoxX;
	*height = metrics.gmBlackBoxY;
	*xOffset = metrics.gmptGlyphOrigin.x;
	*yOffset = -metrics.gmptGlyphOrigin.y;
	sourceStride = ( *width + 3 ) & ~3;
	bitmap = Mem_Malloc( host.mempool, (size_t)*width * (size_t)*height );

	for( y = 0; y < *height; y++ )
	{
		for( x = 0; x < *width; x++ )
		{
			int coverage = gdiBitmap[(size_t)y * sourceStride + x];
			bitmap[(size_t)y * *width + x] = (byte)bound( 0, ( coverage * 255 + 32 ) / 64, 255 );
		}
	}

	Mem_Free( gdiBitmap );
	return bitmap;
}
#endif

static byte *CL_HudTextRasterizeGlyph( hud_text_font_t *font, int ch,
	int *width, int *height, int *xOffset, int *yOffset )
{
#if XASH_WIN32
	byte *bitmap;

	if( font->backend == HUD_TEXT_BACKEND_WIN32_GDI )
	{
		bitmap = CL_HudTextRasterizeWin32Glyph( font, ch, width, height, xOffset, yOffset );
		if( bitmap )
			return bitmap;
		hudTextStbFallbackGlyphs++;
	}
#endif

	return CL_HudTextRasterizeStbGlyph( font, ch, width, height, xOffset, yOffset );
}

static hud_text_atlas_page_t *CL_HudTextCreateAtlasPage( int style, int *pageIndex )
{
	hud_text_atlas_page_t *page;
	size_t pixelCount;
	size_t i;
	int index;

	for( index = 0; index < HUD_TEXT_MAX_ATLAS_PAGES; index++ )
	{
		page = &hudTextAtlasPages[index];
		if( page->occupied )
			continue;

		memset( page, 0, sizeof( *page ));
		page->occupied = true;
		page->dirty = true;
		page->style = style;
		page->pixels = Mem_Calloc( host.mempool,
			(size_t)HUD_TEXT_ATLAS_SIZE * HUD_TEXT_ATLAS_SIZE * 4 );
		pixelCount = (size_t)HUD_TEXT_ATLAS_SIZE * HUD_TEXT_ATLAS_SIZE;
		for( i = 0; i < pixelCount; i++ )
		{
			page->pixels[i * 4 + 0] = 255;
			page->pixels[i * 4 + 1] = 255;
			page->pixels[i * 4 + 2] = 255;
		}
		Q_snprintf( page->textureName, sizeof( page->textureName ),
			"#hudtext_atlas_%u_%i_%i", hudTextGeneration, style, index );
		*pageIndex = index;
		return page;
	}

	return NULL;
}

static qboolean CL_HudTextTryPackAtlasRect( hud_text_atlas_page_t *page,
	int width, int height, int *atlasX, int *atlasY )
{
	int packedWidth = width + HUD_TEXT_ATLAS_GLYPH_PAD * 2;
	int packedHeight = height + HUD_TEXT_ATLAS_GLYPH_PAD * 2;
	int cursorX = page->cursorX;
	int cursorY = page->cursorY;
	int rowHeight = page->rowHeight;

	if( packedWidth > HUD_TEXT_ATLAS_SIZE || packedHeight > HUD_TEXT_ATLAS_SIZE )
		return false;

	if( cursorX + packedWidth > HUD_TEXT_ATLAS_SIZE )
	{
		cursorX = 0;
		cursorY += rowHeight;
		rowHeight = 0;
	}

	if( cursorY + packedHeight > HUD_TEXT_ATLAS_SIZE )
		return false;

	*atlasX = cursorX + HUD_TEXT_ATLAS_GLYPH_PAD;
	*atlasY = cursorY + HUD_TEXT_ATLAS_GLYPH_PAD;
	page->cursorX = cursorX + packedWidth;
	page->cursorY = cursorY;
	page->rowHeight = Q_max( rowHeight, packedHeight );
	return true;
}

static qboolean CL_HudTextPlaceGlyphInAtlas( hud_text_glyph_t *glyph,
	const byte *bitmap, int width, int height )
{
	hud_text_atlas_page_t *page;
	byte *destination;
	int pageIndex;
	int atlasX;
	int atlasY;
	int index;
	int x;
	int y;

	page = NULL;
	pageIndex = -1;
	for( index = 0; index < HUD_TEXT_MAX_ATLAS_PAGES; index++ )
	{
		if( !hudTextAtlasPages[index].occupied || hudTextAtlasPages[index].texture > 0
			|| hudTextAtlasPages[index].style != glyph->style )
			continue;
		if( CL_HudTextTryPackAtlasRect( &hudTextAtlasPages[index],
			width, height, &atlasX, &atlasY ))
		{
			page = &hudTextAtlasPages[index];
			pageIndex = index;
			break;
		}
	}

	if( !page )
	{
		page = CL_HudTextCreateAtlasPage( glyph->style, &pageIndex );
		if( !page || !CL_HudTextTryPackAtlasRect( page, width, height, &atlasX, &atlasY ))
			return false;
	}

	for( y = 0; y < height; y++ )
	{
		destination = page->pixels + ((size_t)( atlasY + y ) * HUD_TEXT_ATLAS_SIZE + atlasX) * 4;
		for( x = 0; x < width; x++ )
		{
			destination[x * 4 + 3] = bitmap[(size_t)y * width + x];
		}
	}

	page->dirty = true;
	glyph->atlasPage = pageIndex;
	glyph->atlasX = atlasX;
	glyph->atlasY = atlasY;
	glyph->width = width;
	glyph->height = height;
	return true;
}

static qboolean CL_HudTextUploadAtlasPage( hud_text_atlas_page_t *page )
{
	int flags = TF_IMAGE | TF_NOMIPMAP | TF_HAS_ALPHA;
	int texture;

	if( !page || !page->occupied )
		return false;
	if( page->texture > 0 )
		return true;

	texture = ref.dllFuncs.GL_CreateTexture( page->textureName,
		HUD_TEXT_ATLAS_SIZE, HUD_TEXT_ATLAS_SIZE, page->pixels, flags );
	if( texture <= 0 )
		return false;

	page->texture = texture;
	page->dirty = false;
	return true;
}

static qboolean CL_HudTextUploadGlyph( hud_text_font_t *font, hud_text_glyph_t *glyph )
{
	byte *bitmap;
	int width = 0;
	int height = 0;
	int xOffset = 0;
	int yOffset = 0;
	qboolean placed;

	glyph->uploaded = true;
	bitmap = CL_HudTextRasterizeGlyph( font, glyph->ch,
		&width, &height, &xOffset, &yOffset );
	if( !bitmap )
		return false;

	placed = CL_HudTextPlaceGlyphInAtlas( glyph, bitmap, width, height );
	Mem_Free( bitmap );
	if( !placed )
	{
		Con_Printf( S_WARN "HUD text: atlas cache exhausted while adding U+%04X\n", glyph->ch );
		return false;
	}

	glyph->xOffset = xOffset;
	glyph->yOffset = yOffset;
	return true;
}

static qboolean CL_HudTextPrewarmAtlases( void )
{
	hud_text_glyph_t *glyph;
	int style;
	int ch;
	int i;

	for( style = HUD_TEXT_STYLE_SMALL; style <= HUD_TEXT_STYLE_LARGE; style++ )
	{
		for( ch = 32; ch <= 126; ch++ )
		{
			glyph = CL_HudTextFindGlyph( style, ch, true );
			if( glyph && ch != ' ' && !CL_HudTextUploadGlyph( &hudTextFonts[style], glyph ))
				return false;
		}
		for( ch = 160; ch <= 255; ch++ )
		{
			glyph = CL_HudTextFindGlyph( style, ch, true );
			if( glyph && ch != 160 && !CL_HudTextUploadGlyph( &hudTextFonts[style], glyph ))
				return false;
		}
	}

	for( i = 0; i < HUD_TEXT_MAX_ATLAS_PAGES; i++ )
	{
		if( hudTextAtlasPages[i].occupied && !CL_HudTextUploadAtlasPage( &hudTextAtlasPages[i] ))
			return false;
	}

	return true;
}

void CL_HudTextInfo_f( void )
{
	static const char *styleNames[HUD_TEXT_STYLE_COUNT] =
	{
		"default", "small", "medium", "large"
	};
	hud_text_font_t *font;
	int glyphCount = 0;
	int pageCount = 0;
	int i;

	for( i = 0; i < HUD_TEXT_GLYPH_CACHE_SIZE; i++ )
	{
		if( hudTextGlyphs[i].occupied )
			glyphCount++;
	}
	for( i = 0; i < HUD_TEXT_MAX_ATLAS_PAGES; i++ )
	{
		if( hudTextAtlasPages[i].occupied )
			pageCount++;
	}

	Con_Printf( "HUD TrueType: %s, ready: %s, atlas: %ix%i, padding: %ipx\n",
		hud_truetype && hud_truetype->value ? "enabled" : "disabled",
		hudTextReady ? "yes" : "no", HUD_TEXT_ATLAS_SIZE, HUD_TEXT_ATLAS_SIZE,
		HUD_TEXT_ATLAS_GLYPH_PAD );
	Con_Printf( "HUD text cache: %i glyphs, %i pages, %u per-glyph stb fallbacks, test overlay: %s\n",
		glyphCount, pageCount, hudTextStbFallbackGlyphs, hudTextTestVisible ? "on" : "off" );

	for( i = HUD_TEXT_STYLE_SMALL; i <= HUD_TEXT_STYLE_LARGE; i++ )
	{
		font = &hudTextFonts[i];
#if XASH_WIN32
		Con_Printf( "  %s: %s, design %i, raster %ipx, height %i, font %s%s%s\n",
			styleNames[i],
			font->backend == HUD_TEXT_BACKEND_WIN32_GDI ? "Win32 GDI hinted" : "stb_truetype",
			font->designSize, font->pixelSize, font->height, font->path,
			font->faceName[0] ? ", face " : "",
			font->faceName[0] ? font->faceName : "" );
#else
		Con_Printf( "  %s: stb_truetype, design %i, raster %ipx, height %i, font %s\n",
			styleNames[i], font->designSize, font->pixelSize, font->height, font->path );
#endif
	}
}

void CL_HudTextTest_f( void )
{
	hudTextTestVisible = !hudTextTestVisible;
	Con_Printf( "HUD text test overlay %s\n", hudTextTestVisible ? "enabled" : "disabled" );
}

static void CL_HudTextDrawTestLine( hud_text_style_t style, int x, int y,
	const char *text, int r, int g, int b )
{
	const unsigned char *cursor = (const unsigned char *)text;

	while( *cursor )
	{
		x += CL_HudTextDrawCharacter( style, x, y, *cursor, r, g, b, 255 );
		cursor++;
	}
}

void CL_HudTextDrawTest( void )
{
	int x = 64;
	int y = 72;
	int smallHeight;
	int mediumHeight;
	int largeHeight;
	int panelWidth;
	int panelHeight;

	if( !hudTextTestVisible || !hudTextReady || !hud_truetype || !hud_truetype->value )
		return;

	smallHeight = CL_HudTextGetHeight( HUD_TEXT_STYLE_SMALL );
	mediumHeight = CL_HudTextGetHeight( HUD_TEXT_STYLE_MEDIUM );
	largeHeight = CL_HudTextGetHeight( HUD_TEXT_STYLE_LARGE );
	panelWidth = Q_min( 1100, refState.width - 64 );
	panelHeight = smallHeight + mediumHeight + largeHeight + 72;

	ref.dllFuncs.FillRGBABlend((float)( x - 24 ), (float)( y - 24 ),
		(float)panelWidth, (float)panelHeight, 0, 0, 0, 210 );
	CL_HudTextDrawTestLine( HUD_TEXT_STYLE_SMALL, x, y,
		"Small 18 - independently tinted", 255, 196, 96 );
	y += smallHeight + 12;
	CL_HudTextDrawTestLine( HUD_TEXT_STYLE_MEDIUM, x, y,
		"Medium 28 - Win32 hinted", 130, 225, 160 );
	y += mediumHeight + 12;
	CL_HudTextDrawTestLine( HUD_TEXT_STYLE_LARGE, x, y,
		"Large 36 - padded atlas", 140, 190, 255 );
}

int CL_HudTextGetHeight( hud_text_style_t style )
{
	style = CL_HudTextNormalizeStyle( style );
	return hudTextFonts[style].ready ? hudTextFonts[style].height : 0;
}

int CL_HudTextGetCharWidth( hud_text_style_t style, int ch )
{
	hud_text_glyph_t *glyph = CL_HudTextFindGlyph( style, ch, true );
	return glyph ? glyph->advance : 0;
}

int CL_HudTextDrawCharacter( hud_text_style_t style, int x, int y, int ch, int r, int g, int b, int a )
{
	hud_text_font_t *font;
	hud_text_glyph_t *glyph;
	hud_text_atlas_page_t *page;
	float s1;
	float t1;
	float s2;
	float t2;

	style = CL_HudTextNormalizeStyle( style );
	font = &hudTextFonts[style];
	glyph = CL_HudTextFindGlyph( style, ch, true );
	if( !glyph )
		return 0;

	if( ch == ' ' || ch == '\t' )
		return glyph->advance;

	if( !glyph->uploaded && !CL_HudTextUploadGlyph( font, glyph ))
		return glyph->advance;

	if( glyph->atlasPage >= 0 && glyph->atlasPage < HUD_TEXT_MAX_ATLAS_PAGES )
	{
		page = &hudTextAtlasPages[glyph->atlasPage];
		if( !CL_HudTextUploadAtlasPage( page ))
			return glyph->advance;

		s1 = glyph->atlasX / (float)HUD_TEXT_ATLAS_SIZE;
		t1 = glyph->atlasY / (float)HUD_TEXT_ATLAS_SIZE;
		s2 = ( glyph->atlasX + glyph->width ) / (float)HUD_TEXT_ATLAS_SIZE;
		t2 = ( glyph->atlasY + glyph->height ) / (float)HUD_TEXT_ATLAS_SIZE;
		ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
		ref.dllFuncs.Color4ub( bound( 0, r, 255 ), bound( 0, g, 255 ),
			bound( 0, b, 255 ), bound( 0, a, 255 ));
		ref.dllFuncs.R_DrawStretchPic((float)( x + glyph->xOffset ),
			(float)( y + font->ascent + glyph->yOffset ),
			(float)glyph->width, (float)glyph->height, s1, t1, s2, t2, page->texture );
	}

	return glyph->advance;
}
