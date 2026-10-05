/* Optional local-machine renderer exports, outside API-10/API-4 tables. */
#ifndef RAGDOLL_BRIDGE_H
#define RAGDOLL_BRIDGE_H
typedef struct {
    int entity, group;
    float fraction, point[3], normal[3];
} rd_hit;
typedef int (*rd_trace_export)(const float *, const float *, float, rd_hit *);
typedef int (*rd_bone_export)(int, int, float *, float *);
typedef void (*rd_save_export)(const char *);
typedef void (*rd_load_export)(const char *, const int *, int);
#endif
