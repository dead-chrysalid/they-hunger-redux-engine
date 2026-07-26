/*
hud_text.c - engine-owned TrueType rendering for classic HUD text
*/
#include "common.h"
#include "filesystem.h"
#include "client.h"
#include "hud_text.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define HUD_TEXT_REFERENCE_HEIGHT 768.0f
#define HUD_TEXT_STYLE_COUNT 4
#define HUD_TEXT_GLYPH_CACHE_SIZE 2048
#define HUD_TEXT_MIN_SIZE 4
#define HUD_TEXT_MAX_SIZE 96

#define HUD_TEXT_DEFAULT_FONT "resource/fonts/Roboto-SemiBold.ttf"

#pragma pack( push, 1 )
typedef struct hud_text_bmp_s
{
	byte id[2];
	uint32_t fileSize;
	uint32_t reserved;
	uint32_t bitmapDataOffset;
	uint32_t bitmapHeaderSize;
	int32_t width;
	int32_t height;
	uint16_t planes;
	uint16_t bitsPerPixel;
	uint32_t compression;
	uint32_t bitmapDataSize;
	int32_t hRes;
	int32_t vRes;
	uint32_t colors;
	uint32_t importantColors;
} hud_text_bmp_t;
#pragma pack( pop )

typedef struct hud_text_font_s
{
	char path[MAX_SYSPATH];
	byte *data;
	stbtt_fontinfo info;
	float scale;
	int designSize;
	int pixelSize;
	int height;
	int ascent;
	qboolean ready;
} hud_text_font_t;

typedef struct hud_text_glyph_s
{
	qboolean occupied;
	int style;
	int ch;
	int texture;
	int advance;
	int xOffset;
	int yOffset;
	int width;
	int height;
} hud_text_glyph_t;

static hud_text_font_t hudTextFonts[HUD_TEXT_STYLE_COUNT];
static hud_text_glyph_t hudTextGlyphs[HUD_TEXT_GLYPH_CACHE_SIZE];
static unsigned int hudTextGeneration;
static qboolean hudTextReady;

static hud_text_style_t CL_HudTextNormalizeStyle( hud_text_style_t style )
{
	if( style == HUD_TEXT_STYLE_SMALL || style == HUD_TEXT_STYLE_LARGE )
		return style;
	return HUD_TEXT_STYLE_MEDIUM;
}

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
		for( i = 0; i < HUD_TEXT_GLYPH_CACHE_SIZE; i++ )
		{
			if( hudTextGlyphs[i].occupied && hudTextGlyphs[i].texture > 0 )
				ref.dllFuncs.GL_FreeTexture( hudTextGlyphs[i].texture );
		}
	}

	memset( hudTextGlyphs, 0, sizeof( hudTextGlyphs ));
}

