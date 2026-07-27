/*
hud_text.h - engine-owned TrueType rendering for classic HUD text
*/
#ifndef HUD_TEXT_H
#define HUD_TEXT_H

typedef unsigned int hud_text_style_t;

void CL_HudTextVidInit( void );
void CL_HudTextShutdown( void );
void CL_HudTextInfo_f( void );
void CL_HudTextTest_f( void );
void CL_HudTextDrawTest( void );
qboolean CL_HudTextReady( void );
qboolean CL_HudTextStyleReady( hud_text_style_t style );
int CL_HudTextDrawCharacter( hud_text_style_t style, int x, int y, int ch, int r, int g, int b, int a );
int CL_HudTextGetHeight( hud_text_style_t style );
int CL_HudTextGetCharWidth( hud_text_style_t style, int ch );

#endif // HUD_TEXT_H
