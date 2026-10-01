#ifndef GL_RAGDOLL_H
#define GL_RAGDOLL_H
void R_RagdollNewMap(void);
void R_RagdollInit(void);
void R_RagdollShutdown(void);
void R_RagdollFrame(void);
int R_RagdollApplyPose(cl_entity_t *entity, studiohdr_t *header, matrix3x4 *pose);
void R_RagdollBones(cl_entity_t *entity, studiohdr_t *header, matrix3x4 *pose);
int R_RagdollBounds(cl_entity_t *entity, vec3_t mins, vec3_t maxs);
#endif