static void CL_HudTextFreeFonts( void )
{
	int i;

	for( i = 0; i < HUD_TEXT_STYLE_COUNT; i++ )
	{
		if( hudTextFonts[i].data )
			Mem_Free( hudTextFonts[i].data );
		hudTextFonts[i].data = NULL;
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

	font->scale = stbtt_ScaleForPixelHeight( &font->info, (float)font->pixelSize );
	stbtt_GetFontVMetrics( &font->info, &ascent, &descent, NULL );
	font->ascent = (int)( ascent * font->scale + 0.5f );
	font->height = (int)(( ascent - descent ) * font->scale + 0.5f );
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

	if( !CL_HudTextLoadScheme() )
	{
		CL_HudTextLoadLegacyFallback();
		Con_DPrintf( "HUD text: fonts.lst not found, using legacy universal font fallback\n" );
	}

	for( i = HUD_TEXT_STYLE_SMALL; i <= HUD_TEXT_STYLE_LARGE; i++ )
	{
		if( !CL_HudTextLoadFont( &hudTextFonts[i] ))
			continue;
		Con_DPrintf( "HUD text: loaded %s at %ipx for style %i\n",
			hudTextFonts[i].path, hudTextFonts[i].pixelSize, i );
	}

	hudTextFonts[HUD_TEXT_STYLE_DEFAULT] = hudTextFonts[HUD_TEXT_STYLE_MEDIUM];
	hudTextFonts[HUD_TEXT_STYLE_DEFAULT].data = NULL;
	hudTextReady = hudTextFonts[HUD_TEXT_STYLE_SMALL].ready
		&& hudTextFonts[HUD_TEXT_STYLE_MEDIUM].ready
		&& hudTextFonts[HUD_TEXT_STYLE_LARGE].ready;
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

static hud_text_glyph_t *CL_HudTextFindGlyph( hud_text_style_t style, int ch, qboolean create )
{
	hud_text_font_t *font;
	hud_text_glyph_t *glyph;
	unsigned int index;
	unsigned int probe;
	int advance, bearing;

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
		glyph->texture = -1;
		stbtt_GetCodepointHMetrics( &font->info, ch, &advance, &bearing );
		glyph->advance = Q_max( 1, (int)( advance * font->scale + 0.5f ));
		return glyph;
	}

	return NULL;
}

static int CL_HudTextUploadGlyph( hud_text_font_t *font, hud_text_glyph_t *glyph )
{
	hud_text_bmp_t *bmp;
	byte *bitmap;
	byte *pixels;
	byte *file;
	char textureName[128];
	int width, height, xOffset, yOffset;
	int x, y;
	size_t dataSize;
	size_t fileSize;

	bitmap = stbtt_GetCodepointBitmap( &font->info, font->scale, font->scale,
		glyph->ch, &width, &height, &xOffset, &yOffset );
	if( !bitmap || width <= 0 || height <= 0 )
	{
		if( bitmap )
			stbtt_FreeBitmap( bitmap, NULL );
		glyph->texture = 0;
		return 0;
	}

	dataSize = (size_t)width * (size_t)height * 4;
	fileSize = sizeof( hud_text_bmp_t ) + dataSize;
	file = Mem_Calloc( cls.mempool, fileSize );
	bmp = (hud_text_bmp_t *)file;
	pixels = file + sizeof( hud_text_bmp_t );

	bmp->id[0] = 'B';
	bmp->id[1] = 'M';
	bmp->fileSize = (uint32_t)fileSize;
	bmp->bitmapDataOffset = sizeof( hud_text_bmp_t );
	bmp->bitmapHeaderSize = 40;
	bmp->width = width;
	bmp->height = height;
	bmp->planes = 1;
	bmp->bitsPerPixel = 32;
	bmp->bitmapDataSize = (uint32_t)dataSize;

	for( y = 0; y < height; y++ )
	{
		byte *dst = pixels + (size_t)( height - y - 1 ) * width * 4;
		byte *src = bitmap + (size_t)y * width;
		for( x = 0; x < width; x++ )
		{
			dst[x * 4 + 0] = 255;
			dst[x * 4 + 1] = 255;
			dst[x * 4 + 2] = 255;
			dst[x * 4 + 3] = src[x];
		}
	}

	Q_snprintf( textureName, sizeof( textureName ), "#hudtext_%u_%i_%x.bmp",
		hudTextGeneration, glyph->style, glyph->ch );
	glyph->texture = ref.dllFuncs.GL_LoadTexture( textureName, file, fileSize, TF_IMAGE | TF_NOMIPMAP );
	glyph->xOffset = xOffset;
	glyph->yOffset = yOffset;
	glyph->width = width;
	glyph->height = height;

	Mem_Free( file );
	stbtt_FreeBitmap( bitmap, NULL );
	return glyph->texture;
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

	style = CL_HudTextNormalizeStyle( style );
	font = &hudTextFonts[style];
	glyph = CL_HudTextFindGlyph( style, ch, true );
	if( !glyph )
		return 0;

	if( ch == ' ' || ch == '\t' )
		return glyph->advance;

	if( glyph->texture < 0 && !CL_HudTextUploadGlyph( font, glyph ))
		return glyph->advance;

	if( glyph->texture > 0 )
	{
		ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
		ref.dllFuncs.Color4ub( bound( 0, r, 255 ), bound( 0, g, 255 ),
			bound( 0, b, 255 ), bound( 0, a, 255 ));
		ref.dllFuncs.R_DrawStretchPic( x + glyph->xOffset, y + font->ascent + glyph->yOffset,
			glyph->width, glyph->height, 0, 0, 1, 1, glyph->texture );
	}

	return glyph->advance;
}
