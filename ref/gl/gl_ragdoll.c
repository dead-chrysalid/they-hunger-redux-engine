/* Client-side enemy corpse simulation in the native RT studio path. */
#include "gl_local.h"
#include "studio.h"
#include "gl_ragdoll.h"
#include "ragdoll_physics.h"

#define RD_MARKER 19780
#define RD_PROP_MARKER 19781
static int R_PhysicsEntity(cl_entity_t *e) {
    return e && (e->curstate.iuser4==RD_MARKER || e->curstate.iuser4==RD_PROP_MARKER);
}
#define RD_POSE_CACHE 256
#define RD_BRUSH_LIMIT 2048
typedef struct {
    int entity, count;
    model_t *model;
    double time;
    int appliedBody;
    model_t *appliedModel;
    matrix3x4 pose[MAXSTUDIOBONES];
    vec3_t origin;
} rd_cache;
static rd_cache poses[RD_POSE_CACHE];
static model_t *brushes[RD_BRUSH_LIMIT];
static cvar_t *enabled, *debug;
static int registered, mapReady;
#define RD_SAVE_MAGIC 0x32474452 /* RDG2 */
typedef struct {
    int entity;
    unsigned int rig;
    char model[MAX_QPATH];
    rd_snapshot state;
} rd_record;
static rd_record restored[RD_MAX_CORPSES];
static int restoredCount;
static char restoredMap[MAX_QPATH];
static int R_RagdollWater(const float *bottom, float *surface);
static int R_RagdollMissingArm(cl_entity_t *e) {
    return e && e->model && e->curstate.body==1 &&
        !Q_strcmp(e->model->name,"models/characters/enemies/dismemberment_cop_pilot.mdl");
}
static unsigned int R_RagdollMissingParts(cl_entity_t *e) {
    static const char *names[]={"dm_head","dm_left_arm","dm_right_arm","dm_left_leg","dm_right_leg"};
    static const int groups[]={1,4,5,6,7};
    studiohdr_t *h;mstudiobodyparts_t *parts;unsigned int mask=0;int b,i;
    if(R_RagdollMissingArm(e)) return 1u<<4;
    if(!e || !e->model || e->curstate.body<0) return 0;
    h=(studiohdr_t *)gEngfuncs.Mod_Extradata(mod_studio,e->model);
    if(!h || h->numbodyparts<1 || h->numbodyparts>32) return 0;
    parts=(mstudiobodyparts_t *)((byte *)h+h->bodypartindex);
    for(b=0;b<h->numbodyparts;++b) if(parts[b].base>0 && parts[b].nummodels>0)
        for(i=0;i<5;++i) if(!Q_strcmp(parts[b].name,names[i]) && ((e->curstate.body/parts[b].base)%parts[b].nummodels)%2)
            mask|=1u<<groups[i];
    return mask;
}
static void R_RagdollApplyMissingParts(cl_entity_t *e,rd_cache *cache) {
    unsigned int mask;int group;
    if(!e) return;
    if(!RD_Has(e->index,e->curstate.fuser4)) return;
    // Body selectors only change on cuts. Settled piles need no repeated MDL
    // scan or unsuccessful collider removal every frame and studio draw.
    if(cache->appliedModel==e->model && cache->appliedBody==e->curstate.body) return;
    cache->appliedModel=e->model;cache->appliedBody=e->curstate.body;
    mask=R_RagdollMissingParts(e);
    for(group=1;group<=7;++group) if(mask & (1u<<group)) RD_RemoveGroup(e->index,group);
}
static void R_RagdollRemovePart(void) {
    int entity,group,part;cl_entity_t *e;studiohdr_t *h;mstudiobodyparts_t *parts;
    if(!enabled || !enabled->value || !ENGINE_GET_PARM(PARM_LOCAL_GAME) || gEngfuncs.Cmd_Argc()!=3) return;
    entity=Q_atoi(gEngfuncs.Cmd_Argv(1));group=Q_atoi(gEngfuncs.Cmd_Argv(2));
    if(group!=1 && group!=4 && group!=5 && group!=6 && group!=7) return;
    e=gEngfuncs.GetEntityByIndex(entity);
    if(!e || !e->model || e->curstate.iuser4!=RD_MARKER || !e->curstate.iuser3) return;
    h=(studiohdr_t *)gEngfuncs.Mod_Extradata(mod_studio,e->model);
    if(!h || h->numbodyparts<6 || h->numbodyparts>32) return;
    parts=(mstudiobodyparts_t *)((byte *)h+h->bodypartindex);
    for(part=0;part<h->numbodyparts;++part) if(!Q_strcmp(parts[part].name,"dm_head")) {RD_RemoveGroup(entity,group);break;}
}
static void R_RagdollRemoveGroup(void) {
    if(enabled && enabled->value && ENGINE_GET_PARM(PARM_LOCAL_GAME) && gEngfuncs.Cmd_Argc()==2) {
        int entity=Q_atoi(gEngfuncs.Cmd_Argv(1));
        cl_entity_t *e=gEngfuncs.GetEntityByIndex(entity);
        if(e && e->curstate.iuser4==RD_MARKER && e->curstate.iuser3 && e->model &&
            !Q_strcmp(e->model->name,"models/characters/enemies/dismemberment_cop_pilot.mdl")) RD_RemoveGroup(entity,4);
    }
}
int EXPORT Ragdoll_Bone(int entity,int bone,float *origin,float *angles) {
    matrix3x4 pose[RD_MAX_BONES];
    cl_entity_t *e=gEngfuncs.GetEntityByIndex(entity);
    studiohdr_t *h;
    if(!enabled || !enabled->value || !mapReady || !ENGINE_GET_PARM(PARM_LOCAL_GAME) ||
        !R_PhysicsEntity(e) || !e->model || !e->curstate.iuser3 || !RD_Has(entity,e->curstate.fuser4)) return 0;
    h=(studiohdr_t *)gEngfuncs.Mod_Extradata(mod_studio,e->model);
    if(!h || bone<0 || bone>=h->numbones || h->numbones>RD_MAX_BONES || !RD_Pose(entity,pose,h->numbones)) return 0;
    if(origin) Matrix3x4_OriginFromMatrix(pose[bone],origin);
    if(angles) Matrix3x4_AnglesFromMatrix(pose[bone],angles);
    return 1;
}
static unsigned int R_RagdollRig(studiohdr_t *header) {
    unsigned int hash = 2166136261u;
    int i;
    byte *bones = (byte *)header + header->boneindex;
    byte *boxes = (byte *)header + header->hitboxindex;
    for (i=0; i<header->numbones*(int)sizeof(mstudiobone_t); ++i) hash = (hash ^ bones[i])*16777619u;
    for (i=0; i<header->numhitboxes*(int)sizeof(mstudiobbox_t); ++i) hash = (hash ^ boxes[i])*16777619u;
    return hash;
}
static void R_RagdollImpulse(void) {
    vec3_t point,kick;
    int i,entity;
    if (!enabled || !enabled->value || !ENGINE_GET_PARM(PARM_LOCAL_GAME) || gEngfuncs.Cmd_Argc()!=8) return;
    entity = Q_atoi(gEngfuncs.Cmd_Argv(1));
    for (i=0; i<3; ++i) { point[i]=Q_atof(gEngfuncs.Cmd_Argv(i+2)); kick[i]=Q_atof(gEngfuncs.Cmd_Argv(i+5)); }
    RD_Impulse(entity,point,kick);
}
int EXPORT Ragdoll_Trace(const float *start,const float *end,float limit,rd_hit *hit) {
    return enabled && enabled->value && mapReady && ENGINE_GET_PARM(PARM_LOCAL_GAME) && RD_Trace(start,end,limit,hit);
}
void EXPORT Ragdoll_Save(const char *level) {
    char path[MAX_QPATH],map[MAX_QPATH];
    file_t *file;
    int header[3]={RD_SAVE_MAGIC,1,0},i,j;
    static rd_record record;
    COM_FileBase(level,map); Q_snprintf(path,sizeof(path),"SAVE/%s.HL4",map);
    file = gEngfuncs.fsapi->Open(path,"wb",true);
    if (!file) { gEngfuncs.Con_Printf("Ragdoll save: cannot write %s\n",path); return; }
    for (i=0; i<RD_POSE_CACHE; ++i) if (poses[i].entity && RD_BodyCount(poses[i].entity) &&
        gEngfuncs.GetEntityByIndex(poses[i].entity)->curstate.iuser4==RD_MARKER) ++header[2];
    // Preserve restored corpses outside the view that have not yet been drawn.
    if (!Q_strcmp(map,restoredMap)) for (i=0; i<restoredCount; ++i) if (restored[i].entity && !RD_BodyCount(restored[i].entity)) ++header[2];
    gEngfuncs.fsapi->Write(file,header,sizeof(header));
    for (i=0; i<RD_POSE_CACHE; ++i) if (poses[i].entity &&
        gEngfuncs.GetEntityByIndex(poses[i].entity)->curstate.iuser4==RD_MARKER && RD_Capture(poses[i].entity,&record.state)) {
        studiohdr_t *h=(studiohdr_t *)gEngfuncs.Mod_Extradata(mod_studio,poses[i].model);
        record.entity=poses[i].entity; record.rig=R_RagdollRig(h); Q_strncpy(record.model,poses[i].model->name,sizeof(record.model));
        gEngfuncs.fsapi->Write(file,&record,sizeof(record));
    }
    if (!Q_strcmp(map,restoredMap)) for (j=0; j<restoredCount; ++j) if(restored[j].entity && !RD_BodyCount(restored[j].entity))
        gEngfuncs.fsapi->Write(file,&restored[j],sizeof(restored[j]));
    gEngfuncs.fsapi->Close(file);
    gEngfuncs.Con_Printf("Ragdoll save: %d corpses in %s\n",header[2],path);
}
void EXPORT Ragdoll_Load(const char *level,const int *remap,int count) {
    char path[MAX_QPATH]; file_t *file; int header[3],i;
    restoredCount=0; COM_FileBase(level,restoredMap); Q_snprintf(path,sizeof(path),"SAVE/%s.HL4",restoredMap);
    file=gEngfuncs.fsapi->Open(path,"rb",true); if(!file) return; // older saves use safe pose fallback
    if(gEngfuncs.fsapi->Read(file,header,sizeof(header))!=sizeof(header) || header[0]!=RD_SAVE_MAGIC ||
        header[1]!=1 || header[2]<0 || header[2]>RD_MAX_CORPSES) { gEngfuncs.fsapi->Close(file); return; }
    for(i=0;i<header[2];++i) {
        if(gEngfuncs.fsapi->Read(file,&restored[i],sizeof(rd_record))!=sizeof(rd_record)) break;
        restored[i].model[MAX_QPATH-1]=0;
        restored[i].entity=restored[i].entity>0 && restored[i].entity<count ? remap[restored[i].entity] : 0;
        if(restored[i].entity<0) restored[i].entity=0;
    }
    if(i==header[2]) restoredCount=header[2];
    gEngfuncs.fsapi->Close(file);
    gEngfuncs.Con_Printf("Ragdoll load: %d saved corpses for %s\n",restoredCount,restoredMap);
}

