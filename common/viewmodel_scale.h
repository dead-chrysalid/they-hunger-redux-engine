#ifndef STILLHUNGER_VIEWMODEL_SCALE_H
#define STILLHUNGER_VIEWMODEL_SCALE_H

// Maximum animated mesh/muzzle reach in authored units, rounded up.
// Scout excludes the deliberately off-screen tag_bullet projectile.
// Shared by RT geometry/attachments and first-person muzzle flash sprites.
static float SH_ViewmodelScaleForReach( const char *name, float metres )
{
	static const struct { const char *name; float reach; } models[] =
	{
		{ "models/weapons/view/v_umbrella.mdl", 30.0f },
		{ "models/weapons/view/v_valvebat.mdl", 32.0f },
		{ "models/weapons/view/v_knife.mdl", 20.0f },
		{ "models/weapons/view/v_knife_glass.mdl", 20.0f },
		{ "models/weapons/view/v_chainsaw.mdl", 42.0f },
		{ "models/weapons/view/v_beretta.mdl", 50.0f },
		{ "models/weapons/view/v_deagle.mdl", 35.0f },
		{ "models/weapons/view/v_uzi.mdl", 49.0f },
		{ "models/weapons/view/v_fiveseven.mdl", 41.0f },
		{ "models/weapons/view/v_m4a1.mdl", 85.0f },
		{ "models/weapons/view/v_mossberg.mdl", 73.0f },
		{ "models/weapons/view/v_awp.mdl", 126.0f },
		{ "models/weapons/view/v_g3sg1.mdl", 72.0f },
		{ "models/weapons/view/v_scout.mdl", 62.0f },
		{ "models/weapons/view/v_tesla.mdl", 30.0f },
		{ "models/weapons/view/v_m60.mdl", 31.0f },
		{ "models/weapons/view/v_adrenaline.mdl", 49.0f },
		{ "models/weapons/view/v_grenade.mdl", 33.0f },
		{ "models/weapons/view/v_throw_general.mdl", 32.0f },
	};
	int i;
	float reach;
	if( !name || !(metres > 0.0f) ) return 1.0f;
	reach = bound( 0.05f, metres, 2.0f ) / 0.025f;
	for( i = 0; i < sizeof( models ) / sizeof( models[0] ); i++ )
	{
		if( !Q_stricmp( name, models[i].name ))
			return Q_min( 1.0f, reach / models[i].reach );
	}
	return 1.0f;
}

#endif

