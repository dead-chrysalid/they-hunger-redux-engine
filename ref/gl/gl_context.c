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
	CVAR_INT( "hud_v6_gradient_alpha", "Gradient opacity", "HUD v6 / Shared", 175, 0, 255 ),

	CVAR_INT( "hud_v6_weapon_x", "Screen X", "HUD v6 / Weapon Menu", 16, 0, 512 ),
	CVAR_INT( "hud_v6_weapon_y", "Screen Y", "HUD v6 / Weapon Menu", 16, 0, 512 ),
	CVAR_INT( "hud_v6_weapon_header_font_size", "Header font size", "HUD v6 / Weapon Menu", 36, 18, 64 ),
	CVAR_INT( "hud_v6_weapon_row_font_size", "Border font size", "HUD v6 / Weapon Menu", 36, 18, 64 ),
	CVAR_INT( "hud_v6_weapon_name_font_size", "Weapon name font size", "HUD v6 / Weapon Menu", 18, 18, 64 ),
	CVAR_INT( "hud_v6_weapon_ammo_font_size", "Weapon ammo font size", "HUD v6 / Weapon Menu", 18, 18, 64 ),
	CVAR_INT( "hud_v6_weapon_columns", "Inner columns", "HUD v6 / Weapon Menu", 15, 8, 24 ),
	CVAR_INT( "hud_v6_weapon_row_gap", "Row gap", "HUD v6 / Weapon Menu", 0, 0, 32 ),
	CVAR_INT( "hud_v6_weapon_text_inset", "Text inset", "HUD v6 / Weapon Menu", 4, 0, 64 ),
	CVAR_INT( "hud_v6_weapon_text_y_offset", "Text Y offset", "HUD v6 / Weapon Menu", 0, -64, 64 ),
	CVAR_INT( "hud_v6_weapon_icon_inset_x", "Icon inset X", "HUD v6 / Weapon Menu", 3, 0, 64 ),
	CVAR_INT( "hud_v6_weapon_icon_inset_y", "Icon inset Y", "HUD v6 / Weapon Menu", 3, 0, 64 ),
	CVAR_INT( "hud_v6_weapon_icon_offset_x", "Icon offset X", "HUD v6 / Weapon Menu", 0, -128, 128 ),
	CVAR_INT( "hud_v6_weapon_icon_offset_y", "Icon offset Y", "HUD v6 / Weapon Menu", 0, -128, 128 ),
	CVAR_FLOAT( "hud_v6_weapon_icon_scale", "Icon scale", "HUD v6 / Weapon Menu", 1.0f, 0.1f, 3.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_v6_weapon_background_inset_left", "Background inset left", "HUD v6 / Weapon Menu", 2, -64, 64 ),
	CVAR_INT( "hud_v6_weapon_background_inset_right", "Background inset right", "HUD v6 / Weapon Menu", 2, -64, 64 ),
	CVAR_INT( "hud_v6_weapon_background_inset_top", "Background inset top", "HUD v6 / Weapon Menu", 2, -64, 64 ),
	CVAR_INT( "hud_v6_weapon_background_inset_bottom", "Background inset bottom", "HUD v6 / Weapon Menu", 2, -64, 64 ),

	CVAR_INT( "hud_v6_toast_x", "Screen X", "HUD v6 / Toast Console", 8, 0, 1024 ),
	CVAR_INT( "hud_v6_toast_bottom", "Bottom offset", "HUD v6 / Toast Console", 216, 0, 1024 ),
	CVAR_INT( "hud_v6_toast_font_size", "Text font size", "HUD v6 / Toast Console", 28, 18, 64 ),
	CVAR_FLOAT( "hud_v6_toast_max_width_fraction", "Maximum screen width", "HUD v6 / Toast Console", 0.45f, 0.1f, 0.9f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_v6_toast_line_gap", "Line gap", "HUD v6 / Toast Console", 0, 0, 64 ),
	CVAR_INT( "hud_v6_toast_visible_rows", "Visible rows", "HUD v6 / Toast Console", 12, 1, 12 ),
	CVAR_FLOAT( "hud_v6_toast_character_time", "Seconds per character", "HUD v6 / Toast Console", 0.02f, 0.001f, 1.0f, 0.005f, "%.3f" ),
	CVAR_FLOAT( "hud_v6_toast_lifetime", "Visible lifetime", "HUD v6 / Toast Console", 8.0f, 0.05f, 120.0f, 0.05f, "%.2f s" ),

	CVAR_INT( "hud_v6_stack_x", "Screen X", "HUD v6 / Item Stack", 8, 0, 512 ),
	CVAR_INT( "hud_v6_stack_bottom", "Bottom offset", "HUD v6 / Item Stack", 8, 0, 512 ),
	CVAR_INT( "hud_v6_stack_panel_height", "Shared panel height", "HUD v6 / Item Stack", 55, 24, 256 ),
	CVAR_INT( "hud_v6_stack_gap", "Panel gap", "HUD v6 / Item Stack", 4, 0, 128 ),
	CVAR_INT( "hud_v6_item_width", "Item width", "HUD v6 / Item Stack", 112, 32, 512 ),
	CVAR_INT( "hud_v6_item_padding_x", "Text inset X", "HUD v6 / Item Stack", 8, 0, 128 ),
	CVAR_INT( "hud_v6_item_amount_font_size", "Amount font size", "HUD v6 / Item Stack", 36, 18, 64 ),
	CVAR_INT( "hud_v6_item_label_font_size", "Label font size", "HUD v6 / Item Stack", 18, 18, 64 ),
	CVAR_INT( "hud_v6_item_amount_y", "Amount Y", "HUD v6 / Item Stack", 4, -128, 256 ),
	CVAR_INT( "hud_v6_item_label_y", "Label Y", "HUD v6 / Item Stack", 34, -128, 256 ),

	CVAR_INT( "hud_v6_health_width", "Panel width", "HUD v6 / Health", 306, 96, 1024 ),
	CVAR_INT( "hud_v6_health_state_font_size", "State font size", "HUD v6 / Health", 36, 18, 64 ),
	CVAR_INT( "hud_v6_health_bracket_font_size", "Bracket font size", "HUD v6 / Health", 36, 18, 64 ),
	CVAR_INT( "hud_v6_health_state_width", "State field width", "HUD v6 / Health", 60, 16, 384 ),
	CVAR_INT( "hud_v6_health_state_y_offset", "State Y offset", "HUD v6 / Health", 0, -128, 128 ),
	CVAR_INT( "hud_v6_health_state_to_bracket_gap", "State to bracket gap", "HUD v6 / Health", 10, 0, 256 ),
	CVAR_INT( "hud_v6_health_bracket_bar_gap", "Bracket to bar gap", "HUD v6 / Health", 3, 0, 128 ),
	CVAR_INT( "hud_v6_health_padding_right", "Right padding", "HUD v6 / Health", 8, 0, 256 ),
	CVAR_INT( "hud_v6_health_bar_height", "Bar height", "HUD v6 / Health", 3, 1, 64 ),
	CVAR_INT( "hud_v6_health_bar_y_offset", "Bar Y offset", "HUD v6 / Health", 0, -128, 128 ),
	CVAR_FLOAT( "hud_v6_health_hazard_cycle_time", "Hazard cycle time", "HUD v6 / Health", 0.5f, 0.05f, 10.0f, 0.05f, "%.2f s" ),
	CVAR_FLOAT( "hud_v6_health_hazard_blink_time", "Hazard blink time", "HUD v6 / Health", 0.25f, 0.02f, 10.0f, 0.01f, "%.2f s" ),
	CVAR_FLOAT( "hud_v6_health_hazard_lifetime", "Hazard lifetime", "HUD v6 / Health", 2.0f, 0.05f, 30.0f, 0.05f, "%.2f s" ),

	CVAR_INT( "hud_v6_ammo_right", "Right margin", "HUD v6 / Ammo", 16, 0, 512 ),
	CVAR_INT( "hud_v6_ammo_bottom", "Bottom margin", "HUD v6 / Ammo", 16, 0, 512 ),
	CVAR_INT( "hud_v6_ammo_panel_width", "Panel width", "HUD v6 / Ammo", 128, 64, 512 ),
	CVAR_INT( "hud_v6_ammo_panel_height", "Panel height", "HUD v6 / Ammo", 83, 32, 384 ),
	CVAR_INT( "hud_v6_ammo_current_font_size", "Magazine font size", "HUD v6 / Ammo", 36, 18, 64 ),
	CVAR_INT( "hud_v6_ammo_slash_font_size", "Slash font size", "HUD v6 / Ammo", 64, 18, 64 ),
	CVAR_INT( "hud_v6_ammo_reserve_font_size", "Reserve font size", "HUD v6 / Ammo", 28, 18, 64 ),
	CVAR_INT( "hud_v6_ammo_clipless_font_size", "Clipless reserve font size", "HUD v6 / Ammo", 36, 18, 64 ),
	CVAR_INT( "hud_v6_ammo_label_font_size", "Label font size", "HUD v6 / Ammo", 18, 18, 64 ),
	CVAR_INT( "hud_v6_ammo_background_inset_left", "Background inset left", "HUD v6 / Ammo", 0, -64, 64 ),
	CVAR_INT( "hud_v6_ammo_background_inset_right", "Background inset right", "HUD v6 / Ammo", 0, -64, 64 ),
	CVAR_INT( "hud_v6_ammo_background_inset_top", "Background inset top", "HUD v6 / Ammo", 0, -64, 64 ),
	CVAR_INT( "hud_v6_ammo_background_inset_bottom", "Background inset bottom", "HUD v6 / Ammo", 0, -64, 64 ),
	CVAR_INT( "hud_v6_ammo_current_right", "Magazine right anchor", "HUD v6 / Ammo", 46, -128, 512 ),
	CVAR_INT( "hud_v6_ammo_current_y", "Magazine Y", "HUD v6 / Ammo", 8, -128, 384 ),
	CVAR_INT( "hud_v6_ammo_slash_center_x", "Slash center X", "HUD v6 / Ammo", 64, -128, 512 ),
	CVAR_INT( "hud_v6_ammo_slash_y", "Slash Y", "HUD v6 / Ammo", 12, -128, 384 ),
	CVAR_INT( "hud_v6_ammo_reserve_left", "Reserve left anchor", "HUD v6 / Ammo", 76, -128, 512 ),
	CVAR_INT( "hud_v6_ammo_reserve_y", "Reserve Y", "HUD v6 / Ammo", 42, -128, 384 ),
	CVAR_INT( "hud_v6_ammo_label_right", "Label right inset", "HUD v6 / Ammo", 8, -128, 256 ),
	CVAR_INT( "hud_v6_ammo_label_y", "Label Y", "HUD v6 / Ammo", 64, -128, 384 ),
	CVAR_INT( "hud_v6_ammo_clipless_reserve_right", "Clipless reserve right inset", "HUD v6 / Ammo", 8, -128, 256 ),
	CVAR_INT( "hud_v6_ammo_clipless_reserve_y", "Clipless reserve Y", "HUD v6 / Ammo", 8, -128, 384 ),

	CVAR_BOOL( "hud_draw", "Draw HUD", "HUD / General", 1.0f ),
	CVAR_BOOL( "crosshair", "Draw crosshair", "HUD / General", 1.0f ),
	CVAR_BOOL( "hud_fastswitch", "Fast weapon switch", "HUD / General", 0.0f ),
	CVAR_FLOAT( "hud_deathnotice_time", "Death notice time", "HUD / General", 6.0f, 0.0f, 30.0f, 0.1f, "%.1f s" ),
	CVAR_INT( "hud_textmode", "HUD text mode", "HUD / General", 0, 0, 2 ),

	CVAR_INT( "hud_crosshair_bar_min_length", "Minimum bar length", "Crosshair", 4, 1, 128 ),
	CVAR_INT( "hud_crosshair_bar_max_length", "Maximum bar length", "Crosshair", 14, 1, 256 ),
	CVAR_INT( "hud_crosshair_bar_thickness", "Bar thickness", "Crosshair", 1, 1, 64 ),
	CVAR_INT( "hud_crosshair_bar_opacity", "Bar opacity", "Crosshair", 128, 0, 255 ),
	CVAR_INT( "hud_crosshair_dot_gap_threshold", "Dot gap threshold", "Crosshair", 2, 0, 128 ),
	CVAR_INT( "hud_crosshair_dot_size", "Dot size", "Crosshair", 2, 1, 64 ),
	CVAR_FLOAT( "hud_scope_crosshair_scale", "Scope scale", "Crosshair", 1.0f, 0.1f, 4.0f, 0.01f, "%.2f" ),
	CVAR_INT( "hud_scope_crosshair_size", "Explicit scope size", "Crosshair", 0, 0, 4096 ),

	CVAR_BOOL( "hit_markers", "Enabled", "Hitmarkers", 1.0f ),
	CVAR_BOOL( "hit_markers_simple", "Simple style", "Hitmarkers", 0.0f ),
	CVAR_INT( "hit_markers_simple_length", "Simple length", "Hitmarkers", 14, 1, 128 ),
	CVAR_INT( "hit_markers_size", "Size", "Hitmarkers", 14, 1, 128 ),
	CVAR_INT( "hit_markers_gap", "Gap", "Hitmarkers", 7, 0, 128 ),
	CVAR_FLOAT( "hit_markers_opacity", "Opacity", "Hitmarkers", 1.0f, 0.0f, 1.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "hit_markers_start_scale", "Start scale", "Hitmarkers", 0.8f, 0.0f, 4.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "hit_markers_peak_scale", "Peak scale", "Hitmarkers", 1.0f, 0.0f, 4.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "hit_markers_end_scale", "End scale", "Hitmarkers", 0.7f, 0.0f, 4.0f, 0.01f, "%.2f" ),
	CVAR_FLOAT( "hit_markers_fade_in_time", "Fade-in time", "Hitmarkers", 0.025f, 0.0f, 2.0f, 0.005f, "%.3f s" ),
	CVAR_FLOAT( "hit_markers_pop_time", "Pop time", "Hitmarkers", 0.05f, 0.0f, 2.0f, 0.005f, "%.3f s" ),
	CVAR_FLOAT( "hit_markers_fade_out_time", "Fade-out time", "Hitmarkers", 0.28f, 0.0f, 2.0f, 0.005f, "%.3f s" ),

	CVAR_BOOL( "killfx_enable", "Enabled", "Kill Effects", 1.0f ),
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

#define RT_EDITOR_MAX_CVARS 256
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
	(void)userdata;
	gEngfuncs.Cbuf_AddText( "_hud_cvar_editor_commit\n" );
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