static void R_RagdollStatus(void) {
    int i, active = 0;
    gEngfuncs.Con_Printf("Ragdolls: %d corpses, %d props (limits %d/%d), enabled %d\n", RD_CorpseCount(), RD_Count()-RD_CorpseCount(), RD_MAX_CORPSES, RD_MAX_PROPS, enabled && enabled->value != 0);
    gEngfuncs.Con_Printf("  dynamics solver: %d limb bodies; settled corpses use one frozen collider each\n",RD_ActiveBodies());
    for (i = 0; i < RD_POSE_CACHE; ++i) {
        if (poses[i].entity && RD_BodyCount(poses[i].entity)) {
            ++active;
            gEngfuncs.Con_Printf("  entity %d: %d bodies, %s, joint error %.3f units\n", poses[i].entity, RD_BodyCount(poses[i].entity), RD_Frozen(poses[i].entity)?"frozen":"active", RD_JointError(poses[i].entity));
        }
    }
    (void)active;
}
static void R_RagdollMesh(int key, model_t *model, const matrix3x4 transform, int kinematic) {
    int i, j;
    RD_BeginMesh(key);
    for (i = 0; i < model->nummodelsurfaces; ++i) {
        msurface_t *s = &model->surfaces[model->firstmodelsurface + i];
        vec3_t a, b, c;
        int edge, index;
        if (s->numedges < 3 || (s->flags & (SURF_DRAWSKY | SURF_DRAWTURB))) continue;
        edge = model->surfedges[s->firstedge];
        index = model->edges[abs(edge)].v[edge < 0 ? 1 : 0];
        VectorCopy(model->vertexes[index].position, a);
        for (j = 1; j + 1 < s->numedges; ++j) {
            edge = model->surfedges[s->firstedge + j];
            index = model->edges[abs(edge)].v[edge < 0 ? 1 : 0];
            VectorCopy(model->vertexes[index].position, b);
            edge = model->surfedges[s->firstedge + j + 1];
            index = model->edges[abs(edge)].v[edge < 0 ? 1 : 0];
            VectorCopy(model->vertexes[index].position, c);
            RD_AddTriangle(a, b, c);
        }
    }
    RD_EndMesh(transform, kinematic);
}
void R_RagdollInit(void) {
    if (!registered) {
        enabled = gEngfuncs.Cvar_Get("cl_ragdolls", "1", FCVAR_ARCHIVE, "Simulate killed walking monsters with Bullet physics");
        debug = gEngfuncs.Cvar_Get("cl_ragdoll_debug", "0", 0, "Log ragdoll creation and collider counts");
        gEngfuncs.Cmd_AddCommand("ragdoll_status", R_RagdollStatus, "Report corpse physics bodies and joint error");
        gEngfuncs.Cmd_AddCommand("ragdoll_impulse",R_RagdollImpulse,"Apply a local corpse hit impulse");
        gEngfuncs.Cmd_AddCommand("ragdoll_remove_arm",R_RagdollRemoveGroup,"Remove cop pilot arm colliders");
        gEngfuncs.Cmd_AddCommand("ragdoll_remove_part",R_RagdollRemovePart,"Remove a detached head or limb collider group");
        registered = 1;
    }
}
void R_RagdollNewMap(void) {
    matrix3x4 identity;
    RD_Reset();
    memset(poses, 0, sizeof(poses)); memset(brushes, 0, sizeof(brushes));
    mapReady = 0;
    R_RagdollInit();
    if (!enabled->value || !WORLDMODEL) return;
    Matrix3x4_LoadIdentity(identity);
    {
        char map[MAX_QPATH]; COM_FileBase(WORLDMODEL->name,map);
        if (Q_strcmp(map,restoredMap)) { restoredCount=0; restoredMap[0]=0; }
    }
    RD_SetWaterQuery(R_RagdollWater);
    R_RagdollMesh(0, WORLDMODEL, identity, 0);
    mapReady = 1;
}
void R_RagdollShutdown(void) {
    RD_Reset(); mapReady = 0;
    memset(poses, 0, sizeof(poses)); memset(brushes, 0, sizeof(brushes));
    if (registered) { gEngfuncs.Cmd_RemoveCommand("ragdoll_status"); gEngfuncs.Cmd_RemoveCommand("ragdoll_impulse"); gEngfuncs.Cmd_RemoveCommand("ragdoll_remove_arm"); gEngfuncs.Cmd_RemoveCommand("ragdoll_remove_part"); }
    restoredCount=0; restoredMap[0]=0;
    registered = 0; enabled = debug = NULL;
}
void R_RagdollFrame(void) {
    int i, count;
    if (!enabled || !enabled->value) {
        if (mapReady) RD_FreezeAll(gpGlobals->time);
        return;
    }
    if (!RI.drawWorld || !ENGINE_GET_PARM(PARM_LOCAL_GAME)) return;
    if (!mapReady) R_RagdollNewMap();
    count = (int)ENGINE_GET_PARM(PARM_NUMENTITIES);
    // Keep doors, elevators and trains as kinematic collision meshes.
    for (i = 1; i < count && i < RD_BRUSH_LIMIT; ++i) {
        cl_entity_t *e = gEngfuncs.GetEntityByIndex(i);
        matrix3x4 transform;
        if (!e || !e->model || e->model->type != mod_brush || e->curstate.solid == SOLID_NOT || e->curstate.solid == SOLID_TRIGGER) {
            if (brushes[i]) { RD_RemoveMesh(i); brushes[i] = NULL; }
            continue;
        }
        Matrix3x4_CreateFromEntity(transform, e->angles, e->origin, 1);
        if (brushes[i] != e->model) {
            R_RagdollMesh(i, e->model, transform, 1); brushes[i] = e->model;
        } else RD_MoveMesh(i, transform);
    }
    // Fullpacked dead actors carry fresh snapshots even outside the PVS.
    // Retire undrawn restored records too, so deleted corpses cannot consume the budget.
    {
        cl_entity_t *player=gEngfuncs.GetLocalPlayer();
        if(player && player->curstate.messagenum>0) for(i=0;i<restoredCount;++i) if(restored[i].entity) {
            cl_entity_t *e=gEngfuncs.GetEntityByIndex(restored[i].entity);
            if(!e || e->curstate.messagenum!=player->curstate.messagenum || e->curstate.iuser4!=RD_MARKER ||
                e->curstate.iuser3!=2 || (e->curstate.effects & EF_NODRAW)) restored[i].entity=0;
        }
    }
    // Remove simulation on gibbing, entity-slot reuse, resurrection or a new death token.
    for (i = 0; i < RD_POSE_CACHE; ++i) if (poses[i].entity) {
        cl_entity_t *e = gEngfuncs.GetEntityByIndex(poses[i].entity);
        cl_entity_t *player = gEngfuncs.GetLocalPlayer();
        if (!R_PhysicsEntity(e) || !e->model || (e->curstate.effects & EF_NODRAW) ||
            (RD_BodyCount(poses[i].entity) && poses[i].model != e->model) ||
            (RD_BodyCount(poses[i].entity) && player && e->curstate.messagenum != player->curstate.messagenum) ||
            (RD_BodyCount(poses[i].entity) && (!e->curstate.iuser3 || !RD_Has(e->index, e->curstate.fuser4)))) {
            RD_Remove(poses[i].entity); memset(&poses[i], 0, sizeof(poses[i]));
        }
    }
    for(i=0;i<RD_POSE_CACHE;++i) if(poses[i].entity) R_RagdollApplyMissingParts(gEngfuncs.GetEntityByIndex(poses[i].entity),&poses[i]);
    RD_Step(gpGlobals->time, gEngfuncs.pfnGetMoveVars()->gravity);
}
static rd_cache *R_RagdollCache(cl_entity_t *e) {
    int i, oldest = -1;
    for (i = 0; i < RD_POSE_CACHE; ++i) {
        if (poses[i].entity == e->index) return &poses[i];
        // Never evict the cache entry that owns a simulated corpse.
        if (!RD_BodyCount(poses[i].entity) && (oldest < 0 || poses[i].time < poses[oldest].time)) oldest = i;
    }
    return oldest < 0 ? NULL : &poses[oldest];
}
void R_RagdollBones(cl_entity_t *e, studiohdr_t *header, matrix3x4 *pose) {
    rd_cache *cache;
    int i;
    if (!enabled || !enabled->value || !mapReady || !R_PhysicsEntity(e) || e->player ||
        !RI.drawWorld || header->numbones < 1 || header->numbones > RD_MAX_BONES) return;
    cache = R_RagdollCache(e);
    if (!cache) return;
    if (!e->curstate.iuser3) {
        cache->entity = e->index; cache->model = e->model; cache->time = gpGlobals->time;
        cache->count = header->numbones; VectorCopy(e->origin, cache->origin);
        memcpy(cache->pose, pose, header->numbones * sizeof(matrix3x4));
        return;
    }
    if (!RD_Has(e->index, e->curstate.fuser4)) {
        cache->appliedModel=NULL;
        rd_bone bones[RD_MAX_BONES]; rd_hitbox boxes[256];
        mstudiobone_t *mdlBones = (mstudiobone_t *)((byte *)header + header->boneindex);
        mstudiobbox_t *mdlBoxes = (mstudiobbox_t *)((byte *)header + header->hitboxindex);
        matrix3x4 initial[RD_MAX_BONES];
        int recent = cache->entity == e->index && cache->count == header->numbones && gpGlobals->time - cache->time < 0.5;
        if (header->numhitboxes < 0 || header->numhitboxes > 256 || (!header->numhitboxes && e->curstate.iuser4!=RD_PROP_MARKER)) return;
        // Headless models may replace the live model at the lethal hit. Reuse
        // the live pose only when the bone hierarchy matches as well as its size.
        if (recent && cache->model != e->model) {
            studiohdr_t *old = (studiohdr_t *)gEngfuncs.Mod_Extradata(mod_studio, cache->model);
            mstudiobone_t *oldBones = old ? (mstudiobone_t *)((byte *)old + old->boneindex) : NULL;
            if (!old || old->numbones != header->numbones) recent = 0;
            for (i = 0; recent && i < header->numbones; ++i)
                if (oldBones[i].parent != mdlBones[i].parent || Q_strcmp(oldBones[i].name, mdlBones[i].name)) recent = 0;
        }
        memcpy(initial, recent ? cache->pose : pose, header->numbones * sizeof(matrix3x4));
        if (recent) for (i = 0; i < header->numbones; ++i) {
            initial[i][0][3] += e->origin[0] - cache->origin[0];
            initial[i][1][3] += e->origin[1] - cache->origin[1];
            initial[i][2][3] += e->origin[2] - cache->origin[2];
        }
        for (i = 0; i < header->numbones; ++i) { bones[i].parent = mdlBones[i].parent; Q_strncpy(bones[i].name, mdlBones[i].name, 32); }
        for (i = 0; i < header->numhitboxes; ++i) {
            boxes[i].bone = mdlBoxes[i].bone; boxes[i].group = mdlBoxes[i].group;
            VectorCopy(mdlBoxes[i].bbmin, boxes[i].mins); VectorCopy(mdlBoxes[i].bbmax, boxes[i].maxs);
        }
        {
            int b;
            mstudiobodyparts_t *parts=(mstudiobodyparts_t *)((byte *)header+header->bodypartindex);
            for(b=0;b<header->numbodyparts;++b) {
                mstudiomodel_t *models=(mstudiomodel_t *)((byte *)header+parts[b].modelindex);
                int selected;
                if(parts[b].base<=0 || parts[b].nummodels<=0 || e->curstate.body<0) return;
                selected=(e->curstate.body/parts[b].base)%parts[b].nummodels;
                RD_FitFootHitboxes(header->numbones,bones,models[selected].numverts,
                    (const float (*)[3])((byte *)header+models[selected].vertindex),
                    (byte *)header+models[selected].vertinfoindex,header->numhitboxes,boxes);
            }
        }
        if(e->curstate.iuser4==RD_PROP_MARKER) {
            // Build a single convex rigid collider from the visible mesh in root-bone space.
            // Other bones retain their local pose; the complete limb moves as one prop.
            vec3_t points[8192],world;
            int pointCount=0,b,m,v;
            mstudiobodyparts_t *parts=(mstudiobodyparts_t *)((byte *)header+header->bodypartindex);
            for(b=0;b<header->numbodyparts;++b) {
                mstudiomodel_t *models=(mstudiomodel_t *)((byte *)header+parts[b].modelindex);
                if(parts[b].base<=0 || parts[b].nummodels<=0 || e->curstate.body<0) return;
                m=(e->curstate.body/parts[b].base)%parts[b].nummodels;
                vec3_t *vertices=(vec3_t *)((byte *)header+models[m].vertindex);
                byte *vertexBones=(byte *)header+models[m].vertinfoindex;
                if(models[m].numverts>8192-pointCount) return;
                for(v=0;v<models[m].numverts;++v) {
                    if(vertexBones[v]>=header->numbones) return;
                    Matrix3x4_VectorTransform(initial[vertexBones[v]],vertices[v],world);
                    Matrix3x4_VectorITransform(initial[0],world,points[pointCount++]);
                }
            }
            if(!RD_CreateProp(e->index,e->curstate.fuser4,header->numbones,bones,initial,
                pointCount,points,e->curstate.vuser4,e->curstate.vuser3)) return;
            cache->entity=e->index;cache->model=e->model;cache->count=header->numbones;
            RD_Pose(e->index,pose,header->numbones);
            return;
        }
        int loaded=0;
        if (e->curstate.iuser3==2) for (i=0;i<restoredCount;++i) {
            if (restored[i].entity==e->index && restored[i].rig==R_RagdollRig(header) &&
                !Q_strcmp(restored[i].model,e->model->name) && restored[i].state.count==header->numbones) {
                loaded=RD_Restore(e->index,e->curstate.fuser4,bones,header->numhitboxes,boxes,&restored[i].state);
                restored[i].entity=0;
                break;
            }
        }
        if (!loaded) {
            int pending=0;
            for(i=0;i<restoredCount;++i) if(restored[i].entity && !RD_BodyCount(restored[i].entity)) ++pending;
            if(e->curstate.iuser3!=2 && RD_CorpseCount()+pending>=RD_MAX_CORPSES) return;
            vec3_t zero={0,0,0};
            if (!RD_Create(e->index, e->curstate.fuser4, header->numbones, bones, header->numhitboxes, boxes,
                initial, e->curstate.iuser3==2?zero:e->curstate.vuser4, e->curstate.iuser2)) return;
            if (e->curstate.iuser3==2) RD_RestoreLegacy(e->index);
        }
        cache->entity = e->index; cache->model = e->model; cache->count = header->numbones;
        if (debug->value) gEngfuncs.Con_Printf("Ragdoll created: entity %d model %s, %d bodies\n", e->index, e->model->name, RD_BodyCount(e->index));
    }
    R_RagdollApplyMissingParts(e,cache);
    RD_Pose(e->index, pose, header->numbones);
}
int R_RagdollBounds(cl_entity_t *e, vec3_t mins, vec3_t maxs) {
    return mapReady && R_PhysicsEntity(e) && e->curstate.iuser3 &&
        RD_Has(e->index, e->curstate.fuser4) && RD_Bounds(e->index, mins, maxs);
}

