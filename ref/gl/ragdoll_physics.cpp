/* Enemy ragdoll experiment. Bullet is vendored under its zlib license.
 * Original integration; see Documentation/ragdolls.md for research/provenance. */
#include "ragdoll_physics.h"
#include "../../3rdparty/bullet/src/btBulletDynamicsCommon.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include <unordered_map>

namespace {
const float Scale = 0.0254f; // GoldSrc inch -> metre; Z stays up.
enum { WorldGroup = 1, CorpseGroup = 2 };
btVector3 Vec(const float *p) { return btVector3(p[0], p[1], p[2]) * Scale; }
btTransform Transform(const rd_matrix m) {
    btMatrix3x3 basis(m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]);
    // Studio scale is allowed, Bullet transforms must remain orthonormal.
    btVector3 x = basis.getColumn(0).normalized(), y = basis.getColumn(1);
    y = (y - x * x.dot(y)).normalized();
    btVector3 z = x.cross(y).normalized();
    basis = btMatrix3x3(x.x(), y.x(), z.x(), x.y(), y.y(), z.y(), x.z(), y.z(), z.z());
    return btTransform(basis, btVector3(m[0][3], m[1][3], m[2][3]) * Scale);
}
void Matrix(const btTransform &t, rd_matrix m, float scale = 1) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) m[r][c] = t.getBasis()[r][c] * scale;
        m[r][3] = t.getOrigin()[r] / Scale;
    }
}
struct Part {
    int bone, group;
    float radius, waterSurface = 0, waterQueryTime = -1;
    float linearDamping=0.12f,angularDamping=0.35f;
    bool water = false, fittedBoot = false;
    btTransform stepStart;
    btTransform offset; // bone -> centre of collider
    std::unique_ptr<btCollisionShape> shape;
    std::unique_ptr<btRigidBody> body;
};
struct Doll {
    int entity, count;
    int selfPairs = 0;
    float token, scale;
    double age = 0, stable = 0, sampleTime = 0;
    double activeTime = 0; // Continuous solver time; hits cannot postpone the safety limit.
    btVector3 previousPosition[RD_MAX_BODIES];
    btQuaternion previousRotation[RD_MAX_BODIES];
    bool frozen = false, poseDirty = true, boundsDirty = true, prop = false;
    bool held=false;
    btVector3 holdTarget;
    double holdDeadline=0;
    rd_matrix cachedPose[RD_MAX_BONES];
    float mins[3], maxs[3];
    int parent[RD_MAX_BONES], part[RD_MAX_BONES];
    btTransform local[RD_MAX_BONES], initial[RD_MAX_BONES];
    std::vector<Part> parts;
    std::vector<std::unique_ptr<btTypedConstraint>> joints;
    std::unique_ptr<btCompoundShape> frozenShape;
    std::unique_ptr<btRigidBody> frozenBody;
};
struct Mesh {
    int key;
    std::unique_ptr<btTriangleMesh> triangles;
    std::unique_ptr<btBvhTriangleMeshShape> shape;
    std::unique_ptr<btRigidBody> body;
};
struct Physics {
    btDefaultCollisionConfiguration config;
    btCollisionDispatcher dispatcher;
    btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world;
    std::vector<std::unique_ptr<Doll>> dolls;
    std::unordered_map<int,Doll *> byEntity;
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::unique_ptr<Mesh> pending;
    double lastTime = -1, accumulator = 0;
    Physics() : dispatcher(&config), world(&dispatcher, &broadphase, &solver, &config) {
        world.setGravity(btVector3(0, 0, -800 * Scale));
        world.getSolverInfo().m_numIterations = 80;
        world.getSolverInfo().m_splitImpulse = true;
        // Correct shallow penetrations without injecting bounce into resting bodies.
        world.getSolverInfo().m_splitImpulsePenetrationThreshold = -0.002f;
    }
    void removeDoll(size_t i) {
        byEntity.erase(dolls[i]->entity);
        if (dolls[i]->frozen) world.removeRigidBody(dolls[i]->frozenBody.get());
        else {
            for (auto &j : dolls[i]->joints) world.removeConstraint(j.get());
            for (auto &p : dolls[i]->parts) world.removeRigidBody(p.body.get());
        }
        dolls.erase(dolls.begin() + i);
    }
    void removeMesh(size_t i) {
        world.removeRigidBody(meshes[i]->body.get());
        meshes.erase(meshes.begin() + i);
    }
    ~Physics() {
        while (!dolls.empty()) removeDoll(dolls.size() - 1);
        while (!meshes.empty()) removeMesh(meshes.size() - 1);
    }
    Doll *find(int entity) {
        auto entry=byEntity.find(entity);
        return entry==byEntity.end()?nullptr:entry->second;
    }
};
std::unique_ptr<Physics> physics;
rd_water_query waterQuery = nullptr;
Physics &Get() { if (!physics) physics.reset(new Physics); return *physics; }
bool HasName(const char *name, const char *word) {
    char lower[32];
    for (int i = 0; i < 32; ++i) { lower[i] = (name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]; }
    lower[31] = 0;
    return strstr(lower, word) != nullptr;
}
float LengthScale(const rd_matrix m) {
    return std::sqrt(m[0][0]*m[0][0] + m[1][0]*m[1][0] + m[2][0]*m[2][0]);
}
}

