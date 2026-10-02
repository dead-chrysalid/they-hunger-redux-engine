/* Internal C bridge. No change to client API 10 or renderer API 4. */
#ifndef RAGDOLL_PHYSICS_H
#define RAGDOLL_PHYSICS_H
#ifdef __cplusplus
extern "C" {
#endif
#include "../../common/ragdoll_bridge.h"
#define RD_MAX_BONES 128
#define RD_MAX_BODIES 64
#define RD_MAX_CORPSES 64
typedef float rd_matrix[3][4];
typedef struct {
    int parent;
    char name[32];
} rd_bone;
typedef struct {
    int bone, group;
    float mins[3], maxs[3];
} rd_hitbox;
/* Portable state, indexed by studio bone; no Bullet pointer/ABI serialization. */
typedef struct {
    int count, frozen;
    rd_matrix initial[RD_MAX_BONES], pose[RD_MAX_BONES];
    float linear[RD_MAX_BONES][3], angular[RD_MAX_BONES][3];
} rd_snapshot;
typedef int (*rd_water_query)(const float *bottom, float *surface);
void RD_SetWaterQuery(rd_water_query query);
int RD_Capture(int entity, rd_snapshot *state);
int RD_Restore(int entity, float token, const rd_bone *bones,
    int hitboxCount, const rd_hitbox *hitboxes, const rd_snapshot *state);
void RD_RestoreLegacy(int entity);
int RD_Trace(const float *start, const float *end, float limit, rd_hit *hit);
void RD_Impulse(int entity, const float *point, const float *impulse);
int RD_RemoveGroup(int entity, int group);
int RD_Frozen(int entity);
int RD_ActiveBodies(void);
void RD_Reset(void);
void RD_BeginMesh(int key);
void RD_AddTriangle(const float *a, const float *b, const float *c);
void RD_EndMesh(const rd_matrix transform, int kinematic);
void RD_MoveMesh(int key, const rd_matrix transform);
void RD_RemoveMesh(int key);
int RD_Create(int entity, float token, int count, const rd_bone *bones,
    int hitboxCount, const rd_hitbox *hitboxes, const rd_matrix *pose,
    const float *velocity, int hitgroup);
void RD_Remove(int entity);
int RD_Has(int entity, float token);
void RD_Step(double time, float gravity);
int RD_Pose(int entity, rd_matrix *pose, int count);
int RD_Bounds(int entity, float *mins, float *maxs);
int RD_Count(void);
int RD_BodyCount(int entity);
float RD_JointError(int entity);
#ifdef __cplusplus
}
#endif
#endif