int R_RagdollApplyPose(cl_entity_t *e,studiohdr_t *header,matrix3x4 *pose) {
    return mapReady && R_PhysicsEntity(e) && e->curstate.iuser3 &&
        RD_Has(e->index,e->curstate.fuser4) && RD_Pose(e->index,pose,header->numbones);
}
static int R_RagdollWater(const float *bottom,float *surface) {
    vec3_t p; float lo,hi; int i;
    mleaf_t *leaf;
    if(!WORLDMODEL) return 0;
    VectorCopy(bottom,p); leaf=gEngfuncs.Mod_PointInLeaf(p,WORLDMODEL->nodes);
    if(leaf->contents!=CONTENTS_WATER && leaf->contents!=CONTENTS_SLIME) return 0;
    lo=p[2]; hi=lo;
    // Find the top of this vertical liquid column, then refine its surface.
    for(i=0;i<64;++i) {
        hi+=32; p[2]=hi; leaf=gEngfuncs.Mod_PointInLeaf(p,WORLDMODEL->nodes);
        if(leaf->contents!=CONTENTS_WATER && leaf->contents!=CONTENTS_SLIME) break;
        lo=hi;
    }
    if(i==64) { *surface=hi; return 1; }
    for(i=0;i<12;++i) {
        p[2]=(lo+hi)*0.5f; leaf=gEngfuncs.Mod_PointInLeaf(p,WORLDMODEL->nodes);
        if(leaf->contents==CONTENTS_WATER || leaf->contents==CONTENTS_SLIME) lo=p[2]; else hi=p[2];
    }
    *surface=(lo+hi)*0.5f; return 1;
}