extern "C" void RD_Reset(void) { physics.reset(); waterQuery = nullptr; }
extern "C" void RD_SetWaterQuery(rd_water_query query) { waterQuery = query; }
extern "C" void RD_BeginMesh(int key) {
    auto &p = Get();
    p.pending.reset(new Mesh);
    p.pending->key = key;
    p.pending->triangles.reset(new btTriangleMesh(true, false));
}
extern "C" void RD_AddTriangle(const float *a, const float *b, const float *c) {
    auto &p = Get();
    if (p.pending && (Vec(b)-Vec(a)).cross(Vec(c)-Vec(a)).length2() > 1e-10f)
        p.pending->triangles->addTriangle(Vec(a), Vec(b), Vec(c), false);
}
extern "C" void RD_RemoveMesh(int key) {
    if (!physics) return;
    for (size_t i = 0; i < physics->meshes.size(); ++i)
        if (physics->meshes[i]->key == key) { physics->removeMesh(i); return; }
}
extern "C" void RD_EndMesh(const rd_matrix transform, int kinematic) {
    auto &p = Get();
    if (!p.pending || !p.pending->triangles->getNumTriangles()) { p.pending.reset(); return; }
    RD_RemoveMesh(p.pending->key);
    auto &m = *p.pending;
    m.shape.reset(new btBvhTriangleMeshShape(m.triangles.get(), true));
    m.shape->setMargin(0.005f);
    btRigidBody::btRigidBodyConstructionInfo info(0, nullptr, m.shape.get());
    m.body.reset(new btRigidBody(info));
    m.body->setWorldTransform(Transform(transform));
    m.body->setFriction(0.8f);
    if (kinematic) {
        m.body->setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
        m.body->setActivationState(DISABLE_DEACTIVATION);
    }
    p.world.addRigidBody(m.body.get(), WorldGroup, CorpseGroup);
    p.meshes.push_back(std::move(p.pending));
}
static void Wake(Doll &d);
static void Freeze(Doll &d);
static void Buoyancy(Doll &d, float gravity, float dt);
extern "C" void RD_MoveMesh(int key, const rd_matrix transform) {
    if (!physics) return;
    for (auto &m : physics->meshes) if (m->key == key) {
        const btTransform t = Transform(transform);
        const btTransform old = m->body->getWorldTransform();
        if (old == t) return; // Most doors/brushes are stationary; do not wake islands.
        btVector3 a,b,c,e; m->shape->getAabb(old,a,b); m->shape->getAabb(t,c,e);
        a.setMin(c); b.setMax(e);
        for (auto &d : physics->dolls) if (d->frozen) {
            float lo[3],hi[3]; RD_Bounds(d->entity,lo,hi);
            if (a.x()<=hi[0]*Scale && b.x()>=lo[0]*Scale &&
                a.y()<=hi[1]*Scale && b.y()>=lo[1]*Scale &&
                a.z()<=hi[2]*Scale && b.z()>=lo[2]*Scale) Wake(*d);
        }
        m->body->setWorldTransform(t);
        physics->world.updateSingleAabb(m->body.get());
        return;
    }
}
extern "C" void RD_Remove(int entity) {
    if (!physics) return;
    for (size_t i = 0; i < physics->dolls.size(); ++i)
        if (physics->dolls[i]->entity == entity) { physics->removeDoll(i); return; }
}
extern "C" int RD_Has(int entity, float token) {
    if (!physics) return 0;
    Doll *d = physics->find(entity);
    return d && std::fabs(d->token - token) < 0.0005f;
}
extern "C" void RD_FitFootHitboxes(int boneCount,const rd_bone *bones,int vertexCount,
    const float (*vertices)[3],const unsigned char *vertexBones,int boxCount,rd_hitbox *boxes) {
    if(!bones || !vertices || !vertexBones || !boxes || boneCount<1 || boneCount>RD_MAX_BONES || vertexCount<0 || boxCount<0) return;
    for(int b=0;b<boxCount;++b) {
        auto &box=boxes[b];
        if(box.bone<0 || box.bone>=boneCount || (box.group!=6 && box.group!=7) || !HasName(bones[box.bone].name,"foot")) continue;
        // Studio reference vertices are already in their weighted bone's local
        // space. Keep authored coverage, extending it to enclose the actual boot.
        for(int v=0;v<vertexCount;++v) if(vertexBones[v]==box.bone)
            for(int a=0;a<3;++a) if(std::isfinite(vertices[v][a])) {
                box.mins[a]=std::min(box.mins[a],vertices[v][a]);
                box.maxs[a]=std::max(box.maxs[a],vertices[v][a]);
            }
    }
}
static int CreateDoll(int entity, float token, int count, const rd_bone *bones,
    int hitboxCount, const rd_hitbox *hitboxes, const rd_matrix *pose, const float *velocity, int hitgroup, bool isProp) {
    if (count < 1 || count > RD_MAX_BONES || hitboxCount < 1 || hitboxCount > 256 || !std::isfinite(token)) return 0;
    // Reject cycles, invalid transforms and malformed MDLs before allocating bodies.
    for (int i = 0; i < count; ++i) {
        if (bones[i].parent < -1 || bones[i].parent >= i) return 0;
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
            if (!std::isfinite(pose[i][r][c])) return 0;
        if (LengthScale(pose[i]) < 0.01f) return 0;
        const btVector3 x(pose[i][0][0], pose[i][1][0], pose[i][2][0]);
        const btVector3 y(pose[i][0][1], pose[i][1][1], pose[i][2][1]);
        if (x.cross(y).length2() < 1e-8f) return 0;
    }
    for (int i = 0; i < 3; ++i) if (!std::isfinite(velocity[i])) return 0;
    auto &p = Get();
    if (RD_Has(entity, token)) return 1;
    RD_Remove(entity);
    int used=0;for(const auto &existing:p.dolls) if(existing->prop==isProp) ++used;
    if(used >= (isProp?RD_MAX_PROPS:RD_MAX_CORPSES)) return 0;
    std::unique_ptr<Doll> d(new Doll);
    d->entity = entity; d->token = token; d->count = count;d->prop=isProp;
    d->scale = std::max(0.1f, std::min(8.0f, LengthScale(pose[0])));
    btVector3 lo[RD_MAX_BONES], hi[RD_MAX_BONES];
    int group[RD_MAX_BONES] = {};
    bool have[RD_MAX_BONES] = {};
    for (int i = 0; i < count; ++i) {
        d->parent[i] = bones[i].parent; d->part[i] = -1;
        d->initial[i] = Transform(pose[i]);
        d->local[i] = bones[i].parent < 0 ? d->initial[i] : d->initial[bones[i].parent].inverse() * d->initial[i];
    }
    for (int i = 0; i < hitboxCount; ++i) {
        const auto &h = hitboxes[i];
        if (h.bone < 0 || h.bone >= count) return 0;
        for (int a = 0; a < 3; ++a)
            if (!std::isfinite(h.mins[a]) || !std::isfinite(h.maxs[a]) || h.maxs[a] <= h.mins[a]) return 0;
        // Weapons/accessories follow their owner bone instead of becoming giant colliders.
        if (HasName(bones[h.bone].name, "weapon") || HasName(bones[h.bone].name, "gun")) continue;
        btVector3 a = Vec(h.mins) * d->scale, b = Vec(h.maxs) * d->scale;
        if (!have[h.bone]) { lo[h.bone] = a; hi[h.bone] = b; group[h.bone] = h.group; }
        else { lo[h.bone].setMin(a); hi[h.bone].setMax(b); }
        have[h.bone] = true;
    }
    bool oversized=false;
    for(int i=0;i<count;++i) if(have[i]) {
        const btVector3 half=(hi[i]-lo[i])*0.5f;
        const int axis=half.maxAxis();
        if(std::min(half[(axis+1)%3],half[(axis+2)%3])*0.85f>0.35f) oversized=true;
    }
    for (int i = 0; i < count; ++i) if (have[i]) {
        if (d->parts.size() == RD_MAX_BODIES) return 0;
        Part part;
        part.bone = i; part.group = group[i];
        const btVector3 half = (hi[i] - lo[i]) * 0.5f;
        const btVector3 centre = (hi[i] + lo[i]) * 0.5f;
        // Capsules fit limbs and avoid snagging box corners on steps. Hitboxes
        // are authored on every shipped monster, so no hard-coded bone indices.
        int axis = half.maxAxis();
        float radius = std::max(0.025f, std::min(half[(axis+1)%3], half[(axis+2)%3]) * 0.85f);
        part.radius = radius;
        float height = std::max(0.0f, 2.0f * (half[axis] - radius));
        // Broad trunks need a contact patch rather than a single rounded
        // capsule contact (notably the enlarged megasquid torso and tail).
        const bool contactPatch=!oversized && (HasName(bones[i].name,"bip01") || HasName(bones[i].name,"bip02")) &&
            (HasName(bones[i].name,"spine") || HasName(bones[i].name,"pelvis") || HasName(bones[i].name,"foot"));
        const bool footPatch=contactPatch && HasName(bones[i].name,"foot");
        const bool fittedBoot=footPatch && (part.group==6 || part.group==7);
        part.fittedBoot=fittedBoot;
        if (radius > 0.35f || contactPatch) {
            // Full mesh-fitted foot coverage, including diagonal sole/toe
            // contacts. Torso hitboxes still shrink to accommodate overlap.
            part.shape.reset(new btBoxShape(fittedBoot?half+btVector3(0.002f,0.002f,0.002f):half*(footPatch?0.95f:0.85f)));
            part.shape->setMargin(fittedBoot?0.003f:0.01f);
        } else if (axis == 0) part.shape.reset(new btCapsuleShapeX(radius, height));
        else if (axis == 2) part.shape.reset(new btCapsuleShapeZ(radius, height));
        else part.shape.reset(new btCapsuleShape(radius, height));
        part.offset.setIdentity(); part.offset.setOrigin(centre);
        const float mass = std::max(2.0f, std::min(8.0f, half.x()*half.y()*half.z()*8.0f*700.0f));
        btVector3 inertia;
        part.shape->calculateLocalInertia(mass, inertia);
        btRigidBody::btRigidBodyConstructionInfo info(mass, nullptr, part.shape.get(), inertia);
        info.m_additionalDamping=!oversized;
        info.m_additionalDampingFactor=0.25f;
        info.m_additionalLinearDampingThresholdSqr=0.0025f;
        info.m_additionalAngularDampingThresholdSqr=0.01f;
        part.body.reset(new btRigidBody(info));
        part.body->setUserIndex(entity);
        part.body->setUserIndex2(group[i]);
        const btTransform t = d->initial[i] * part.offset;
        part.body->setWorldTransform(t);
        part.body->setInterpolationWorldTransform(t);
        part.body->setDamping(0.12f, 0.35f);
        part.body->setFriction(oversized?0.8f:0.85f); part.body->setRestitution(oversized?0.05f:0.0f);
        part.body->setRollingFriction(oversized?0.0f:0.025f);
        part.body->setSpinningFriction(oversized?0.0f:0.025f);
        part.body->setSleepingThresholds(0.12f, 0.18f);
        part.body->setCcdMotionThreshold(footPatch?0.005f:radius * 0.5f);
        part.body->setCcdSweptSphereRadius(footPatch?std::min(half.x(),std::min(half.y(),half.z()))*0.85f:radius * 0.8f);
        btVector3 v = Vec(velocity);
        if (v.length() > 15) v *= 15 / v.length();
        part.body->setLinearVelocity(v);
        // A small deterministic tumble prevents balanced standing corpses.
        part.body->setAngularVelocity(btVector3(v.y(), -v.x(), 0) * 0.35f + btVector3(0.4f, 0.7f, 0.2f));
        if (group[i] == hitgroup) part.body->setAngularVelocity(part.body->getAngularVelocity() * 1.8f);
        d->previousPosition[d->parts.size()] = t.getOrigin();
        d->previousRotation[d->parts.size()] = t.getRotation();
        d->part[i] = (int)d->parts.size();
        d->parts.push_back(std::move(part));
    }
    if (d->parts.empty()) return 0;
    // Bind each collider to its nearest physical ancestor. Skeleton roots and
    // intermediate bones without hitboxes retain their captured local pose.
    int root = 0;
    for (size_t i = 1; i < d->parts.size(); ++i) {
        int a = bones[d->parts[i].bone].parent;
        while (a >= 0 && d->part[a] < 0) a = bones[a].parent;
        int parentPart = a < 0 ? root : d->part[a];
        auto &child = d->parts[i]; auto &parent = d->parts[parentPart];
        btTransform joint = d->initial[child.bone];
        // Both frames start aligned: constraints never snap the death pose to a bind pose.
        btTransform frameA = parent.body->getWorldTransform().inverse() * joint;
        btTransform frameB = child.body->getWorldTransform().inverse() * joint;
        auto cone = std::unique_ptr<btConeTwistConstraint>(new btConeTwistConstraint(*parent.body, *child.body, frameA, frameB));
        float swing = 0.8f, twist = 0.35f;
        if (HasName(bones[child.bone].name, "spine") || HasName(bones[child.bone].name, "neck")) { swing = 0.25f; twist = 0.2f; }
        else if (HasName(bones[child.bone].name, "head")) { swing = 0.45f; twist = 0.4f; }
        else if (HasName(bones[child.bone].name, "forearm") || HasName(bones[child.bone].name, "calf") ||
            HasName(bones[child.bone].name,"arm2") || HasName(bones[child.bone].name,"leg1")) { swing = 1.1f; twist = 0.12f; }
        else if (HasName(bones[child.bone].name, "upperarm") || HasName(bones[child.bone].name, "thigh") ||
            HasName(bones[child.bone].name,"arm1")) { swing = 1.2f; twist = 0.6f; }
        else if(HasName(bones[child.bone].name,"foot")) { swing=0.35f;twist=0.15f; }
        else if(HasName(bones[child.bone].name,"hand")) { swing=0.4f;twist=0.25f; }
        // Oversized creature trunks/tails cannot fold onto terrain under
        // human spine limits. Give these assemblies a free ball joint.
        if (child.radius > 0.35f || parent.radius > 0.35f) { swing = 3.0f; twist = 3.0f; }
        cone->setLimit(swing, swing, twist, 0.9f, 0.3f, 1.0f);
        cone->setDamping(oversized?0.15f:0.3f);
        // Bullet's default axis here selects angular limit correction, not
        // attachment correction. The old 0.8 was injecting angular energy.
        cone->setParam(BT_CONSTRAINT_ERP, oversized ? 0.8f : 0.1f);
        if(!oversized) for(int axis=0;axis<3;++axis) cone->setParam(BT_CONSTRAINT_ERP,0.35f,axis);
        d->joints.push_back(std::move(cone));
    }
    // Exclude neighbouring joints and overlapping captured legacy colliders.
    // Separated limbs still collide with the torso and each other.
    // Oversized legacy creature hitboxes are not anatomical limbs; retain their
    // previous collision policy until they have purpose-built physics proxies.
    for (auto &part : d->parts) p.world.addRigidBody(part.body.get(), CorpseGroup, WorldGroup | CorpseGroup);
    for (size_t i = 0; i < d->parts.size(); ++i)
        for (size_t j = i+1; j < d->parts.size(); ++j) {
            int a=d->parts[i].bone,b=d->parts[j].bone;
            int distance=999;
            for(int x=a,dx=0;x>=0;x=bones[x].parent,++dx)
                for(int y=b,dy=0;y>=0;y=bones[y].parent,++dy)
                    if(x==y) distance=std::min(distance,dx+dy);
            struct Overlap : btCollisionWorld::ContactResultCallback {
                bool penetrating=false;
                btScalar addSingleResult(btManifoldPoint &point,const btCollisionObjectWrapper *,int,int,
                    const btCollisionObjectWrapper *,int,int) override {
                    if(point.getDistance() < -0.003f) penetrating=true;
                    return 0;
                }
            } overlap;
            p.world.contactPairTest(d->parts[i].body.get(),d->parts[j].body.get(),overlap);
            const int groupA=d->parts[i].group,groupB=d->parts[j].group;
            const bool torsoA=groupA==2 || groupA==3,torsoB=groupB==2 || groupB==3;
            // Legacy spine segments and each limb's internal hitboxes form
            // one anatomical region. Their constraints cannot resolve contacts
            // inside that region; cross-region limb/torso contacts remain active.
            const bool sameRegion=(groupA>0 && groupA==groupB) || (torsoA && torsoB) ||
                (groupA==1 && groupB==2) || (groupB==1 && groupA==2);
            if(oversized || sameRegion || distance<=2 || overlap.penetrating)
                d->parts[i].body->setIgnoreCollisionCheck(d->parts[j].body.get(), true);
            else ++d->selfPairs;
        }
    for (auto &j : d->joints) p.world.addConstraint(j.get(), true);
    p.byEntity[d->entity]=d.get();
    p.dolls.push_back(std::move(d));
    return 1;
}
extern "C" int RD_SelfCollisionPairs(int entity) {
    Doll *d=physics?physics->find(entity):nullptr;return d?d->selfPairs:0;
}
extern "C" float RD_SelfPenetration(int entity) {
    Doll *d=physics?physics->find(entity):nullptr;if(!d) return 0;
    struct Depth : btCollisionWorld::ContactResultCallback {
        float deepest=0;
        btScalar addSingleResult(btManifoldPoint &point,const btCollisionObjectWrapper *,int,int,
            const btCollisionObjectWrapper *,int,int) override {
            deepest=std::max(deepest,float(-point.getDistance()/Scale));return 0;
        }
    } depth;
    for(size_t i=0;i<d->parts.size();++i) for(size_t j=i+1;j<d->parts.size();++j) {
        auto *a=d->parts[i].body.get(),*b=d->parts[j].body.get();
        if(a->checkCollideWith(b) && b->checkCollideWith(a)) physics->world.contactPairTest(a,b,depth);
    }
    return depth.deepest;
}
extern "C" int RD_Create(int entity,float token,int count,const rd_bone *bones,
    int hitboxCount,const rd_hitbox *hitboxes,const rd_matrix *pose,const float *velocity,int hitgroup) {
    return CreateDoll(entity,token,count,bones,hitboxCount,hitboxes,pose,velocity,hitgroup,false);
}
extern "C" int RD_CreateProp(int entity,float token,int count,const rd_bone *bones,
    const rd_matrix *pose,int pointCount,const float (*points)[3],
    const float *velocity,const float *angularDegrees) {
    if(!points || pointCount<4 || pointCount>8192 || !angularDegrees) return 0;
    rd_hitbox box={};box.bone=0;box.group=0;
    for(int a=0;a<3;++a) {
        if(!std::isfinite(angularDegrees[a])) return 0;
        box.mins[a]=box.maxs[a]=points[0][a];
    }
    for(int i=0;i<pointCount;++i) for(int a=0;a<3;++a) {
        if(!std::isfinite(points[i][a])) return 0;
        box.mins[a]=std::min(box.mins[a],points[i][a]);
        box.maxs[a]=std::max(box.maxs[a],points[i][a]);
    }
    if(RD_Has(entity,token)) return 1;
    if(!CreateDoll(entity,token,count,bones,1,&box,pose,velocity,0,true)) return 0;
    auto &p=*physics;auto &part=p.find(entity)->parts[0];
    p.world.removeRigidBody(part.body.get());
    std::unique_ptr<btConvexHullShape> hull(new btConvexHullShape);
    const btVector3 centre=part.offset.getOrigin();
    for(int i=0;i<pointCount;++i) hull->addPoint(Vec(points[i])-centre,false);
    hull->recalcLocalAabb();hull->optimizeConvexHull();hull->setMargin(0.005f);
    part.body->setCollisionShape(hull.get());part.shape=std::move(hull);
    const float mass=4.0f;btVector3 inertia;part.shape->calculateLocalInertia(mass,inertia);
    part.body->setMassProps(mass,inertia);part.body->updateInertiaTensor();
    part.body->setAngularVelocity(btVector3(angularDegrees[0],angularDegrees[1],angularDegrees[2])*(SIMD_PI/180));
    part.body->setDamping(0.04f,0.12f);
    part.linearDamping=0.04f;part.angularDamping=0.12f;
    part.body->setSleepingThresholds(0.05f,0.08f);
    // No actor/player bodies enter this world: props touch world brushes and corpses only.
    p.world.addRigidBody(part.body.get(),CorpseGroup,WorldGroup|CorpseGroup);
    return 1;
}
extern "C" void RD_Step(double time, float gravity) {
    if (!physics || !std::isfinite(time)) return;
    auto &p = *physics;
    if (p.lastTime < 0 || time < p.lastTime) { p.lastTime = time; p.accumulator = 0; return; }
    double dt = std::min(0.1, time - p.lastTime);
    p.lastTime = time;
    if (dt <= 0) return;
    if (!std::isfinite(gravity)) gravity = 800;
    p.world.setGravity(btVector3(0, 0, -std::max(0.0f, gravity) * Scale));
    const int activeBodies=RD_ActiveBodies();
    if (!activeBodies) {
        p.accumulator = 0;
        for (auto &m : p.meshes) m->body->setInterpolationWorldTransform(m->body->getWorldTransform());
        return;
    }
    p.accumulator += dt;
    // Preserve the accepted small-scene solver. Bound the repeated constraint
    // work in crowded scenes while retaining full-rate contacts and boot CCD.
    p.world.getSolverInfo().m_numIterations=activeBodies>240?32:activeBodies>96?48:80;
    const double step = 1.0 / 120.0;
    while (p.accumulator + 1e-9 >= step) {
        for (auto &d : p.dolls) if (!d->frozen) {
            Buoyancy(*d, gravity, (float)step);
            if(d->held) {
                if(time>d->holdDeadline) d->held=false;
                else {
                    auto &part=d->parts[0];auto &body=*part.body;
                    const btVector3 target=d->holdTarget;
                    const btVector3 delta=target-body.getWorldTransform().getOrigin();
                    if(delta.length2()>std::pow(96*Scale,2)) d->held=false;
                    else {
                        // Reach the camera target in one physics tick when clear.
                        // Contacts and CCD resolve walls before the body can move.
                        btVector3 velocity=delta/btScalar(step);
                        if(velocity.length()>25)velocity*=25/velocity.length();
                        body.setLinearVelocity(velocity);
                        body.setAngularVelocity(btVector3(0,0,0));
                        body.activate(true);d->age=d->stable=d->activeTime=0;
                    }
                }
                if(!d->held)d->parts[0].body->setCcdMotionThreshold(0);
            }
            for(auto &part:d->parts) if(part.fittedBoot) part.stepStart=part.body->getWorldTransform();
        }
        p.world.stepSimulation((btScalar)step, 0);
        for (auto &d : p.dolls) if (!d->frozen) {
            // Bullet's built-in CCD sweeps an inscribed sphere and misses the
            // toe/sole of a rotating boot. Sweep the full box against brushes.
            for(auto &part:d->parts) if(part.fittedBoot) {
                auto &body=*part.body;const btTransform end=body.getWorldTransform();
                if((end.getOrigin()-part.stepStart.getOrigin()).length2()<1e-8f &&
                    std::fabs(end.getRotation().dot(part.stepStart.getRotation()))>0.999999f) continue;
                btCollisionWorld::ClosestConvexResultCallback hit(part.stepStart.getOrigin(),end.getOrigin());
                hit.m_collisionFilterGroup=CorpseGroup;hit.m_collisionFilterMask=WorldGroup;
                p.world.convexSweepTest(static_cast<btConvexShape *>(part.shape.get()),part.stepStart,end,hit);
                if(hit.hasHit() && hit.m_closestHitFraction<1 && hit.m_hitNormalWorld.z()>0.5f) {
                    // Correct only motion through the brush plane. Keeping
                    // tangential motion/rotation avoids pinning the ankle while
                    // the joint solver is trying to move the rest of the leg.
                    const auto *shape=static_cast<btConvexShape *>(part.shape.get());
                    const btVector3 support=end*shape->localGetSupportingVertex(end.getBasis().transpose()*-hit.m_hitNormalWorld);
                    const btScalar depth=(hit.m_hitPointWorld-support).dot(hit.m_hitNormalWorld);
                    if(depth<=0) continue;
                    btTransform safe=end;safe.getOrigin()+=hit.m_hitNormalWorld*(depth+0.0001f);
                    body.setWorldTransform(safe);body.setInterpolationWorldTransform(safe);
                    btVector3 velocity=body.getLinearVelocity();const btScalar inward=velocity.dot(hit.m_hitNormalWorld);
                    if(inward<0) body.setLinearVelocity(velocity-hit.m_hitNormalWorld*inward);
                    p.world.updateSingleAabb(&body);
                }
            }
            d->age += step; d->poseDirty = d->boundsDirty = true;
            d->activeTime += step;
            if(d->activeTime>=60.0) { Freeze(*d);continue; }
            d->sampleTime += step;
            if (d->sampleTime >= 0.25) {
                bool quiet = true;
                for (size_t i=0; i<d->parts.size(); ++i) {
                    const auto &t = d->parts[i].body->getWorldTransform();
                    const float travel = (t.getOrigin()-d->previousPosition[i]).length2();
                    const float rotation = std::fabs(t.getRotation().dot(d->previousRotation[i]));
                    if (travel > 0.000025f || rotation < 0.99996f) quiet = false;
                    d->previousPosition[i]=t.getOrigin(); d->previousRotation[i]=t.getRotation();
                }
                // Constraint correction can leave nonzero velocities on a
                // visibly settled legacy rig. Measure actual pose motion.
                d->stable = quiet ? d->stable+d->sampleTime : 0;
                d->sampleTime=0;
                if(d->age>2 && d->stable>1.0) Freeze(*d);
            }
        }
        p.accumulator -= step;
    }
}
extern "C" int RD_Pose(int entity, rd_matrix *pose, int count) {
    Doll *d = physics ? physics->find(entity) : nullptr;
    if (!d || count != d->count) return 0;
    if (!d->poseDirty) { std::memcpy(pose, d->cachedPose, count*sizeof(rd_matrix)); return 1; }
    btTransform result[RD_MAX_BONES];
    // Root bones lacking a collider must travel with the physical root.
    auto &root = d->parts[0];
    const btTransform rootDelta = root.body->getWorldTransform() * root.offset.inverse() * d->initial[root.bone].inverse();
    for (int i = 0; i < count; ++i) {
        int part = d->part[i];
        if (part >= 0) result[i] = d->parts[part].body->getWorldTransform() * d->parts[part].offset.inverse();
        else if (d->parent[i] < 0) result[i] = rootDelta * d->initial[i];
        else result[i] = result[d->parent[i]] * d->local[i];
        Matrix(result[i], pose[i], d->scale);
    }
    std::memcpy(d->cachedPose, pose, count*sizeof(rd_matrix)); d->poseDirty = false;
    return 1;
}
extern "C" int RD_Bounds(int entity, float *mins, float *maxs) {
    Doll *d = physics ? physics->find(entity) : nullptr;
    if (!d) return 0;
    if (!d->boundsDirty) { std::memcpy(mins,d->mins,12); std::memcpy(maxs,d->maxs,12); return 1; }
    btVector3 lo(1e10f,1e10f,1e10f), hi(-1e10f,-1e10f,-1e10f);
    for (auto &p : d->parts) {
        // Capsule getAabb encloses a rotated box, which can extend far below
        // the real surface on long diagonal tentacles. Use convex support
        // points for tight world-axis bounds instead.
        auto *shape = static_cast<btConvexShape *>(p.shape.get());
        const btTransform &t = p.body->getWorldTransform();
        for (int axis = 0; axis < 3; ++axis) {
            btVector3 direction(0, 0, 0); direction[axis] = 1;
            const btVector3 local = t.getBasis().transpose() * direction;
            const btVector3 a = t * shape->localGetSupportingVertex(-local);
            const btVector3 b = t * shape->localGetSupportingVertex(local);
            lo[axis] = std::min(lo[axis], a[axis]); hi[axis] = std::max(hi[axis], b[axis]);
        }
    }
    for (int i = 0; i < 3; ++i) { mins[i] = lo[i]/Scale - 16; maxs[i] = hi[i]/Scale + 16; }
    std::memcpy(d->mins,mins,12); std::memcpy(d->maxs,maxs,12); d->boundsDirty = false;
    return 1;
}
extern "C" int RD_Count(void) { return physics ? (int)physics->dolls.size() : 0; }
extern "C" int RD_CorpseCount(void) {
    int count=0;if(physics) for(const auto &d:physics->dolls) if(!d->prop) ++count;
    return count;
}
extern "C" int RD_BodyCount(int entity) { Doll *d = physics ? physics->find(entity) : nullptr; return d ? (int)d->parts.size() : 0; }
extern "C" float RD_JointError(int entity) {
    Doll *d = physics ? physics->find(entity) : nullptr;
    float error = 0;
    if (d) for (auto &j : d->joints) {
        auto *c = static_cast<btConeTwistConstraint *>(j.get());
        btVector3 a = c->getRigidBodyA().getWorldTransform() * c->getAFrame().getOrigin();
        btVector3 b = c->getRigidBodyB().getWorldTransform() * c->getBFrame().getOrigin();
        error = std::max(error, (float)(a-b).length()/Scale);
    }
    return error;
}

