/*
vid_sdl.c - SDL vid component
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// GL API function pointers, if any, reside in this translation unit
#define APIENTRY_LINKAGE
#include "gl_local.h"
#include "gl_export.h"

#ifdef XASH_GL4ES
#include "gl4es/include/gl4esinit.h"
#endif

ref_api_t      gEngfuncs;
ref_globals_t *gpGlobals;

static void R_ClearScreen( void )
{
	pglClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	pglClear( GL_COLOR_BUFFER_BIT );
}

static const byte *R_GetTextureOriginalBuffer( unsigned int idx )
{
	gl_texture_t *glt = R_GetTexture( idx );

	if( !glt || !glt->original || !glt->original->buffer )
		return NULL;

	return glt->original->buffer;
}

/*
=============
CL_FillRGBA

=============
*/
static void CL_FillRGBA( float _x, float _y, float _w, float _h, int r, int g, int b, int a )
{
	pglDisable( GL_TEXTURE_2D );
	pglEnable( GL_BLEND );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
	pglColor4f( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f );

	pglBegin( GL_QUADS );
		pglVertex2f( _x, _y );
		pglVertex2f( _x + _w, _y );
		pglVertex2f( _x + _w, _y + _h );
		pglVertex2f( _x, _y + _h );
	pglEnd ();

	pglColor3f( 1.0f, 1.0f, 1.0f );
	pglEnable( GL_TEXTURE_2D );
	pglDisable( GL_BLEND );
}

/*
=============
pfnFillRGBABlend

=============
*/
static void GAME_EXPORT CL_FillRGBABlend( float _x, float _y, float _w, float _h, int r, int g, int b, int a )
{
	pglDisable( GL_TEXTURE_2D );
	// The normal 2D setup keeps alpha testing enabled for sprites. RayTracedGL1
	// maps that state to an alpha-tested primitive, which discards the low-alpha
	// middle of procedural gradients and makes adjacent strips look detached.
	// Solid-color translucent quads must use blending only.
	pglDisable( GL_ALPHA_TEST );
	pglEnable( GL_BLEND );
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	pglColor4f( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f );

	pglBegin( GL_QUADS );
		pglVertex2f( _x, _y );
		pglVertex2f( _x + _w, _y );
		pglVertex2f( _x + _w, _y + _h );
		pglVertex2f( _x, _y + _h );
	pglEnd ();

	pglColor3f( 1.0f, 1.0f, 1.0f );
	pglEnable( GL_TEXTURE_2D );
	pglDisable( GL_BLEND );
	pglEnable( GL_ALPHA_TEST );
}

void Mod_BrushUnloadTextures( model_t *mod )
{
	int i;

	for( i = 0; i < mod->numtextures; i++ )
	{
		texture_t *tx = mod->textures[i];
		if( !tx || tx->gl_texturenum == tr.defaultTexture )
			continue; // free slot

		GL_FreeTexture( tx->gl_texturenum );    // main texture
		GL_FreeTexture( tx->fb_texturenum );    // luma texture
	}
}

void Mod_UnloadTextures( model_t *mod )
{
	Assert( mod != NULL );

	switch( mod->type )
	{
	case mod_studio:
		Mod_StudioUnloadTextures( mod->cache.data );
		break;
	case mod_alias:
		Mod_AliasUnloadTextures( mod->cache.data );
		break;
	case mod_brush:
		Mod_BrushUnloadTextures( mod );
		break;
	case mod_sprite:
		Mod_SpriteUnloadTextures( mod->cache.data );
		break;
	default:
		ASSERT( 0 );
		break;
	}
}

qboolean Mod_ProcessRenderData( model_t *mod, qboolean create, const byte *buf )
{
	qboolean loaded = true;

	if( create )
	{
		switch( mod->type )
		{
			case mod_studio:
				// Mod_LoadStudioModel( mod, buf, loaded );
				break;
			case mod_sprite:
				Mod_LoadSpriteModel( mod, buf, &loaded, mod->numtexinfo );
				break;
			case mod_alias:
				Mod_LoadAliasModel( mod, buf, &loaded );
				break;
			case mod_brush:
				// Mod_LoadBrushModel( mod, buf, loaded );
				break;
			default: gEngfuncs.Host_Error( "Mod_LoadModel: unsupported type %d\n", mod->type );
		}
	}

	if( loaded && gEngfuncs.drawFuncs->Mod_ProcessUserData )
		gEngfuncs.drawFuncs->Mod_ProcessUserData( mod, create, buf );

	if( !create )
		Mod_UnloadTextures( mod );

	return loaded;
}

static int GL_RefGetParm( int parm, int arg )
{
	gl_texture_t *glt;

	switch( parm )
	{
	case PARM_TEX_WIDTH:
		glt = R_GetTexture( arg );
		return glt->width;
	case PARM_TEX_HEIGHT:
		glt = R_GetTexture( arg );
		return glt->height;
	case PARM_TEX_SRC_WIDTH:
		glt = R_GetTexture( arg );
		return glt->srcWidth;
	case PARM_TEX_SRC_HEIGHT:
		glt = R_GetTexture( arg );
		return glt->srcHeight;
	case PARM_TEX_GLFORMAT:
		glt = R_GetTexture( arg );
		return glt->format;
	case PARM_TEX_ENCODE:
		glt = R_GetTexture( arg );
		return glt->encode;
	case PARM_TEX_MIPCOUNT:
		glt = R_GetTexture( arg );
		return glt->numMips;
	case PARM_TEX_DEPTH:
		glt = R_GetTexture( arg );
		return glt->depth;
	case PARM_TEX_SKYBOX:
		Assert( arg >= 0 && arg < 6 );
		return tr.skyboxTextures[arg];
	case PARM_TEX_SKYTEXNUM:
		return tr.skytexturenum;
	case PARM_TEX_LIGHTMAP:
		arg = bound( 0, arg, MAX_LIGHTMAPS - 1 );
		return tr.lightmapTextures[arg];
	case PARM_WIDESCREEN:
		return gpGlobals->wideScreen;
	case PARM_FULLSCREEN:
		return gpGlobals->fullScreen;
	case PARM_SCREEN_WIDTH:
		return gpGlobals->width;
	case PARM_SCREEN_HEIGHT:
		return gpGlobals->height;
	case PARM_TEX_TARGET:
		glt = R_GetTexture( arg );
		return glt->target;
	case PARM_TEX_TEXNUM:
		glt = R_GetTexture( arg );
		return glt->texnum;
	case PARM_TEX_FLAGS:
		glt = R_GetTexture( arg );
		return glt->flags;
	case PARM_ACTIVE_TMU:
		return glState.activeTMU;
	case PARM_LIGHTSTYLEVALUE:
		arg = bound( 0, arg, MAX_LIGHTSTYLES - 1 );
		return tr.lightstylevalue[arg];
	case PARM_MAX_IMAGE_UNITS:
		return GL_MaxTextureUnits();
	case PARM_REBUILD_GAMMA:
		return glConfig.softwareGammaUpdate;
	case PARM_SURF_SAMPLESIZE:
		if( arg >= 0 && arg < WORLDMODEL->numsurfaces )
			return gEngfuncs.Mod_SampleSizeForFace( &WORLDMODEL->surfaces[arg] );
		return LM_SAMPLE_SIZE;
	case PARM_GL_CONTEXT_TYPE:
		return glConfig.context;
	case PARM_GLES_WRAPPER:
		return glConfig.wrapper;
	case PARM_STENCIL_ACTIVE:
		return glState.stencilEnabled;
	case PARM_SKY_SPHERE:
		return ENGINE_GET_PARM_( parm, arg ) && !tr.fCustomSkybox;
	default:
		return ENGINE_GET_PARM_( parm, arg );
	}
	return 0;
}

static void R_GetDetailScaleForTexture( int texture, float *xScale, float *yScale )
{
	gl_texture_t *glt = R_GetTexture( texture );

	if( xScale ) *xScale = glt->xscale;
	if( yScale ) *yScale = glt->yscale;
}

