/* Enemy ragdoll experiment. Bullet is vendored under its zlib license.
 * Original integration; see Documentation/ragdolls.md for research/provenance. */
#include "ragdoll_physics.h"
#include "../../3rdparty/bullet/src/btBulletDynamicsCommon.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

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
    float radius;
    btTransform offset; // bone -> centre of collider
    std::unique_ptr<btCollisionShape> shape;
    std::unique_ptr<btRigidBody> body;
};
struct Doll {
    int entity, count;
    float token, scale;
    int parent[RD_MAX_BONES], part[RD_MAX_BONES];
    btTransform local[RD_MAX_BONES], initial[RD_MAX_BONES];
    std::vector<Part> parts;
    std::vector<std::unique_ptr<btTypedConstraint>> joints;
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
    std::vector<std::unique_ptr<Mesh>> meshes;
    std::unique_ptr<Mesh> pending;
    double lastTime = -1, accumulator = 0;
    Physics() : dispatcher(&config), world(&dispatcher, &broadphase, &solver, &config) {
        world.setGravity(btVector3(0, 0, -800 * Scale));
        world.getSolverInfo().m_numIterations = 80;
        world.getSolverInfo().m_splitImpulse = true;
    }
    void removeDoll(size_t i) {
        for (auto &j : dolls[i]->joints) world.removeConstraint(j.get());
        for (auto &p : dolls[i]->parts) world.removeRigidBody(p.body.get());
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
        for (auto &d : dolls) if (d->entity == entity) return d.get();
        return nullptr;
    }
};
std::unique_ptr<Physics> physics;
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

extern "C" void RD_Reset(void) { physics.reset(); }
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
extern "C" void RD_MoveMesh(int key, const rd_matrix transform) {
    if (!physics) return;
    for (auto &m : physics->meshes) if (m->key == key) {
        const btTransform t = Transform(transform);
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
extern "C" int RD_Create(int entity, float token, int count, const rd_bone *bones,
    int hitboxCount, const rd_hitbox *hitboxes, const rd_matrix *pose, const float *velocity, int hitgroup) {
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
    if (p.dolls.size() >= RD_MAX_CORPSES) return 0;
    std::unique_ptr<Doll> d(new Doll);
    d->entity = entity; d->token = token; d->count = count;
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
        if (radius > 0.35f) {
            part.shape.reset(new btBoxShape(half * 0.85f));
            part.shape->setMargin(0.01f);
        } else if (axis == 0) part.shape.reset(new btCapsuleShapeX(radius, height));
        else if (axis == 2) part.shape.reset(new btCapsuleShapeZ(radius, height));
        else part.shape.reset(new btCapsuleShape(radius, height));
        part.offset.setIdentity(); part.offset.setOrigin(centre);
        const float mass = std::max(2.0f, std::min(8.0f, half.x()*half.y()*half.z()*8.0f*700.0f));
        btVector3 inertia;
        part.shape->calculateLocalInertia(mass, inertia);
        btRigidBody::btRigidBodyConstructionInfo info(mass, nullptr, part.shape.get(), inertia);
        part.body.reset(new btRigidBody(info));
        const btTransform t = d->initial[i] * part.offset;
        part.body->setWorldTransform(t);
        part.body->setInterpolationWorldTransform(t);
        part.body->setDamping(0.12f, 0.35f);
        part.body->setFriction(0.8f); part.body->setRestitution(0.05f);
        part.body->setSleepingThresholds(0.12f, 0.18f);
        part.body->setCcdMotionThreshold(radius * 0.5f);
        part.body->setCcdSweptSphereRadius(radius * 0.8f);
        btVector3 v = Vec(velocity);
        if (v.length() > 15) v *= 15 / v.length();
        part.body->setLinearVelocity(v);
        // A small deterministic tumble prevents balanced standing corpses.
        part.body->setAngularVelocity(btVector3(v.y(), -v.x(), 0) * 0.35f + btVector3(0.4f, 0.7f, 0.2f));
        if (group[i] == hitgroup) part.body->setAngularVelocity(part.body->getAngularVelocity() * 1.8f);
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
        else if (HasName(bones[child.bone].name, "forearm") || HasName(bones[child.bone].name, "calf")) { swing = 1.1f; twist = 0.12f; }
        else if (HasName(bones[child.bone].name, "upperarm") || HasName(bones[child.bone].name, "thigh")) { swing = 1.2f; twist = 0.6f; }
        // Oversized creature trunks/tails cannot fold onto terrain under
        // human spine limits. Give these assemblies a free ball joint.
        if (child.radius > 0.35f || parent.radius > 0.35f) { swing = 3.0f; twist = 3.0f; }
        cone->setLimit(swing, swing, twist, 0.9f, 0.3f, 1.0f);
        cone->setDamping(0.15f);
        // Preserve attachment points on small, closely spaced legacy spine bones.
        cone->setParam(BT_CONSTRAINT_ERP, 0.8f);
        d->joints.push_back(std::move(cone));
    }
    // Self collision is off: legacy hitboxes overlap significantly. Other
    // corpses and world brushes still collide.
    for (auto &part : d->parts) p.world.addRigidBody(part.body.get(), CorpseGroup, WorldGroup | CorpseGroup);
    for (size_t i = 0; i < d->parts.size(); ++i)
        for (size_t j = i+1; j < d->parts.size(); ++j)
            d->parts[i].body->setIgnoreCollisionCheck(d->parts[j].body.get(), true);
    for (auto &j : d->joints) p.world.addConstraint(j.get(), true);
    p.dolls.push_back(std::move(d));
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
    p.accumulator += dt;
    const double step = 1.0 / 120.0;
    while (p.accumulator + 1e-9 >= step) {
        p.world.stepSimulation((btScalar)step, 0);
        p.accumulator -= step;
    }
}
extern "C" int RD_Pose(int entity, rd_matrix *pose, int count) {
    Doll *d = physics ? physics->find(entity) : nullptr;
    if (!d || count != d->count) return 0;
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
    return 1;
}
extern "C" int RD_Bounds(int entity, float *mins, float *maxs) {
    Doll *d = physics ? physics->find(entity) : nullptr;
    if (!d) return 0;
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
    return 1;
}
extern "C" int RD_Count(void) { return physics ? (int)physics->dolls.size() : 0; }
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
