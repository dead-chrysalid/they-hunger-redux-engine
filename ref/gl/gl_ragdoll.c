/* Client-side enemy corpse simulation in the native RT studio path. */
#include "gl_local.h"
#include "studio.h"
#include "gl_ragdoll.h"
#include "ragdoll_physics.h"

#define RD_MARKER 19780
#define RD_POSE_CACHE 128
#define RD_BRUSH_LIMIT 2048
typedef struct {
    int entity, count;
    model_t *model;
    double time;
    matrix3x4 pose[MAXSTUDIOBONES];
    vec3_t origin;
} rd_cache;
static rd_cache poses[RD_POSE_CACHE];
static model_t *brushes[RD_BRUSH_LIMIT];
static cvar_t *enabled, *debug;
static int registered, mapReady;

static void R_RagdollStatus(void) {
    int i, active = 0;
    gEngfuncs.Con_Printf("Ragdolls: %d corpses (limit %d), enabled %d\n", RD_Count(), RD_MAX_CORPSES, enabled && enabled->value != 0);
    for (i = 0; i < RD_POSE_CACHE; ++i) {
        if (poses[i].entity && RD_BodyCount(poses[i].entity)) {
            ++active;
            gEngfuncs.Con_Printf("  entity %d: %d bodies, joint error %.3f units\n", poses[i].entity, RD_BodyCount(poses[i].entity), RD_JointError(poses[i].entity));
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
void R_RagdollNewMap(void) {
    matrix3x4 identity;
    RD_Reset();
    memset(poses, 0, sizeof(poses)); memset(brushes, 0, sizeof(brushes));
    mapReady = 0;
    if (!registered) {
        enabled = gEngfuncs.Cvar_Get("cl_ragdolls", "1", FCVAR_ARCHIVE, "Simulate killed walking monsters with Bullet physics");
        debug = gEngfuncs.Cvar_Get("cl_ragdoll_debug", "0", 0, "Log ragdoll creation and collider counts");
        gEngfuncs.Cmd_AddCommand("ragdoll_status", R_RagdollStatus, "Report corpse physics bodies and joint error");
        registered = 1;
    }
    if (!enabled->value || !WORLDMODEL) return;
    Matrix3x4_LoadIdentity(identity);
    R_RagdollMesh(0, WORLDMODEL, identity, 0);
    mapReady = 1;
}
void R_RagdollShutdown(void) {
    RD_Reset(); mapReady = 0;
    memset(poses, 0, sizeof(poses)); memset(brushes, 0, sizeof(brushes));
    if (registered) gEngfuncs.Cmd_RemoveCommand("ragdoll_status");
    registered = 0; enabled = debug = NULL;
}
void R_RagdollFrame(void) {
    int i, count;
    if (!enabled || !enabled->value) {
        if (mapReady) { RD_Reset(); mapReady = 0; memset(poses, 0, sizeof(poses)); memset(brushes, 0, sizeof(brushes)); }
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
    // Remove simulation on gibbing, entity-slot reuse, resurrection or a new death token.
    for (i = 0; i < RD_POSE_CACHE; ++i) if (poses[i].entity) {
        cl_entity_t *e = gEngfuncs.GetEntityByIndex(poses[i].entity);
        cl_entity_t *player = gEngfuncs.GetLocalPlayer();
        if (!e || !e->model || e->curstate.iuser4 != RD_MARKER || (e->curstate.effects & EF_NODRAW) ||
            (RD_BodyCount(poses[i].entity) && poses[i].model != e->model) ||
            (RD_BodyCount(poses[i].entity) && player && e->curstate.messagenum != player->curstate.messagenum) ||
            (RD_BodyCount(poses[i].entity) && (!e->curstate.iuser3 || !RD_Has(e->index, e->curstate.fuser4)))) {
            RD_Remove(poses[i].entity); memset(&poses[i], 0, sizeof(poses[i]));
        }
    }
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
    if (!enabled || !enabled->value || !mapReady || e->curstate.iuser4 != RD_MARKER || e->player ||
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
        rd_bone bones[RD_MAX_BONES]; rd_hitbox boxes[256];
        mstudiobone_t *mdlBones = (mstudiobone_t *)((byte *)header + header->boneindex);
        mstudiobbox_t *mdlBoxes = (mstudiobbox_t *)((byte *)header + header->hitboxindex);
        matrix3x4 initial[RD_MAX_BONES];
        int recent = cache->entity == e->index && cache->count == header->numbones && gpGlobals->time - cache->time < 0.5;
        if (header->numhitboxes < 1 || header->numhitboxes > 256) return;
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
        if (!RD_Create(e->index, e->curstate.fuser4, header->numbones, bones, header->numhitboxes, boxes,
            initial, e->curstate.vuser4, e->curstate.iuser2)) return;
        cache->entity = e->index; cache->model = e->model; cache->count = header->numbones;
        if (debug->value) gEngfuncs.Con_Printf("Ragdoll created: entity %d model %s, %d bodies\n", e->index, e->model->name, RD_BodyCount(e->index));
    }
    RD_Pose(e->index, pose, header->numbones);
}
int R_RagdollBounds(cl_entity_t *e, vec3_t mins, vec3_t maxs) {
    return enabled && enabled->value && e->curstate.iuser4 == RD_MARKER && e->curstate.iuser3 &&
        RD_Has(e->index, e->curstate.fuser4) && RD_Bounds(e->index, mins, maxs);
}