static void R_GetExtraParmsForTexture( int texture, byte *red, byte *green, byte *blue, byte *density )
{
	gl_texture_t *glt = R_GetTexture( texture );

	if( red ) *red = glt->fogParams[0];
	if( green ) *green = glt->fogParams[1];
	if( blue ) *blue = glt->fogParams[2];
	if( density ) *density = glt->fogParams[3];
}


static void R_SetCurrentEntity( cl_entity_t *ent )
{
	RI.currententity = ent;

	// set model also
	if( RI.currententity != NULL )
	{
		RI.currentmodel = RI.currententity->model;
	}
}

static void R_SetCurrentModel( model_t *mod )
{
	RI.currentmodel = mod;
}

static float R_GetFrameTime( void )
{
	return tr.frametime;
}

static const char *GL_TextureName( unsigned int texnum )
{
	return R_GetTexture( texnum )->name;
}

const byte *GL_TextureData( unsigned int texnum )
{
	rgbdata_t *pic = R_GetTexture( texnum )->original;

	if( pic != NULL )
		return pic->buffer;
	return NULL;
}

void R_ProcessEntData( qboolean allocate )
{
	if( !allocate )
	{
		tr.draw_list->num_solid_entities = 0;
		tr.draw_list->num_trans_entities = 0;
		tr.draw_list->num_beam_entities = 0;
	}

	if( gEngfuncs.drawFuncs->R_ProcessEntData )
		gEngfuncs.drawFuncs->R_ProcessEntData( allocate );
}

qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int offset_x, int offset_y, float scale_x, float scale_y )
{
	qboolean ret = true;
	if( rotate > 0 )
	{
		gEngfuncs.Con_Printf("rotation transform not supported\n");
		ret = false;
	}

	if( offset_x || offset_y )
	{
		gEngfuncs.Con_Printf("offset transform not supported\n");
		ret = false;
	}

	if( scale_x != 1.0f || scale_y != 1.0f )
	{
		gEngfuncs.Con_Printf("scale transform not supported\n");
		ret = false;
	}

	return ret;
}

static void* GAME_EXPORT R_GetProcAddress( const char *name )
{
#ifdef XASH_GL4ES
	return gl4es_GetProcAddress( name );
#else // TODO: other wrappers
	return gEngfuncs.GL_GetProcAddress( name );
#endif
}

static const char *R_GetConfigName( void )
{
#if !XASH_RAYTRACING
	return "opengl";
#else
    return "rt";
#endif
}

#if XASH_RAYTRACING
static cvar_t *rt_editor_active;
static cvar_t *rt_editor_workspace;
static cvar_t *rt_editor_unsaved;
static cvar_t *rt_editor_map_serial;
static cvar_t *rt_editor_mode;

#define CVAR_BOOL( name, label, category, def ) \
	{ name, label, category, RG_EDITOR_CVAR_BOOL, def, 0.0f, 1.0f, 1.0f, NULL }
#define CVAR_INT( name, label, category, def, minv, maxv ) \
	{ name, label, category, RG_EDITOR_CVAR_INT, def, minv, maxv, 1.0f, "%d" }
#define CVAR_FLOAT( name, label, category, def, minv, maxv, drag, format ) \
	{ name, label, category, RG_EDITOR_CVAR_FLOAT, def, minv, maxv, drag, format }

