/* Internal C bridge. No change to client API 10 or renderer API 4. */
#ifndef RAGDOLL_PHYSICS_H
#define RAGDOLL_PHYSICS_H
#ifdef __cplusplus
extern "C" {
#endif
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