// Settled corpses retain their exact pose and one static compound collider.
// Their limb bodies and constraints leave the dynamics solver completely.
static void Freeze(Doll &d) {
    if (d.frozen || !physics) return;
    float lo[3],hi[3]; RD_Bounds(d.entity,lo,hi);
    btTransform root; root.setIdentity(); root.setOrigin((Vec(lo)+Vec(hi))*0.5f);
    d.frozenShape.reset(new btCompoundShape);
    for (auto &p : d.parts) {
        d.frozenShape->addChildShape(root.inverse()*p.body->getWorldTransform(), p.shape.get());
        p.body->setLinearVelocity(btVector3(0,0,0)); p.body->setAngularVelocity(btVector3(0,0,0));
    }
    for (auto &j : d.joints) physics->world.removeConstraint(j.get());
    for (auto &p : d.parts) physics->world.removeRigidBody(p.body.get());
    btRigidBody::btRigidBodyConstructionInfo info(0,nullptr,d.frozenShape.get());
    d.frozenBody.reset(new btRigidBody(info)); d.frozenBody->setWorldTransform(root);
    d.frozenBody->setUserIndex(d.entity); d.frozenBody->setUserIndex2(-1);
    d.frozenBody->setFriction(0.8f);
    physics->world.addRigidBody(d.frozenBody.get(),CorpseGroup,CorpseGroup);
    d.frozen = true;
}
static void Wake(Doll &d) {
    if (!d.frozen || !physics) return;
    physics->world.removeRigidBody(d.frozenBody.get());
    d.frozenBody.reset(); d.frozenShape.reset();
    for (auto &p : d.parts) {
        physics->world.addRigidBody(p.body.get(),CorpseGroup,WorldGroup|CorpseGroup);
        p.body->activate(true); p.waterQueryTime = -1;
    }
    for (auto &j : d.joints) physics->world.addConstraint(j.get(),true);
    d.frozen = false; d.age = d.stable = d.sampleTime = 0;
    d.activeTime=0;
    for(size_t i=0;i<d.parts.size();++i) {
        d.previousPosition[i]=d.parts[i].body->getWorldTransform().getOrigin();
        d.previousRotation[i]=d.parts[i].body->getWorldTransform().getRotation();
    }
}
extern "C" void RD_FreezeAll(double time) {
    if(!physics) return;
    for(auto &d:physics->dolls) {d->held=false;Freeze(*d);}
    physics->lastTime=time;physics->accumulator=0;
}
extern "C" int RD_HoldProp(int entity,float token,const float *target) {
    Doll *d=physics?physics->find(entity):nullptr;
    if(!d || !d->prop || d->token!=token || !target)return 0;
    for(int i=0;i<3;++i)if(!std::isfinite(target[i]))return 0;
    Wake(*d);d->held=true;d->holdTarget=Vec(target);d->holdDeadline=physics->lastTime+0.25;
    d->parts[0].body->setCcdMotionThreshold(0.01f);
    d->parts[0].body->setCcdSweptSphereRadius(0.025f);
    return 1;
}
extern "C" int RD_ThrowProp(int entity,float token,const float *velocity) {
    Doll *d=physics?physics->find(entity):nullptr;
    if(!d || !d->prop || d->token!=token || !velocity)return 0;
    for(int i=0;i<3;++i)if(!std::isfinite(velocity[i]))return 0;
    d->held=false;Wake(*d);d->age=d->stable=d->activeTime=0;
    d->parts[0].body->setCcdMotionThreshold(0);
    btVector3 v=Vec(velocity);if(v.length()>16)v*=16/v.length();
    d->parts[0].body->setLinearVelocity(v);
    d->parts[0].body->setAngularVelocity(v.fuzzyZero()?btVector3(0,0,0):btVector3(2,3,1));
    return 1;
}
static void Buoyancy(Doll &d, float gravity, float dt) {
    (void)dt;
    if (!waterQuery) return;
    for (auto &p : d.parts) {
        auto &b = *p.body;
        btVector3 lo,hi; p.shape->getAabb(b.getWorldTransform(),lo,hi);
        if (p.waterQueryTime < 0 || d.age - p.waterQueryTime >= 0.1) {
            float bottom[3] = {b.getWorldTransform().getOrigin().x()/Scale,
                b.getWorldTransform().getOrigin().y()/Scale,lo.z()/Scale+0.1f};
            p.water = waterQuery(bottom,&p.waterSurface)!=0;
            p.waterQueryTime = (float)d.age;
            b.setDamping(p.water?0.55f:p.linearDamping,p.water?0.65f:p.angularDamping);
        }
        if (!p.water) continue;
        const float fraction = std::max(0.0f,std::min(1.0f,
            (p.waterSurface*Scale-lo.z())/std::max(0.01f,(float)(hi.z()-lo.z()))));
        const float mass = 1.0f/b.getInvMass();
        // Slightly less dense than water, so the corpse settles at the surface.
        b.applyCentralForce(btVector3(0,0,mass*gravity*Scale*1.15f*fraction));
        b.applyCentralForce(-b.getLinearVelocity()*mass*2.5f*fraction);
    }
}
extern "C" int RD_Frozen(int entity) {
    Doll *d=physics?physics->find(entity):nullptr; return d && d->frozen;
}
extern "C" int RD_RemoveGroup(int entity, int group) {
    Doll *d=physics?physics->find(entity):nullptr;
    if (!d || group<=0) return 0;
    int removed=0;
    for(auto &p:d->parts) if(p.group==group) ++removed;
    if (!removed || removed==(int)d->parts.size()) return 0;
    rd_matrix pose[RD_MAX_BONES]; RD_Pose(entity,pose,d->count);
    const bool frozen=d->frozen;
    Wake(*d);
    // Constraints must leave the world before either connected body is freed.
    for(auto it=d->joints.begin();it!=d->joints.end();) {
        bool cut=false;
        for(auto &p:d->parts) if(p.group==group &&
            (&(*it)->getRigidBodyA()==p.body.get() || &(*it)->getRigidBodyB()==p.body.get())) cut=true;
        if(cut) { physics->world.removeConstraint(it->get()); it=d->joints.erase(it); }
        else ++it;
    }
    for(auto it=d->parts.begin();it!=d->parts.end();) {
        if(it->group==group) {
            const int bone=it->bone, parent=d->parent[bone];
            d->local[bone]=parent<0?Transform(pose[bone]):Transform(pose[parent]).inverse()*Transform(pose[bone]);
            physics->world.removeRigidBody(it->body.get()); it=d->parts.erase(it);
        } else ++it;
    }
    std::fill(d->part,d->part+d->count,-1);
    for(size_t i=0;i<d->parts.size();++i) d->part[d->parts[i].bone]=(int)i;
    d->poseDirty=d->boundsDirty=true;
    if(frozen) Freeze(*d);
    return removed;
}
extern "C" int RD_ActiveBodies(void) {
    int count=0; if(physics) for(auto &d:physics->dolls) if(!d->frozen) count+=(int)d->parts.size(); return count;
}
extern "C" int RD_Trace(const float *start,const float *end,float limit,rd_hit *hit) {
    if (!physics || !hit || limit<=0) return 0;
    btVector3 from=Vec(start), to=from+(Vec(end)-from)*std::min(1.0f,limit);
    if ((to-from).length2()<1e-10f) return 0;
    btCollisionWorld::ClosestRayResultCallback ray(from,to);
    ray.m_collisionFilterGroup=CorpseGroup; ray.m_collisionFilterMask=CorpseGroup;
    physics->world.rayTest(from,to,ray);
    if(!ray.hasHit()) return 0;
    hit->entity=ray.m_collisionObject->getUserIndex();
    hit->group=ray.m_collisionObject->getUserIndex2();
    // Compound static corpses still report the exact limb/hitgroup.
    if(hit->group<0) {
        Doll *d=physics->find(hit->entity);
        btTransform a,b; a.setIdentity(); b.setIdentity(); a.setOrigin(from); b.setOrigin(to);
        btScalar closest=1;
        for(auto &p:d->parts) {
            btCollisionWorld::ClosestRayResultCallback partRay(from,to);
            btCollisionWorld::rayTestSingle(a,b,p.body.get(),p.shape.get(),p.body->getWorldTransform(),partRay);
            if(partRay.hasHit() && partRay.m_closestHitFraction<closest) {
                closest=partRay.m_closestHitFraction; hit->group=p.group;
            }
        }
    }
    hit->fraction=ray.m_closestHitFraction*std::min(1.0f,limit);
    for(int i=0;i<3;++i) { hit->point[i]=ray.m_hitPointWorld[i]/Scale; hit->normal[i]=ray.m_hitNormalWorld[i]; }
    return 1;
}
extern "C" void RD_Impulse(int entity,const float *point,const float *impulse) {
    Doll *d=physics?physics->find(entity):nullptr; if(!d) return;
    for(int i=0;i<3;++i) if(!std::isfinite(point[i]) || !std::isfinite(impulse[i])) return;
    Wake(*d); d->age=d->stable=0;
    // Apply at the hit point to the nearest collider surface, rather than its centre.
    Part *best=nullptr; float distance=1e30f;
    for(auto &p:d->parts) {
        btVector3 lo,hi; p.shape->getAabb(p.body->getWorldTransform(),lo,hi);
        btVector3 v=Vec(point), nearest=v; nearest.setMax(lo); nearest.setMin(hi);
        float x=(v-nearest).length2();
        if(x<distance) { distance=x; best=&p; }
        p.body->activate(true); p.waterQueryTime=-1;
    }
    btVector3 kick=Vec(impulse); if(kick.length()>12) kick*=12/kick.length();
    // Input is a velocity kick in GoldSrc units. Mass scales momentum consistently.
    best->body->applyImpulse(kick/best->body->getInvMass(),Vec(point)-best->body->getWorldTransform().getOrigin());
}
extern "C" int RD_Capture(int entity,rd_snapshot *state) {
    Doll *d=physics?physics->find(entity):nullptr; if(!d || !state) return 0;
    std::memset(state,0,sizeof(*state)); state->count=d->count; state->frozen=d->frozen?1:0;
    RD_Pose(entity,state->pose,d->count);
    for(int i=0;i<d->count;++i) Matrix(d->initial[i],state->initial[i],d->scale);
    for(auto &p:d->parts) for(int a=0;a<3;++a) {
        state->linear[p.bone][a]=p.body->getLinearVelocity()[a]/Scale;
        state->angular[p.bone][a]=p.body->getAngularVelocity()[a];
    }
    return 1;
}
extern "C" int RD_Restore(int entity,float token,const rd_bone *bones,
    int hitboxCount,const rd_hitbox *hitboxes,const rd_snapshot *state) {
    if(!state || state->count<1 || state->count>RD_MAX_BONES || (state->frozen!=0 && state->frozen!=1)) return 0;
    for(int i=0;i<state->count;++i) {
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) if(!std::isfinite(state->pose[i][r][c])) return 0;
        const auto &m=state->pose[i]; btVector3 x(m[0][0],m[1][0],m[2][0]),y(m[0][1],m[1][1],m[2][1]);
        if(x.cross(y).length2()<1e-8f) return 0;
        for(int a=0;a<3;++a) if(!std::isfinite(state->linear[i][a]) || !std::isfinite(state->angular[i][a]) ||
            std::fabs(state->linear[i][a])>4096 || std::fabs(state->angular[i][a])>1000) return 0;
    }
    const float zero[3]={}; RD_Remove(entity);
    if(!RD_Create(entity,token,state->count,bones,hitboxCount,hitboxes,state->initial,zero,0)) return 0;
    Doll &d=*physics->find(entity);
    for(auto &p:d.parts) {
        const btTransform t=Transform(state->pose[p.bone])*p.offset;
        p.body->setWorldTransform(t); p.body->setInterpolationWorldTransform(t);
        p.body->setLinearVelocity(Vec(state->linear[p.bone]));
        p.body->setAngularVelocity(btVector3(state->angular[p.bone][0],state->angular[p.bone][1],state->angular[p.bone][2]));
        physics->world.updateSingleAabb(p.body.get());
    }
    if(state->frozen) Freeze(d);
    return 1;
}
extern "C" void RD_RestoreLegacy(int entity) {
    Doll *d=physics?physics->find(entity):nullptr; if(!d) return;
    float lo[3],hi[3]; RD_Bounds(entity,lo,hi);
    // Older saves have only the final death animation. Do not reapply a lethal
    // impulse to this prone pose; lift its collider assembly out of the floor.
    const float x=(lo[0]+hi[0])*0.5f,y=(lo[1]+hi[1])*0.5f;
    btVector3 from(x*Scale,y*Scale,(hi[2]+64)*Scale),to(x*Scale,y*Scale,(lo[2]-256)*Scale);
    btCollisionWorld::ClosestRayResultCallback ray(from,to);
    ray.m_collisionFilterGroup=CorpseGroup; ray.m_collisionFilterMask=WorldGroup;
    physics->world.rayTest(from,to,ray);
    if(ray.hasHit() && ray.m_hitNormalWorld.z()>0.5f) {
        const float lift=ray.m_hitPointWorld.z()/Scale-(lo[2]+16)+0.5f;
        if(lift>0 && lift<256) for(auto &p:d->parts) {
            btTransform t=p.body->getWorldTransform(); t.getOrigin().setZ(t.getOrigin().z()+lift*Scale);
            p.body->setWorldTransform(t); p.body->setInterpolationWorldTransform(t);
            physics->world.updateSingleAabb(p.body.get());
        }
    }
    d->poseDirty=d->boundsDirty=true; RD_Bounds(entity,lo,hi);
    // Underwater legacy corpses must be allowed to rise, rather than frozen on the bottom.
    bool water=false;
    if(waterQuery) { float sample[3]={x,y,lo[2]+16},surface; water=waterQuery(sample,&surface)!=0; }
    if(!water) Freeze(*d);
    else for(auto &p:d->parts) { p.body->setLinearVelocity(btVector3(0,0,0)); p.body->setAngularVelocity(btVector3(0,0,0)); }
}