static const RgEditorCvarDescriptor rt_editor_base_cvars[] =
{
	CVAR_INT( "hud_v6_toast_x", "Screen X", "HUD v6 / Toast Console", 8, 0, 1024 ),
	CVAR_INT( "hud_v6_toast_bottom", "Bottom offset", "HUD v6 / Toast Console", 216, 0, 1024 ),
	CVAR_INT( "hud_v6_toast_font_size", "Text font size", "HUD v6 / Toast Console", 28, 14, 64 ),
	CVAR_FLOAT( "hud_v6_toast_max_width_fraction", "Maximum screen width", "HUD v6 / Toast Console", 0.45f, 0.1f, 0.9f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_v6_toast_line_gap", "Line gap", "HUD v6 / Toast Console", 0, 0, 64 ),
	CVAR_INT( "hud_v6_toast_visible_rows", "Maximum simultaneous messages", "HUD v6 / Toast Console", 12, 1, 32 ),
	CVAR_FLOAT( "hud_v6_toast_character_time", "Seconds per character", "HUD v6 / Toast Console", 0.02f, 0.001f, 1.0f, 0.005f, "%.3f" ),
	CVAR_FLOAT( "hud_v6_toast_lifetime", "Visible lifetime", "HUD v6 / Toast Console", 8.0f, 0.05f, 120.0f, 0.05f, "%.2f s" ),

	CVAR_INT( "hud_v7_status_x", "Screen X", "HUD v7 / Status Widget", 16, 0, 1024 ),
	CVAR_INT( "hud_v7_status_bottom", "Bottom offset", "HUD v7 / Status Widget", 16, 0, 1024 ),
	CVAR_INT( "hud_v7_status_height", "Widget height", "HUD v7 / Status Widget", 96, 32, 512 ),
	CVAR_FLOAT( "hud_v7_status_scale", "Widget scale", "HUD v7 / Status Widget", 1.0f, 0.25f, 4.0f, 0.01f, "%.2f" ),

	CVAR_INT( "hud_v7_status_weapon_icon_x", "Weapon icon X", "HUD v7 / Status Weapon", 14, -256, 1024 ),
	CVAR_INT( "hud_v7_status_weapon_icon_y", "Weapon icon Y", "HUD v7 / Status Weapon", 10, -256, 512 ),
	CVAR_FLOAT( "hud_v7_status_weapon_icon_scale", "Weapon icon scale", "HUD v7 / Status Weapon", 0.54f, 0.01f, 4.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_v7_status_ammo_count_x", "Ammo count X", "HUD v7 / Status Ammo", 120, -256, 1024 ),
	CVAR_INT( "hud_v7_status_ammo_count_y", "Ammo count Y", "HUD v7 / Status Ammo", 20, -256, 512 ),
	CVAR_INT( "hud_v7_status_ammo_count_font_size", "Ammo count font size", "HUD v7 / Status Ammo", 28, 14, 64 ),
	CVAR_INT( "hud_v7_status_ammo_slash_x", "Slash X", "HUD v7 / Status Ammo", 105, -256, 1024 ),
	CVAR_INT( "hud_v7_status_ammo_slash_y", "Slash Y", "HUD v7 / Status Ammo", 46, -256, 512 ),
	CVAR_INT( "hud_v7_status_ammo_slash_font_size", "Slash font size", "HUD v7 / Status Ammo", 22, 14, 64 ),
	CVAR_INT( "hud_v7_status_ammo_name_x", "Ammo name X", "HUD v7 / Status Ammo", 122, -256, 1024 ),
	CVAR_INT( "hud_v7_status_ammo_name_y", "Ammo name Y", "HUD v7 / Status Ammo", 48, -256, 512 ),
	CVAR_INT( "hud_v7_status_ammo_name_font_size", "Ammo name font size", "HUD v7 / Status Ammo", 18, 14, 64 ),

	CVAR_INT( "hud_v7_status_health_x", "Health value X", "HUD v7 / Status Health", 12, -256, 1024 ),
	CVAR_INT( "hud_v7_status_health_y", "Health value Y", "HUD v7 / Status Health", 72, -256, 512 ),
	CVAR_INT( "hud_v7_status_health_font_size", "Health value font size", "HUD v7 / Status Health", 22, 14, 64 ),
	CVAR_INT( "hud_v7_status_bar_x", "Healthbar X", "HUD v7 / Status Health", 55, -256, 1024 ),
	CVAR_INT( "hud_v7_status_bar_y", "Healthbar Y", "HUD v7 / Status Health", 78, -256, 512 ),
	CVAR_FLOAT( "hud_v7_status_bar_scale", "Healthbar scale", "HUD v7 / Status Health", 0.18f, 0.01f, 4.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "hud_v7_status_hazard_cycle_time", "Hazard cycle time", "HUD v7 / Status Health", 0.5f, 0.05f, 10.0f, 0.05f, "%.2f s" ),
	CVAR_FLOAT( "hud_v7_status_hazard_blink_time", "Hazard blink time", "HUD v7 / Status Health", 0.25f, 0.02f, 10.0f, 0.01f, "%.2f s" ),
	CVAR_FLOAT( "hud_v7_status_hazard_lifetime", "Hazard lifetime", "HUD v7 / Status Health", 2.0f, 0.05f, 30.0f, 0.05f, "%.2f s" ),
	CVAR_INT( "hud_v7_status_hazard_icon_x", "Hazard icon X", "HUD v7 / Status Health", 12, -256, 1024 ),
	CVAR_INT( "hud_v7_status_hazard_icon_y", "Hazard icon Y", "HUD v7 / Status Health", 72, -256, 512 ),
	CVAR_FLOAT( "hud_v7_status_hazard_icon_scale", "Hazard icon scale", "HUD v7 / Status Health", 0.055f, 0.005f, 1.0f, 0.005f, "%.3f" ),

	CVAR_INT( "hud_v7_status_item_icon_x", "Health-kit icon X", "HUD v7 / Status Item", 218, -256, 1024 ),
	CVAR_INT( "hud_v7_status_item_icon_y", "Health-kit icon Y", "HUD v7 / Status Item", 69, -256, 512 ),
	CVAR_FLOAT( "hud_v7_status_item_icon_scale", "Health-kit icon scale", "HUD v7 / Status Item", 0.36f, 0.01f, 4.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_v7_status_item_count_x", "Syringe count X", "HUD v7 / Status Item", 244, -256, 1024 ),
	CVAR_INT( "hud_v7_status_item_count_y", "Syringe count Y", "HUD v7 / Status Item", 72, -256, 512 ),
	CVAR_INT( "hud_v7_status_item_count_font_size", "Syringe count font size", "HUD v7 / Status Item", 22, 14, 64 ),

	CVAR_INT( "hud_v7_weapon_x", "Screen X", "HUD v7 / Weapon Menu", 28, 0, 1024 ),
	CVAR_INT( "hud_v7_weapon_y", "Screen Y", "HUD v7 / Weapon Menu", 103, 0, 1024 ),
	CVAR_FLOAT( "hud_v7_weapon_scale", "Widget scale", "HUD v7 / Weapon Menu", 1.0f, 0.25f, 4.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_v7_weapon_bucket_x", "Bucket number X", "HUD v7 / Weapon Menu", 3, -256, 512 ),
	CVAR_INT( "hud_v7_weapon_bucket_y", "Bucket number Y", "HUD v7 / Weapon Menu", 0, -256, 512 ),
	CVAR_INT( "hud_v7_weapon_bucket_font_size", "Bucket number font size", "HUD v7 / Weapon Menu", 18, 14, 64 ),
	CVAR_INT( "hud_v7_weapon_cards_x", "Cards X", "HUD v7 / Weapon Cards", 0, -256, 512 ),
	CVAR_INT( "hud_v7_weapon_cards_y", "Cards Y", "HUD v7 / Weapon Cards", 16, -256, 512 ),
	CVAR_FLOAT( "hud_v7_weapon_card_scale", "Card and glow scale", "HUD v7 / Weapon Cards", 0.34f, 0.01f, 4.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_v7_weapon_card_gap", "Card gap", "HUD v7 / Weapon Cards", 8, 0, 256 ),
	CVAR_INT( "hud_v7_weapon_icon_x", "Icon X", "HUD v7 / Weapon Card Content", 8, -256, 512 ),
	CVAR_INT( "hud_v7_weapon_icon_y", "Icon Y", "HUD v7 / Weapon Card Content", 4, -256, 512 ),
	CVAR_FLOAT( "hud_v7_weapon_icon_scale", "Icon scale", "HUD v7 / Weapon Card Content", 0.27f, 0.01f, 4.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_v7_weapon_name_x", "Name X", "HUD v7 / Weapon Card Content", 7, -256, 512 ),
	CVAR_INT( "hud_v7_weapon_name_y", "Name Y", "HUD v7 / Weapon Card Content", 31, -256, 512 ),
	CVAR_INT( "hud_v7_weapon_name_font_size", "Name font size", "HUD v7 / Weapon Card Content", 14, 14, 64 ),
	CVAR_INT( "hud_v7_weapon_ammo_right", "Ammo right inset", "HUD v7 / Weapon Card Content", 7, -256, 512 ),
	CVAR_INT( "hud_v7_weapon_ammo_y", "Ammo Y", "HUD v7 / Weapon Card Content", 31, -256, 512 ),
	CVAR_INT( "hud_v7_weapon_ammo_font_size", "Ammo font size", "HUD v7 / Weapon Card Content", 14, 14, 64 ),
	CVAR_FLOAT( "hud_v7_weapon_selection_slide_speed", "Slide response", "HUD v7 / Weapon Selection", 18.0f, 1.0f, 60.0f, 0.1f, "%.1f" ),

	CVAR_BOOL( "hud_draw", "Draw HUD", "HUD / General", 1.0f ),
	CVAR_BOOL( "crosshair", "Draw crosshair", "HUD / General", 1.0f ),
	CVAR_BOOL( "hud_fastswitch", "Fast weapon switch", "HUD / General", 0.0f ),
	CVAR_FLOAT( "hud_deathnotice_time", "Death notice time", "HUD / General", 6.0f, 0.0f, 30.0f, 0.1f, "%.1f s" ),
	CVAR_INT( "hud_textmode", "HUD text mode", "HUD / General", 0, 0, 2 ),

	CVAR_BOOL( "hud_stamina_enabled", "Enabled", "HUD / Stamina", 1.0f ),
	CVAR_INT( "hud_stamina_x_offset", "Horizontal center offset", "HUD / Stamina", 0, -1024, 1024 ),
	CVAR_INT( "hud_stamina_bottom", "Bottom offset", "HUD / Stamina", 10, 0, 1024 ),
	CVAR_INT( "hud_stamina_width", "Width", "HUD / Stamina", 260, 16, 2048 ),
	CVAR_INT( "hud_stamina_thickness", "Thickness", "HUD / Stamina", 3, 1, 64 ),
	CVAR_FLOAT( "hud_stamina_smoothing", "Animation response", "HUD / Stamina", 14.0f, 0.1f, 60.0f, 0.1f, "%.1f" ),
	CVAR_FLOAT( "hud_stamina_fade_response", "Visibility fade response", "HUD / Stamina", 12.0f, 0.1f, 60.0f, 0.1f, "%.1f" ),
	CVAR_FLOAT( "hud_stamina_low_threshold", "Low stamina threshold fraction", "HUD / Stamina", 0.30f, 0.0f, 1.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_stamina_color_red", "Bar color red", "HUD / Stamina", 236, 0, 255 ),
	CVAR_INT( "hud_stamina_color_green", "Bar color green", "HUD / Stamina", 207, 0, 255 ),
	CVAR_INT( "hud_stamina_color_blue", "Bar color blue", "HUD / Stamina", 135, 0, 255 ),
	CVAR_INT( "hud_stamina_color_alpha", "Bar opacity", "HUD / Stamina", 230, 0, 255 ),
	CVAR_INT( "hud_stamina_background_red", "Background red", "HUD / Stamina", 8, 0, 255 ),
	CVAR_INT( "hud_stamina_background_green", "Background green", "HUD / Stamina", 8, 0, 255 ),
	CVAR_INT( "hud_stamina_background_blue", "Background blue", "HUD / Stamina", 10, 0, 255 ),
	CVAR_INT( "hud_stamina_background_alpha", "Background opacity", "HUD / Stamina", 150, 0, 255 ),
	CVAR_FLOAT( "hud_stamina_low_pulse_period", "Low pulse period", "HUD / Stamina", 0.85f, 0.05f, 10.0f, 0.05f, "%.2f s" ),
	CVAR_INT( "hud_stamina_low_pulse_min_alpha", "Low pulse minimum opacity", "HUD / Stamina", 105, 0, 255 ),
	CVAR_INT( "hud_stamina_depleted_color_red", "Depleted flash red", "HUD / Stamina", 225, 0, 255 ),
	CVAR_INT( "hud_stamina_depleted_color_green", "Depleted flash green", "HUD / Stamina", 28, 0, 255 ),
	CVAR_INT( "hud_stamina_depleted_color_blue", "Depleted flash blue", "HUD / Stamina", 24, 0, 255 ),
	CVAR_FLOAT( "hud_stamina_depleted_pulse_period", "Depleted flash period", "HUD / Stamina", 0.65f, 0.05f, 10.0f, 0.05f, "%.2f s" ),
	CVAR_INT( "hud_stamina_depleted_alpha_min", "Depleted minimum opacity", "HUD / Stamina", 55, 0, 255 ),
	CVAR_INT( "hud_stamina_depleted_alpha_max", "Depleted maximum opacity", "HUD / Stamina", 220, 0, 255 ),

	CVAR_BOOL( "sv_stamina_enabled", "Enabled", "Gameplay / Stamina", 1.0f ),
	CVAR_INT( "sv_stamina_max", "Maximum stamina", "Gameplay / Stamina", 1000, 1, 10000 ),
	CVAR_FLOAT( "sv_stamina_regen_rate", "Regeneration rate", "Gameplay / Stamina", 100.0f, 0.0f, 5000.0f, 1.0f, "%.0f / s" ),
	CVAR_FLOAT( "sv_stamina_regen_delay", "Regeneration delay", "Gameplay / Stamina", 0.7f, 0.0f, 30.0f, 0.05f, "%.2f s" ),
	CVAR_FLOAT( "sv_stamina_depleted_regen_delay", "Depleted regeneration delay", "Gameplay / Stamina", 2.0f, 0.0f, 30.0f, 0.05f, "%.2f s" ),
	CVAR_FLOAT( "sv_stamina_sprint_drain_rate", "Sprint drain rate", "Gameplay / Stamina", 60.0f, 0.0f, 5000.0f, 1.0f, "%.0f / s" ),
	CVAR_FLOAT( "sv_stamina_sprint_min_speed", "Sprint drain minimum speed", "Gameplay / Stamina", 120.0f, 0.0f, 1000.0f, 5.0f, "%.0f u/s" ),
	CVAR_FLOAT( "sv_stamina_sprint_resume_fraction", "Exhausted sprint recovery fraction", "Gameplay / Stamina", 0.03f, 0.0f, 1.0f, 0.01f, "%.2f" ),

	CVAR_FLOAT( "rt_flsh", "Intensity", "Lighting / Flashlight", 800.0f, 0.0f, 5000.0f, 10.0f, "%.0f" ),
	CVAR_FLOAT( "rt_flsh_radius", "Emitter radius", "Lighting / Flashlight", 0.02f, 0.0f, 0.50f, 0.005f, "%.3f m" ),
	CVAR_FLOAT( "rt_flsh_angle", "Outer cone", "Lighting / Flashlight", 20.0f, 1.0f, 89.0f, 0.25f, "%.2f deg" ),
	CVAR_FLOAT( "rt_flsh_angle_inner", "Inner cone", "Lighting / Flashlight", 0.0f, 0.0f, 89.0f, 0.25f, "%.2f deg" ),
	CVAR_FLOAT( "rt_flsh_r", "Right offset", "Lighting / Flashlight", -0.2f, -2.0f, 2.0f, 0.01f, "%.2f m" ),
	CVAR_FLOAT( "rt_flsh_u", "Up offset", "Lighting / Flashlight", -0.3f, -2.0f, 2.0f, 0.01f, "%.2f m" ),

	CVAR_BOOL( "sv_interact_enabled", "Enabled", "Gameplay / Interaction", 1.0f ),
	CVAR_BOOL( "sv_interact_pickups_require_use", "Pickups require interact", "Gameplay / Interaction", 1.0f ),
	CVAR_FLOAT( "sv_interact_outline_full_opacity_radius", "Item full-opacity distance", "Gameplay / Interaction", 200.0f, 0.0f, 2048.0f, 4.0f, "%.0f u" ),
	CVAR_FLOAT( "sv_interact_outline_radius", "Item zero-opacity distance", "Gameplay / Interaction", 240.0f, 0.0f, 2048.0f, 4.0f, "%.0f u" ),
	CVAR_FLOAT( "sv_interact_use_range", "Interaction range", "Gameplay / Interaction", 96.0f, 16.0f, 512.0f, 2.0f, "%.0f u" ),
	CVAR_FLOAT( "sv_interact_aim_dot", "Selection aim threshold", "Gameplay / Interaction", 0.55f, -1.0f, 1.0f, 0.01f, "%.2f" ),
	CVAR_BOOL( "sv_interact_hitbox_targeting", "Model hitbox targeting", "Gameplay / Interaction", 1.0f ),
	CVAR_FLOAT( "sv_interact_update_rate", "Network update rate", "Gameplay / Interaction", 12.0f, 1.0f, 60.0f, 1.0f, "%.0f Hz" ),
	CVAR_INT( "sv_interact_max_candidates", "Maximum outlined objects", "Gameplay / Interaction", 16, 1, 32 ),
	CVAR_BOOL( "sv_interact_require_los", "Interactions require line of sight", "Gameplay / Interaction", 1.0f ),
	CVAR_BOOL( "sv_interact_item_require_los", "Item outlines require engine LoS", "Gameplay / Interaction", 0.0f ),
	CVAR_BOOL( "sv_interact_outline_enemies", "Outline enemies", "Gameplay / Interaction", 0.0f ),
	CVAR_BOOL( "sv_interact_outline_friendlies", "Outline friendly NPCs", "Gameplay / Interaction", 0.0f ),
	CVAR_FLOAT( "sv_interact_enemy_full_opacity_radius", "Enemy full-opacity distance", "Gameplay / Interaction", 540.0f, 0.0f, 4096.0f, 8.0f, "%.0f u" ),
	CVAR_FLOAT( "sv_interact_enemy_radius", "Enemy zero-opacity distance", "Gameplay / Interaction", 640.0f, 0.0f, 4096.0f, 8.0f, "%.0f u" ),
	CVAR_BOOL( "sv_interact_enemy_require_los", "Enemy outlines require engine LoS", "Gameplay / Interaction", 0.0f ),
	CVAR_BOOL( "sv_interact_friendly_require_los", "Friendly outlines require engine LoS", "Gameplay / Interaction", 0.0f ),
	CVAR_BOOL( "sv_interact_debug", "Log targeting state", "Interaction / Diagnostics", 0.0f ),

	CVAR_BOOL( "hud_interact_prompt_enabled", "Prompt enabled", "HUD / Interaction Prompt", 1.0f ),
	CVAR_BOOL( "hud_interact_preview", "Preview prompt", "HUD / Interaction Prompt", 0.0f ),
	CVAR_BOOL( "hud_interact_debug", "Log client glow tags", "Interaction / Diagnostics", 0.0f ),
	CVAR_INT( "hud_interact_x_offset", "Horizontal center offset", "HUD / Interaction Prompt", 0, -1024, 1024 ),
	CVAR_INT( "hud_interact_y_offset", "Bottom offset", "HUD / Interaction Prompt", 145, -1024, 1024 ),
	CVAR_INT( "hud_interact_font_size", "Text font size", "HUD / Interaction Prompt", 22, 14, 64 ),
	CVAR_INT( "hud_interact_padding_x", "Horizontal padding", "HUD / Interaction Prompt", 12, 0, 128 ),
	CVAR_INT( "hud_interact_padding_y", "Vertical padding", "HUD / Interaction Prompt", 7, 0, 64 ),
	CVAR_INT( "hud_interact_key_width", "Key badge width", "HUD / Interaction Prompt", 34, 8, 128 ),
	CVAR_INT( "hud_interact_gap", "Key-to-label gap", "HUD / Interaction Prompt", 8, 0, 128 ),
	CVAR_FLOAT( "hud_interact_fade_response", "Fade response", "HUD / Interaction Prompt", 18.0f, 1.0f, 60.0f, 0.5f, "%.1f" ),
	CVAR_FLOAT( "hud_interact_pulse_period", "Selected pulse period", "HUD / Interaction Prompt", 1.10f, 0.05f, 10.0f, 0.05f, "%.2f s" ),
	CVAR_FLOAT( "hud_interact_pulse_amount", "Selected pulse strength", "HUD / Interaction Prompt", 0.08f, 0.0f, 1.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_interact_text_red", "Text red", "HUD / Interaction Prompt", 242, 0, 255 ),
	CVAR_INT( "hud_interact_text_green", "Text green", "HUD / Interaction Prompt", 242, 0, 255 ),
	CVAR_INT( "hud_interact_text_blue", "Text blue", "HUD / Interaction Prompt", 236, 0, 255 ),
	CVAR_INT( "hud_interact_accent_red", "Accent red", "HUD / Interaction Prompt", 239, 0, 255 ),
	CVAR_INT( "hud_interact_accent_green", "Accent green", "HUD / Interaction Prompt", 200, 0, 255 ),
	CVAR_INT( "hud_interact_accent_blue", "Accent blue", "HUD / Interaction Prompt", 88, 0, 255 ),
	CVAR_INT( "hud_interact_background_red", "Background red", "HUD / Interaction Prompt", 7, 0, 255 ),
	CVAR_INT( "hud_interact_background_green", "Background green", "HUD / Interaction Prompt", 8, 0, 255 ),
	CVAR_INT( "hud_interact_background_blue", "Background blue", "HUD / Interaction Prompt", 10, 0, 255 ),
	CVAR_INT( "hud_interact_background_alpha", "Background opacity", "HUD / Interaction Prompt", 190, 0, 255 ),
	CVAR_FLOAT( "hud_interact_state_timeout", "Network state timeout", "HUD / Interaction Prompt", 0.40f, 0.10f, 2.0f, 0.05f, "%.2f s" ),

	CVAR_BOOL( "rt_interact_outline_enabled", "Enabled", "Interaction / Outlines", 1.0f ),
	CVAR_FLOAT( "rt_interact_outline_width", "Nearby width", "Interaction / Outlines", 3.0f, 0.0f, 32.0f, 0.1f, "%.1f px" ),
	CVAR_FLOAT( "rt_interact_outline_selected_width", "Selected width", "Interaction / Outlines", 16.0f, 0.0f, 32.0f, 0.1f, "%.1f px" ),
	CVAR_FLOAT( "rt_interact_outline_core_width", "Nearby white-core width", "Interaction / Outlines", 1.25f, 0.1f, 16.0f, 0.05f, "%.2f px" ),
	CVAR_FLOAT( "rt_interact_outline_selected_core_width", "Selected white-core width", "Interaction / Outlines", 1.75f, 0.1f, 16.0f, 0.05f, "%.2f px" ),
	CVAR_FLOAT( "rt_interact_outline_core_intensity", "White-core intensity", "Interaction / Outlines", 1.40f, 0.0f, 8.0f, 0.05f, "%.2f" ),
	CVAR_FLOAT( "rt_interact_outline_halo_falloff", "Halo falloff", "Interaction / Outlines", 2.0f, 0.1f, 16.0f, 0.1f, "%.1f" ),
	CVAR_INT( "rt_interact_outline_core_r", "Core red", "Interaction / Outlines", 255, 0, 255 ),
	CVAR_INT( "rt_interact_outline_core_g", "Core green", "Interaction / Outlines", 255, 0, 255 ),
	CVAR_INT( "rt_interact_outline_core_b", "Core blue", "Interaction / Outlines", 255, 0, 255 ),
	CVAR_INT( "rt_interact_outline_diagnostic_view", "Diagnostic view", "Interaction / Diagnostics", 0, 0, 5 ),
	CVAR_FLOAT( "rt_interact_outline_opacity", "Nearby opacity", "Interaction / Outlines", 0.78f, 0.0f, 1.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "rt_interact_outline_selected_opacity", "Selected opacity", "Interaction / Outlines", 0.96f, 0.0f, 1.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "rt_interact_outline_pulse_period", "Pulse period", "Interaction / Outlines", 1.20f, 0.05f, 10.0f, 0.05f, "%.2f s" ),
	CVAR_FLOAT( "rt_interact_outline_pulse_amount", "Pulse strength", "Interaction / Outlines", 0.12f, 0.0f, 1.0f, 0.01f, "%.2f" ),
	CVAR_BOOL( "rt_interact_outline_items_xray", "Items visible through walls", "Interaction / Outlines", 0.0f ),
	CVAR_BOOL( "rt_interact_outline_enemies_xray", "Enemies visible through walls", "Interaction / Outlines", 0.0f ),
	CVAR_BOOL( "rt_interact_outline_friendlies_xray", "Friendly NPCs visible through walls", "Interaction / Outlines", 0.0f ),
	CVAR_INT( "rt_interact_outline_item_r", "Item red", "Interaction / Outlines", 96, 0, 255 ),
	CVAR_INT( "rt_interact_outline_item_g", "Item green", "Interaction / Outlines", 190, 0, 255 ),
	CVAR_INT( "rt_interact_outline_item_b", "Item blue", "Interaction / Outlines", 255, 0, 255 ),
	CVAR_INT( "rt_interact_outline_use_r", "Usable red", "Interaction / Outlines", 245, 0, 255 ),
	CVAR_INT( "rt_interact_outline_use_g", "Usable green", "Interaction / Outlines", 199, 0, 255 ),
	CVAR_INT( "rt_interact_outline_use_b", "Usable blue", "Interaction / Outlines", 82, 0, 255 ),
	CVAR_INT( "rt_interact_outline_medkit_r", "Medical red", "Interaction / Outlines", 100, 0, 255 ),
	CVAR_INT( "rt_interact_outline_medkit_g", "Medical green", "Interaction / Outlines", 235, 0, 255 ),
	CVAR_INT( "rt_interact_outline_medkit_b", "Medical blue", "Interaction / Outlines", 145, 0, 255 ),
	CVAR_INT( "rt_interact_outline_enemy_r", "Enemy red", "Interaction / Outlines", 255, 0, 255 ),
	CVAR_INT( "rt_interact_outline_enemy_g", "Enemy green", "Interaction / Outlines", 80, 0, 255 ),
	CVAR_INT( "rt_interact_outline_enemy_b", "Enemy blue", "Interaction / Outlines", 70, 0, 255 ),
	CVAR_INT( "rt_interact_outline_friendly_r", "Friendly NPC red", "Interaction / Outlines", 92, 0, 255 ),
	CVAR_INT( "rt_interact_outline_friendly_g", "Friendly NPC green", "Interaction / Outlines", 230, 0, 255 ),
	CVAR_INT( "rt_interact_outline_friendly_b", "Friendly NPC blue", "Interaction / Outlines", 128, 0, 255 ),
	CVAR_INT( "rt_interact_outline_selected_r", "Selected red", "Interaction / Outlines", 255, 0, 255 ),
	CVAR_INT( "rt_interact_outline_selected_g", "Selected green", "Interaction / Outlines", 238, 0, 255 ),
	CVAR_INT( "rt_interact_outline_selected_b", "Selected blue", "Interaction / Outlines", 174, 0, 255 ),
	CVAR_BOOL( "rt_interact_outline_debug", "Log renderer submissions", "Interaction / Diagnostics", 0.0f ),

	CVAR_INT( "hud_crosshair_bar_min_length", "Minimum bar length", "Crosshair", 4, 1, 128 ),
	CVAR_INT( "hud_crosshair_bar_max_length", "Maximum bar length", "Crosshair", 14, 1, 256 ),
	CVAR_INT( "hud_crosshair_bar_thickness", "Bar thickness", "Crosshair", 1, 1, 64 ),
	CVAR_INT( "hud_crosshair_bar_opacity", "Bar opacity", "Crosshair", 128, 0, 255 ),
	CVAR_INT( "hud_crosshair_dot_gap_threshold", "Dot gap threshold", "Crosshair", 2, 0, 128 ),
	CVAR_INT( "hud_crosshair_dot_size", "Dot size", "Crosshair", 2, 1, 64 ),
	CVAR_INT( "hud_crosshair_color_red", "Color red", "Crosshair", 255, 0, 255 ),
	CVAR_INT( "hud_crosshair_color_green", "Color green", "Crosshair", 160, 0, 255 ),
	CVAR_INT( "hud_crosshair_color_blue", "Color blue", "Crosshair", 0, 0, 255 ),
	CVAR_FLOAT( "hud_scope_crosshair_scale", "Scope scale", "Crosshair", 1.0f, 0.1f, 4.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_scope_crosshair_size", "Explicit scope size", "Crosshair", 0, 0, 4096 ),

	CVAR_BOOL( "hit_markers", "Enabled", "Hitmarkers", 1.0f ),
	CVAR_INT( "hit_markers_simple_length", "Length", "Hitmarkers", 14, 1, 128 ),
	CVAR_INT( "hit_markers_gap", "Gap", "Hitmarkers", 7, 0, 128 ),
	CVAR_FLOAT( "hit_markers_opacity", "Opacity", "Hitmarkers", 1.0f, 0.0f, 1.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hit_markers_body_color_red", "Body color red", "Hitmarkers", 246, 0, 255 ),
	CVAR_INT( "hit_markers_body_color_green", "Body color green", "Hitmarkers", 0, 0, 255 ),
	CVAR_INT( "hit_markers_body_color_blue", "Body color blue", "Hitmarkers", 0, 0, 255 ),
	CVAR_INT( "hit_markers_head_color_red", "Head color red", "Hitmarkers", 255, 0, 255 ),
	CVAR_INT( "hit_markers_head_color_green", "Head color green", "Hitmarkers", 153, 0, 255 ),
	CVAR_INT( "hit_markers_head_color_blue", "Head color blue", "Hitmarkers", 0, 0, 255 ),
	CVAR_FLOAT( "hit_markers_start_scale", "Start scale", "Hitmarkers", 0.8f, 0.0f, 4.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "hit_markers_peak_scale", "Peak scale", "Hitmarkers", 1.0f, 0.0f, 4.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "hit_markers_end_scale", "End scale", "Hitmarkers", 0.7f, 0.0f, 4.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "hit_markers_fade_in_time", "Fade-in time", "Hitmarkers", 0.025f, 0.0f, 2.0f, 0.005f, "%.3f s" ),
	CVAR_FLOAT( "hit_markers_pop_time", "Pop time", "Hitmarkers", 0.05f, 0.0f, 2.0f, 0.005f, "%.3f s" ),
	CVAR_FLOAT( "hit_markers_fade_out_time", "Fade-out time", "Hitmarkers", 0.28f, 0.0f, 2.0f, 0.005f, "%.3f s" ),

	CVAR_BOOL( "killfx_enable", "Enabled", "Kill Effects", 1.0f ),
	CVAR_BOOL( "killfx_human_head_gibs", "Flying human skulls on headshots", "Kill Effects", 1.0f ),
	CVAR_BOOL( "killfx_sound", "Sound enabled", "Kill Effects", 1.0f ),
	CVAR_FLOAT( "killfx_sound_volume", "Sound volume", "Kill Effects", 1.0f, 0.0f, 4.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "killfx_sound_boost", "Sound boost", "Kill Effects", 3.2f, 0.0f, 8.0f, 0.05f, "%.2f" ),
	CVAR_FLOAT( "killfx_sound_delay", "Sound delay", "Kill Effects", 0.05f, 0.0f, 2.0f, 0.005f, "%.3f s" ),
	CVAR_BOOL( "killfx_vignette", "Vignette enabled", "Kill Effects", 1.0f ),
	CVAR_INT( "killfx_vignette_alpha", "Vignette opacity", "Kill Effects", 52, 0, 255 ),
	CVAR_FLOAT( "killfx_vignette_size", "Vignette size", "Kill Effects", 0.18f, 0.02f, 0.5f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "killfx_fade_in_time", "Fade-in time", "Kill Effects", 0.025f, 0.0f, 3.0f, 0.005f, "%.3f s" ),
	CVAR_FLOAT( "killfx_hold_time", "Hold time", "Kill Effects", 0.04f, 0.0f, 3.0f, 0.005f, "%.3f s" ),
	CVAR_FLOAT( "killfx_fade_out_time", "Fade-out time", "Kill Effects", 0.32f, 0.0f, 5.0f, 0.005f, "%.3f s" ),
	CVAR_BOOL( "killfx_distortion", "Distortion enabled", "Kill Effects", 1.0f ),
};

#define RT_EDITOR_MAX_CVARS 384
#define RT_EDITOR_SPREAD_WEAPONS 10
#define RT_EDITOR_SPREAD_FIELDS 10
static RgEditorCvarDescriptor rt_editor_cvars[RT_EDITOR_MAX_CVARS];
static char rt_editor_spread_names[RT_EDITOR_SPREAD_WEAPONS][RT_EDITOR_SPREAD_FIELDS][64];
static char rt_editor_spread_labels[RT_EDITOR_SPREAD_WEAPONS][RT_EDITOR_SPREAD_FIELDS][64];
static char rt_editor_spread_categories[RT_EDITOR_SPREAD_WEAPONS][64];
static uint32_t rt_editor_cvar_count;

static RgBool32 RGCONV RT_EditorCvarGet( void *userdata, const char *name, float *value )
{
	cvar_t *cvar;
	(void)userdata;
	if( !name || !value ) return 0;
	cvar = gEngfuncs.pfnGetCvarPointer( name, 0 );
	if( !cvar ) return 0;
	*value = cvar->value;
	return 1;
}

static RgBool32 RGCONV RT_EditorCvarSet( void *userdata, const char *name, float value )
{
	(void)userdata;
	if( !name || !gEngfuncs.pfnGetCvarPointer( name, 0 )) return 0;
	gEngfuncs.Cvar_SetValue( name, value );
	return 1;
}

static void RGCONV RT_EditorCvarCommit( void *userdata )
{
	uint32_t i;
	(void)userdata;
	gEngfuncs.Cbuf_AddText( "_cvar_editor_begin\n" );
	for( i = 0; i < rt_editor_cvar_count; i++ )
	{
		const char *name = rt_editor_cvars[i].pName;
		if( !name || !gEngfuncs.pfnGetCvarPointer( name, 0 )) continue;
		gEngfuncs.Cbuf_AddText( va( "_cvar_editor_track \"%s\"\n", name ));
	}
	gEngfuncs.Cbuf_AddText( "_cvar_editor_commit\n" );
}

static void RT_EditorBuildCvarList( void )
{
	static const char *weaponNames[RT_EDITOR_SPREAD_WEAPONS] =
		{ "glock", "python", "mp5", "crossbow", "shotgun",
		  "ap9", "sniper", "tfcsniper", "taurus", "chaingun" };
	static const char *weaponLabels[RT_EDITOR_SPREAD_WEAPONS] =
		{ "Glock", "Python", "MP5", "Crossbow", "Shotgun",
		  "AP9", "Sniper", "TFC Sniper", "Taurus", "Chaingun" };
	static const char *suffixes[RT_EDITOR_SPREAD_FIELDS] =
		{ "min", "max", "focus", "shot_add", "sprint_max_add_frac",
		  "ads_min_add", "ads_max_add", "ads_focus_add", "ads_shot_add", "enabled" };
	static const char *labels[RT_EDITOR_SPREAD_FIELDS] =
		{ "Minimum spread", "Maximum spread", "Focus rate", "Shot spread add",
		  "Sprint max add fraction", "ADS minimum add", "ADS maximum add",
		  "ADS focus add", "ADS shot add", "Enabled" };
	static const float defaults[RT_EDITOR_SPREAD_WEAPONS][RT_EDITOR_SPREAD_FIELDS] =
	{
		{ .020f, .080f, .100f, .020f, .5f, -.010f, .0f, .0f, .015f, 1.0f },
		{ .010f, .100f, .070f, .070f, .5f, -.010f, .0f, .060f, .0f, 1.0f },
		{ .015f, .050f, .100f, .015f, .5f, -.005f, .0f, .0f, .0f, 1.0f },
		{ .0f, .0f, .0f, .0f, .5f, .0f, .0f, .0f, .0f, 1.0f },
		{ .050f, .150f, .100f, .100f, .5f, .0f, -.050f, .0f, -.030f, 1.0f },
		{ .020f, .100f, .100f, .012f, .5f, -.010f, .0f, .0f, .003f, 1.0f },
		{ .015f, .060f, .070f, .045f, .5f, -.015f, -.060f, 1.0f, -.045f, 1.0f },
		{ .010f, .080f, .070f, .060f, .5f, -.010f, -.080f, 1.0f, -.060f, 1.0f },
		{ .015f, .080f, .070f, .020f, .5f, -.010f, .0f, .030f, .015f, 1.0f },
		{ .040f, .070f, .070f, .013f, .5f, .0f, .0f, .0f, .0f, 1.0f },
	};
	uint32_t i;
	rt_editor_cvar_count = ARRAYSIZE( rt_editor_base_cvars );
	memcpy( rt_editor_cvars, rt_editor_base_cvars, sizeof( rt_editor_base_cvars ));

	for( i = 0; i < RT_EDITOR_SPREAD_WEAPONS; i++ )
	{
		uint32_t field;
		Q_snprintf( rt_editor_spread_categories[i], sizeof( rt_editor_spread_categories[i] ),
			"Crosshair Spread / %s", weaponLabels[i] );
		for( field = 0; field < RT_EDITOR_SPREAD_FIELDS &&
			rt_editor_cvar_count < RT_EDITOR_MAX_CVARS; field++ )
		{
			RgEditorCvarDescriptor *descriptor = &rt_editor_cvars[rt_editor_cvar_count++];
			Q_snprintf( rt_editor_spread_names[i][field],
				sizeof( rt_editor_spread_names[i][field] ),
				"cl_spread_%s_%s", weaponNames[i], suffixes[field] );
			Q_strncpy( rt_editor_spread_labels[i][field], labels[field],
				sizeof( rt_editor_spread_labels[i][field] ));
			descriptor->pName = rt_editor_spread_names[i][field];
			descriptor->pLabel = rt_editor_spread_labels[i][field];
			descriptor->pCategory = rt_editor_spread_categories[i];
			descriptor->type = field == RT_EDITOR_SPREAD_FIELDS - 1
				? RG_EDITOR_CVAR_BOOL : RG_EDITOR_CVAR_FLOAT;
			descriptor->defaultValue = defaults[i][field];
			descriptor->minimum = field >= 5 && field <= 8 ? -2.0f : 0.0f;
			descriptor->maximum = field == 4 ? 4.0f : ( field == 9 ? 1.0f : 2.0f );
			descriptor->step = field == 4 ? 0.01f : 0.001f;
			descriptor->pFormat = field == 4 ? "%.2f" : "%.3f";
		}
	}
}

#undef CVAR_BOOL
#undef CVAR_INT
#undef CVAR_FLOAT

static void RT_EditorSyncCvars( const RgEditorStatus *status )
{
	if( !status ) return;
	if( rt_editor_active ) gEngfuncs.Cvar_SetValue( "rt_editor_active", status->active ? 1.0f : 0.0f );
	if( rt_editor_workspace ) gEngfuncs.Cvar_SetValue( "rt_editor_workspace", status->workspaceOpen ? 1.0f : 0.0f );
	if( rt_editor_unsaved ) gEngfuncs.Cvar_SetValue( "rt_editor_unsaved", status->hasUnsavedChanges ? 1.0f : 0.0f );
	if( rt_editor_map_serial ) gEngfuncs.Cvar_SetValue( "rt_editor_map_serial", (float)status->mapSerial );
}

static void RT_EditorGetStatusImpl( ref_editor_status_t *status )
{
	RgEditorStatus rgstatus = { 0 };
	if( !status ) return;
	memset( status, 0, sizeof( *status ));
	if( !rg_instance || rgEditorGetStatus( rg_instance, &rgstatus ) != RG_RESULT_SUCCESS ) return;
	status->available = rgstatus.available;
	status->active = rgstatus.active;
	status->workspaceOpen = rgstatus.workspaceOpen;
	status->wantsInput = rgstatus.wantsInput;
	status->wantsCursor = rgstatus.wantsCursor;
	status->hasUnsavedChanges = rgstatus.hasUnsavedChanges;
	status->mapSerial = rgstatus.mapSerial;
	Q_strncpy( status->mapName, rgstatus.mapName, sizeof( status->mapName ) );
	Q_strncpy( status->message, rgstatus.message, sizeof( status->message ) );
	RT_EditorSyncCvars( &rgstatus );
}

static qboolean RT_EditorSetEnabledImpl( qboolean enabled )
{
	RgEditorStatus status = { 0 };
	RgEditorMode mode = RG_EDITOR_MODE_MAP;
	if( !rg_instance ) return false;
	if( enabled )
	{
		if( gEngfuncs.pfnGetCvarFloat( "developer" ) <= 0.0f )
		{
			gEngfuncs.Con_Printf( "RT editor requires developer mode.\n" );
			return false;
		}
		if( gEngfuncs.EngineGetParm( PARM_CONNSTATE, 0 ) != ca_active ||
			gEngfuncs.EngineGetParm( PARM_PLAYING_DEMO, 0 ) ||
			gEngfuncs.EngineGetParm( PARM_MAX_CLIENTS, 0 ) != 1 ||
			!gEngfuncs.EngineGetParm( PARM_LOCAL_GAME, 0 ) )
		{
			gEngfuncs.Con_Printf( "RT editor is available only in an active local single-player map.\n" );
			return false;
		}
	}
	if( rt_editor_mode && rt_editor_mode->value >= 1.5f )
		mode = RG_EDITOR_MODE_CVARS;
	if( rgEditorSetMode( rg_instance, mode ) != RG_RESULT_SUCCESS ) return false;
	if( rgEditorSetEnabled( rg_instance, enabled ) != RG_RESULT_SUCCESS ) return false;
	rgEditorGetStatus( rg_instance, &status );
	RT_EditorSyncCvars( &status );
	gEngfuncs.Con_Printf( "%s\n",
		enabled
			? ( mode == RG_EDITOR_MODE_CVARS ? "Cvar Editor enabled" : "RT editor enabled" )
			: "Editor disabled" );
	return true;
}

static qboolean RT_EditorHandleInputImpl( const ref_editor_event_t *event )
{
	RgEditorInputEvent input = { 0 };
	if( !event || !rg_instance ) return false;
	input.type = (RgEditorInputType)event->type;
	input.key = event->key;
	input.down = event->down;
	input.button = event->button;
	input.modifiers = event->modifiers;
	input.x = event->x;
	input.y = event->y;
	input.dx = event->dx;
	input.dy = event->dy;
	input.wheel = event->wheel;
	Q_strncpy( input.text, event->text, sizeof( input.text ) );
	return rgEditorSubmitInput( rg_instance, &input ) != 0;
}

static void RT_EditorToggleMode( RgEditorMode requestedMode )
{
	ref_editor_status_t status;
	const RgEditorMode currentMode =
		rt_editor_mode && rt_editor_mode->value >= 1.5f
			? RG_EDITOR_MODE_CVARS : RG_EDITOR_MODE_MAP;
	RT_EditorGetStatusImpl( &status );
	if( status.active && currentMode == requestedMode )
	{
		RT_EditorSetEnabledImpl( false );
		return;
	}
	gEngfuncs.Cvar_SetValue(
		"rt_editor_mode",
		requestedMode == RG_EDITOR_MODE_CVARS ? 2.0f : 1.0f );
	RT_EditorSetEnabledImpl( true );
}

static void RT_EditorToggle_f( void )
{
	RT_EditorToggleMode( RG_EDITOR_MODE_MAP );
}

static void RT_CvarEditorToggle_f( void )
{
	RT_EditorToggleMode( RG_EDITOR_MODE_CVARS );
}

static void RT_CvarEditorExport_f( void )
{
	RT_EditorCvarCommit( NULL );
}

static void RT_EditorRetrySave_f( void )
{
	if( rg_instance ) rgEditorRetrySave( rg_instance );
}

void RT_EditorInit( void )
{
	RgEditorCvarHost cvarHost = { 0 };
	// These are renderer-owned synchronization cvars, but they cannot use
	// FCVAR_READ_ONLY: the public renderer API deliberately cannot mutate a
	// read-only cvar and the client DLL must be able to observe their values.
	rt_editor_active = gEngfuncs.Cvar_Get( "rt_editor_active", "0", FCVAR_SPONLY|FCVAR_UNLOGGED, "RT live editor active state" );
	rt_editor_workspace = gEngfuncs.Cvar_Get( "rt_editor_workspace", "0", FCVAR_SPONLY|FCVAR_UNLOGGED, "RT live editor cursor workspace state" );
	rt_editor_unsaved = gEngfuncs.Cvar_Get( "rt_editor_unsaved", "0", FCVAR_SPONLY|FCVAR_UNLOGGED, "RT live editor has unsaved changes" );
	rt_editor_map_serial = gEngfuncs.Cvar_Get( "rt_editor_map_serial", "0", FCVAR_SPONLY|FCVAR_UNLOGGED, "RT live editor map generation" );
	rt_editor_mode = gEngfuncs.Cvar_Get( "rt_editor_mode", "1", FCVAR_SPONLY|FCVAR_UNLOGGED, "Active developer editor mode: 1 map, 2 cvars" );
	gEngfuncs.Cvar_Get( "rt_editor_speed", "500", FCVAR_ARCHIVE, "RT editor noclip speed in game units per second" );
	gEngfuncs.Cmd_AddCommand( "rt_editor_toggle", RT_EditorToggle_f, "Toggle the developer RT live editor" );
	gEngfuncs.Cmd_AddCommand( "rt_cvar_editor_toggle", RT_CvarEditorToggle_f, "Toggle the developer Cvar Editor" );
	gEngfuncs.Cmd_AddCommand( "rt_cvar_editor_export", RT_CvarEditorExport_f, "Export every Cvar Editor value to release content" );
	gEngfuncs.Cmd_AddCommand( "rt_editor_retry_save", RT_EditorRetrySave_f, "Retry pending RT editor saves" );

	RT_EditorBuildCvarList();
	cvarHost.pDescriptors = rt_editor_cvars;
	cvarHost.descriptorCount = rt_editor_cvar_count;
	cvarHost.pfnGetValue = RT_EditorCvarGet;
	cvarHost.pfnSetValue = RT_EditorCvarSet;
	cvarHost.pfnCommit = RT_EditorCvarCommit;
	if( rg_instance )
		rgEditorSetCvarHost( rg_instance, &cvarHost );
}

void RT_EditorShutdown( void )
{
	if( rg_instance )
	{
		rgEditorSetEnabled( rg_instance, false );
		rgEditorSetCvarHost( rg_instance, NULL );
	}
	gEngfuncs.Cmd_RemoveCommand( "rt_editor_toggle" );
	gEngfuncs.Cmd_RemoveCommand( "rt_cvar_editor_toggle" );
	gEngfuncs.Cmd_RemoveCommand( "rt_cvar_editor_export" );
	gEngfuncs.Cmd_RemoveCommand( "rt_editor_retry_save" );
	rt_editor_active = rt_editor_workspace = rt_editor_unsaved =
		rt_editor_map_serial = rt_editor_mode = NULL;
}
#else
void RT_EditorInit( void ) {}
void RT_EditorShutdown( void ) {}
#define RT_EditorSetEnabledImpl NULL
#define RT_EditorHandleInputImpl NULL
#define RT_EditorGetStatusImpl NULL
#endif

ref_interface_t gReffuncs =
{
	R_Init,
	R_Shutdown,
	R_GetConfigName,
	R_SetDisplayTransform,

	GL_SetupAttributes,
	GL_InitExtensions,
	GL_ClearExtensions,

	R_BeginFrame,
	R_RenderScene,
	R_EndFrame,
	R_PushScene,
	R_PopScene,
	GL_BackendStartFrame,
	GL_BackendEndFrame,

	R_ClearScreen,
	R_AllowFog,
	GL_SetRenderMode,

	R_AddEntity,
	CL_AddCustomBeam,
	R_ProcessEntData,

	R_ShowTextures,

	R_GetTextureOriginalBuffer,
	GL_LoadTextureFromBuffer,
	GL_ProcessTexture,
	R_SetupSky,

	R_Set2DMode,
	R_DrawStretchRaw,
	R_DrawStretchPic,
	R_DrawStretchPicEx,
	R_DrawTileClear,
	CL_FillRGBA,
	CL_FillRGBABlend,
	R_WorldToScreen,

	VID_ScreenShot,
	VID_CubemapShot,

	R_LightPoint,

	R_DecalShoot,
	R_DecalRemoveAll,
	R_CreateDecalList,
	R_ClearAllDecals,

	R_StudioEstimateFrame,
	R_StudioLerpMovement,
	CL_InitStudioAPI,

	R_InitSkyClouds,
	GL_SubdivideSurface,
	CL_RunLightStyles,

	R_GetSpriteParms,
	R_GetSpriteTexture,

	Mod_LoadMapSprite,
	Mod_ProcessRenderData,
	Mod_StudioLoadTextures,

	CL_DrawParticles,
	CL_DrawTracers,
	CL_DrawBeams,
	R_BeamCull,

	GL_RefGetParm,
	R_GetDetailScaleForTexture,
	R_GetExtraParmsForTexture,
	R_GetFrameTime,

	R_SetCurrentEntity,
	R_SetCurrentModel,

	GL_FindTexture,
	GL_TextureName,
	GL_TextureData,
	GL_LoadTexture,
	GL_CreateTexture,
	GL_LoadTextureArray,
	GL_CreateTextureArray,
	GL_FreeTexture,

	DrawSingleDecal,
	R_DecalSetupVerts,
	R_EntityRemoveDecals,

	R_UploadStretchRaw,

	GL_Bind,
	GL_SelectTexture,
	GL_LoadTexMatrixExt,
	GL_LoadIdentityTexMatrix,
	GL_CleanUpTextureUnits,
	GL_TexGen,
	GL_TextureTarget,
	GL_SetTexCoordArrayMode,
	GL_UpdateTexSize,
	NULL,
	NULL,

	CL_DrawParticlesExternal,
	R_LightVec,
	R_StudioGetTexture,

	R_RenderFrame,
	Mod_SetOrthoBounds,
	R_SpeedsMessage,
	Mod_GetCurrentVis,
	R_NewMap,
	R_ClearScene,
	R_GetProcAddress,

	TriRenderMode,
	TriBegin,
	TriEnd,
	_TriColor4f,
	_TriColor4ub,
	TriTexCoord2f,
	TriVertex3fv,
	TriVertex3f,
	TriFog,
	R_ScreenToWorld,
	TriGetMatrix,
	TriFogParams,
	TriCullFace,

	VGUI_DrawInit,
	VGUI_DrawShutdown,
	VGUI_SetupDrawingText,
	VGUI_SetupDrawingRect,
	VGUI_SetupDrawingImage,
	VGUI_BindTexture,
	VGUI_EnableTexture,
	VGUI_CreateTexture,
	VGUI_UploadTexture,
	VGUI_UploadTextureBlock,
	VGUI_DrawQuad,
	VGUI_GetTextureSizes,
	VGUI_GenerateTexture,

	RT_EditorSetEnabledImpl,
	RT_EditorHandleInputImpl,
	RT_EditorGetStatusImpl,
};

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	// fill in our callbacks
	memcpy( funcs, &gReffuncs, sizeof( ref_interface_t ));
	memcpy( &gEngfuncs, engfuncs, sizeof( ref_api_t ));
	gpGlobals = globals;

	return REF_API_VERSION;
}

void EXPORT GetRefHumanReadableName( char *out, size_t size )
{
#if defined XASH_NANOGL
	Q_strncpy( out, "GLES1(NanoGL)", size );
#elif defined XASH_WES
	Q_strncpy( out, "GLES2(gl-wes-v2)", size );
#elif defined XASH_GL4ES
	Q_strncpy( out, "GLES2(gl4es)", size );
#else
	Q_strncpy( out, "OpenGL", size );
#endif
}
