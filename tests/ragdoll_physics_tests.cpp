#include "../ref/gl/ragdoll_physics.h"
#include "../3rdparty/bullet/src/btBulletDynamicsCommon.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

static void Check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
static void Identity(rd_matrix m) {
    std::memset(m, 0, sizeof(rd_matrix)); m[0][0]=m[1][1]=m[2][2]=1;
}
static void Floor() {
    RD_Reset(); rd_matrix m; Identity(m);
    float a[]={-2048,-2048,0}, b[]={2048,-2048,0}, c[]={2048,2048,0}, d[]={-2048,2048,0};
    RD_BeginMesh(0); RD_AddTriangle(a,b,c); RD_AddTriangle(a,c,d); RD_EndMesh(m,0);
}
struct Rig {
    struct FootVertex { int bone; float point[3]; };
    int count = 0;
    rd_bone bones[RD_MAX_BONES] = {};
    rd_matrix pose[RD_MAX_BONES] = {};
    std::vector<rd_hitbox> boxes;
    std::vector<FootVertex> feet;
};
static Rig Synthetic() {
    Rig r; r.count=3;
    for (int i=0;i<3;++i) {
        Identity(r.pose[i]); r.pose[i][2][3]=48+i*16;
        r.bones[i].parent=i-1;
        std::strcpy(r.bones[i].name,i==0?"pelvis":i==1?"spine":"head");
        rd_hitbox h={};h.bone=i;h.group=i+1;
        for (int a=0;a<3;++a) {h.mins[a]=-5;h.maxs[a]=5;}
        r.boxes.push_back(h);
    }
    return r;
}
static void Create(const Rig &r, int entity=1, float token=1) {
    float v[]={120,25,40};
    Check(RD_Create(entity,token,r.count,r.bones,(int)r.boxes.size(),r.boxes.data(),r.pose,v,1)!=0,"create failed");
}
static void Step(int hz, float seconds) {
    RD_Step(0,800);
    for(int i=1;i<=int(hz*seconds);++i) RD_Step(double(i)/hz,800);
}
static void Validate(const Rig &r) {
    rd_matrix pose[RD_MAX_BONES]; float lo[3],hi[3];
    Check(RD_Pose(1,pose,r.count)!=0,"pose unavailable");
    Check(RD_Bounds(1,lo,hi)!=0,"bounds unavailable");
    for(int i=0;i<r.count;++i) for(int a=0;a<3;++a) for(int b=0;b<4;++b)
        Check(std::isfinite(pose[i][a][b]),"nonfinite bone transform");
    Check(lo[2] > -28,"floor penetration or tunnelling"); // bounds include 16-unit mesh margin
    Check(hi[2] < 150,"corpse did not fall onto floor");
    Check(RD_JointError(1) < 2.5f,"joint separation exceeds 2.5 units");
}
static float Simulate(int hz) {
    Floor();auto r=Synthetic();Create(r);Step(hz,4);Validate(r);
    rd_matrix out[RD_MAX_BONES]; RD_Pose(1,out,r.count);
    // Rendering/pause must not advance physics when simulation time is unchanged.
    rd_matrix before[RD_MAX_BONES]; std::memcpy(before,out,sizeof(out));
    for(int i=0;i<100;++i) RD_Step(4,800);
    RD_Pose(1,out,r.count);
    Check(std::memcmp(before,out,r.count*sizeof(rd_matrix))==0,"pause advances simulation");
    return out[0][2][3];
}
static int Int(const std::vector<char> &d,size_t off) {
    Check(off+4<=d.size(),"invalid model offset"); int value;std::memcpy(&value,d.data()+off,4);return value;
}
static Rig Model(const std::string &path) {
    std::ifstream f(path,std::ios::binary);Check(bool(f),"model file unavailable");
    std::vector<char> d((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    Check(Int(d,0)==0x54534449 && Int(d,4)==10,"unexpected studio format");
    // Match the engine's resident allocation after embedded texture upload.
    // A raw-file-only test can miss geometry incorrectly placed past this tail.
    if(Int(d,180)>0) {
        const int limit=Int(d,188);Check(limit>=244 && size_t(limit)<=d.size(),"invalid texture truncation boundary");
        d.resize(limit);
    }
    Rig r; r.count=Int(d,140);int boneoff=Int(d,144),boxcount=Int(d,156),boxoff=Int(d,160);
    Check(r.count>0 && r.count<=RD_MAX_BONES && boxcount>0 && boxcount<=256,"invalid studio counts");
    btTransform world[RD_MAX_BONES];
    for(int i=0;i<r.count;++i) {
        size_t off=boneoff+i*112; Check(off+112<=d.size(),"invalid bone data");
        std::memcpy(r.bones[i].name,d.data()+off,32);r.bones[i].parent=Int(d,off+32);
        float value[6];std::memcpy(value,d.data()+off+64,24);
        // Studio's bind Euler order matches quaternion ZYX composition.
        btQuaternion q; q.setEulerZYX(value[5],value[4],value[3]);
        btTransform local(q,btVector3(value[0],value[1],value[2]));
        int parent=r.bones[i].parent; Check(parent>=-1 && parent<i,"invalid bone hierarchy");
        world[i]=parent<0?local:world[parent]*local;
        for(int a=0;a<3;++a) {for(int b=0;b<3;++b) r.pose[i][a][b]=world[i].getBasis()[a][b];r.pose[i][a][3]=world[i].getOrigin()[a]+(a==2?100:0);}
    }
    for(int i=0;i<boxcount;++i) {
        size_t off=boxoff+i*32;Check(off+32<=d.size(),"invalid hitbox data");
        rd_hitbox h;h.bone=Int(d,off);h.group=Int(d,off+4);
        std::memcpy(h.mins,d.data()+off+8,12);std::memcpy(h.maxs,d.data()+off+20,12);r.boxes.push_back(h);
    }
    const int partCount=Int(d,204),partOffset=Int(d,208);
    for(int part=0;part<partCount;++part) {
        const int modelOffset=Int(d,partOffset+part*76+72);
        const int count=Int(d,modelOffset+80),weights=Int(d,modelOffset+84),vertices=Int(d,modelOffset+88);
        Check(count>=0 && weights>=0 && vertices>=0 && size_t(weights)+count<=d.size() && size_t(vertices)+size_t(count)*12<=d.size(),"invalid mesh data");
        const auto points=reinterpret_cast<const float (*)[3]>(d.data()+vertices);
        const auto vertexBones=reinterpret_cast<const unsigned char *>(d.data()+weights);
        RD_FitFootHitboxes(r.count,r.bones,count,points,vertexBones,(int)r.boxes.size(),r.boxes.data());
        for(int vertex=0;vertex<count;++vertex) {
            const int bone=vertexBones[vertex];
            if(bone<r.count && std::strstr(r.bones[bone].name,"Foot")) {
                Rig::FootVertex foot;foot.bone=bone;std::memcpy(foot.point,points[vertex],12);r.feet.push_back(foot);
            }
        }
    }
    return r;
}
static void BootContactTests(const std::string &directory) {
    for(const char *name:{"../allies/barney","../allies/barney_headless","dismemberment_cop_pilot"}) {
        for(int orientation=0;orientation<3;++orientation) {
            Floor();auto rig=Model(directory+"/"+name+".mdl");
            Check(!rig.feet.empty(),"boot regression has no visible vertices");
            const btTransform rotate(btQuaternion(btVector3(1,0,0),orientation*SIMD_HALF_PI),btVector3(0,0,0));
            for(int bone=0;bone<rig.count;++bone) {
                auto &m=rig.pose[bone];
                btTransform t(btMatrix3x3(m[0][0],m[0][1],m[0][2],m[1][0],m[1][1],m[1][2],m[2][0],m[2][1],m[2][2]),btVector3(m[0][3],m[1][3],m[2][3]));t=rotate*t;
                for(int row=0;row<3;++row) {for(int col=0;col<3;++col) m[row][col]=t.getBasis()[row][col];m[row][3]=t.getOrigin()[row];}
            }
            Create(rig);float lo[3],hi[3];RD_Bounds(1,lo,hi);
            const float lift=std::max(0.0f,8-(lo[2]+16));
            for(int bone=0;bone<rig.count;++bone) rig.pose[bone][2][3]+=lift;
            RD_Remove(1);Create(rig);Step(60,20);
            rd_matrix pose[RD_MAX_BONES];RD_Pose(1,pose,rig.count);
            float lowest=std::numeric_limits<float>::max();
            for(const auto &foot:rig.feet) {
                const auto &m=pose[foot.bone];float z=m[2][3];
                for(int axis=0;axis<3;++axis) z+=m[2][axis]*foot.point[axis];
                lowest=std::min(lowest,z);
            }
            std::cout<<"Boot contact "<<name<<" orientation "<<orientation<<": lowest vertex "<<lowest<<" units\n";
            Check(lowest>=-0.1f,"visible boot sole clips through floor");
            Check(RD_Frozen(1),"boot contact corpse failed to settle in 20 seconds");
        }
    }
    std::cout<<"PASS visible Barney/headless/pilot boot vertices above floor in three fall orientations\n";
}
static int Water(const float *bottom,float *surface) {
    *surface=80; return bottom[2]<80;
}
static void StateTests() {
    auto rig=Synthetic(); Floor(); Create(rig); Step(60,0.4f);
    rd_snapshot state; Check(RD_Capture(1,&state)!=0,"snapshot capture failed");
    rd_matrix before[RD_MAX_BONES],after[RD_MAX_BONES]; RD_Pose(1,before,rig.count);
    Floor(); Check(RD_Restore(1,9,rig.bones,(int)rig.boxes.size(),rig.boxes.data(),&state)!=0,"active restore failed");
    RD_Pose(1,after,rig.count);
    for(int i=0;i<rig.count;++i) for(int a=0;a<3;++a) for(int b=0;b<4;++b)
        Check(std::fabs(before[i][a][b]-after[i][a][b])<0.001f,"save/load changed corpse pose");
    Step(60,8); Validate(rig); Check(RD_Frozen(1)!=0,"settled corpse never froze");
    Check(RD_ActiveBodies()==0,"settled limb bodies remain in solver");
    RD_Capture(1,&state); Floor(); Check(RD_Restore(1,10,rig.bones,(int)rig.boxes.size(),rig.boxes.data(),&state),"frozen restore failed");
    Check(RD_Frozen(1) && !RD_ActiveBodies(),"frozen state was lost on restore");
    RD_Pose(1,before,rig.count); Step(60,30); RD_Pose(1,after,rig.count);
    Check(!std::memcmp(before,after,rig.count*sizeof(rd_matrix)),"frozen corpse drifts after load");
    state.pose[0][0][0]=std::numeric_limits<float>::quiet_NaN();
    Check(!RD_Restore(2,1,rig.bones,(int)rig.boxes.size(),rig.boxes.data(),&state),"corrupt snapshot accepted");
    float point[3]={after[0][0][3],after[0][1][3],after[0][2][3]};
    float start[3]={point[0]-50,point[1],point[2]},end[3]={point[0]+50,point[1],point[2]};
    rd_hit hit; Check(RD_Trace(start,end,1,&hit) && hit.entity==1,"frozen corpse not hittable");
    Check(!RD_Trace(start,end,0.1f,&hit),"corpse hit through nearer occluder");
    float impulse[3]={0,140,200}; RD_Impulse(1,point,impulse);
    Check(!RD_Frozen(1) && RD_ActiveBodies()==3,"hit did not wake whole corpse");
    RD_Step(30.2,800); RD_Pose(1,after,rig.count);
    Check(std::fabs(after[0][1][3]-before[0][1][3])>0.1f,"hit impulse had no effect");
    Floor(); for(int i=0;i<rig.count;++i) rig.pose[i][2][3]=1+i*16;
    Create(rig); RD_RestoreLegacy(1); RD_Pose(1,before,rig.count); Step(60,20); RD_Pose(1,after,rig.count);
    float lo[3],hi[3]; RD_Bounds(1,lo,hi);
    Check(RD_Frozen(1) && lo[2]>-16,"legacy restore remains inside floor");
    Check(!std::memcmp(before,after,rig.count*sizeof(rd_matrix)),"legacy corpse tweaks after restore");
    std::cout<<"PASS exact active/frozen save pose, malformed state, legacy floor recovery, frozen ray hit, occlusion, wake impulse\n";
    Floor(); rig=Synthetic();
    rd_matrix platform; Identity(platform);
    float pa[]={-1024,-1024,0},pb[]={1024,-1024,0},pc[]={1024,1024,0},pd[]={-1024,1024,0};
    RD_BeginMesh(1); RD_AddTriangle(pa,pb,pc); RD_AddTriangle(pa,pc,pd); RD_EndMesh(platform,1);
    Create(rig); Step(60,8); Check(RD_Frozen(1),"platform corpse did not settle");
    for(int i=1;i<=120;++i) {
        platform[2][3]=float(i)*0.5f; RD_MoveMesh(1,platform); RD_Step(8+double(i)/60,800);
    }
    RD_Bounds(1,lo,hi); Check(lo[2]>40,"moving brush failed to wake/carry frozen corpse");
    std::cout<<"PASS moving platform wakes and carries a frozen corpse\n";
    Floor(); rig=Synthetic(); RD_SetWaterQuery(Water); Create(rig); Step(60,15);
    RD_Bounds(1,lo,hi);
    Check(lo[2]>30 && hi[2]>80 && hi[2]<150,"corpse did not float at water surface");
    RD_Capture(1,&state); Floor(); RD_SetWaterQuery(Water);
    Check(RD_Restore(1,2,rig.bones,(int)rig.boxes.size(),rig.boxes.data(),&state),"water restore failed");
    Step(60,8); RD_Bounds(1,lo,hi); Check(lo[2]>30,"restored floating corpse sank");
    std::cout<<"PASS buoyancy equilibrium and restored water pose\n";
}
static void CrowdTest(const std::string &directory) {
    Floor(); auto rig=Model(directory+"/zombie.mdl");
    for(int n=0;n<20;++n) {
        auto r=rig; for(int i=0;i<r.count;++i) { r.pose[i][0][3]+=(n%5)*180-360; r.pose[i][1][3]+=(n/5)*180-270; }
        Create(r,n+1,float(n+1));
    }
    auto clock=std::chrono::steady_clock::now(); RD_Step(0,800);
    for(int i=1;i<=60;++i) RD_Step(double(i)/60,800);
    auto active=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-clock).count();
    for(int i=61;i<=1200;++i) RD_Step(double(i)/60,800);
    int frozen=0; for(int i=1;i<=20;++i) frozen+=RD_Frozen(i);
    std::cout<<"Crowd: "<<frozen<<"/20 frozen, "<<RD_ActiveBodies()<<" solver bodies\n";
    if(frozen!=20) for(int entity=1;entity<=20;++entity) if(!RD_Frozen(entity)) {
        rd_snapshot state;RD_Capture(entity,&state);float linear=0,angular=0;int fastest=0;
        for(int bone=0;bone<state.count;++bone) for(int axis=0;axis<3;++axis) {
            linear=std::max(linear,std::fabs(state.linear[bone][axis]));
            if(std::fabs(state.angular[bone][axis])>angular) { angular=std::fabs(state.angular[bone][axis]);fastest=bone; }
        }
        std::cout<<"  unsettled "<<entity<<": linear "<<linear<<" in/s, angular "<<angular<<" rad/s on "<<rig.bones[fastest].name<<"\n";
    }
    Check(frozen==20 && RD_Count()==20 && !RD_ActiveBodies(),"old crowd keeps expensive dynamics bodies");
    clock=std::chrono::steady_clock::now();
    for(int i=1201;i<=1260;++i) RD_Step(double(i)/60,800);
    auto idle=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-clock).count();
    std::cout<<"PASS 20 persistent corpses: active simulation "<<active<<" ms / 60 frames, settled "<<idle<<" ms / 60 frames\n";
}
static void DismembermentTests() {
    for(int frozen=0;frozen<2;++frozen) {
        Floor(); auto rig=Synthetic();rig.boxes.back().group=4;
        Create(rig);
        if(frozen) { Step(60,8);Check(RD_Frozen(1),"dismemberment test corpse did not settle"); }
        rd_snapshot before;Check(RD_Capture(1,&before),"pre-cut pose missing");
        float start[3],end[3],direction[3];float length=0;
        for(int a=0;a<3;++a) { direction[a]=before.pose[2][a][3]-before.pose[1][a][3];length+=direction[a]*direction[a]; }
        length=std::sqrt(length);
        for(int a=0;a<3;++a) { end[a]=before.pose[2][a][3];start[a]=end[a]+direction[a]*30/length; }
        rd_hit hit;
        Check(RD_Trace(start,end,1,&hit) && hit.group==4,"arm collider not hittable before cut");
        Check(RD_RemoveGroup(1,4)==1 && RD_BodyCount(1)==2,"cut did not remove exactly the arm collider");
        Check(RD_RemoveGroup(1,4)==0,"repeated cut changed corpse");
        Check(!RD_Trace(start,end,1,&hit) || hit.group!=4,"detached arm still catches rays");
        rd_snapshot after;Check(RD_Capture(1,&after),"post-cut pose missing");
        for(int b=0;b<rig.count;++b) for(int r=0;r<3;++r) for(int c=0;c<4;++c)
            Check(std::fabs(before.pose[b][r][c]-after.pose[b][r][c])<0.001f,"cut reset the corpse pose");
        Check(RD_Frozen(1)==frozen,"cut changed settled/active state");
        Check(!RD_RemoveGroup(1,0) && !RD_RemoveGroup(99,4),"invalid cut accepted");
        // Saved snapshots restore the pose before the bodygroup removes colliders.
        RD_Remove(1);
        Check(RD_Restore(1,2,rig.bones,(int)rig.boxes.size(),rig.boxes.data(),&after),"cut corpse restore failed");
        Check(RD_RemoveGroup(1,4)==1,"restored missing arm regained colliders");
    }
    std::cout<<"PASS lethal/active and settled corpse limb removal, hit tracing, pose continuity, duplicate cuts and restore\n";
}
static void PropTests() {
    Floor();auto rig=Synthetic();
    float points[8][3];
    for(int i=0;i<8;++i) {
        points[i][0]=(i&1)?7.0f:-3.0f;
        points[i][1]=(i&2)?4.0f:-4.0f;
        points[i][2]=(i&4)?1.0f:-31.0f;
    }
    float velocity[3]={160,25,100},angular[3]={120,90,30};
    Check(RD_CreateProp(1,1,rig.count,rig.bones,rig.pose,8,points,velocity,angular),"prop creation failed");
    Check(RD_BodyCount(1)==1 && !RD_SelfCollisionPairs(1),"prop is not a single rigid collider");
    Step(60,8);
    float lo[3],hi[3];RD_Bounds(1,lo,hi);
    Check(lo[2]>-17 && hi[2]<70,"prop penetrated floor or failed to fall");
    Check(RD_Frozen(1),"prop failed to settle");
    rd_matrix before[RD_MAX_BONES],after[RD_MAX_BONES];RD_Pose(1,before,rig.count);
    for(int i=1;i<rig.count;++i) {
        float length=0;for(int a=0;a<3;++a) length+=std::pow(before[i][a][3]-before[0][a][3],2);
        Check(std::fabs(std::sqrt(length)-16*i)<0.001f,"prop skeleton deformed");
    }
    float point[3]={before[0][0][3],before[0][1][3],before[0][2][3]},kick[3]={0,180,160};
    RD_Impulse(1,point,kick);Check(!RD_Frozen(1),"prop hit failed to wake");
    RD_Step(8.1,800);RD_Pose(1,after,rig.count);
    Check(std::fabs(after[0][1][3]-before[0][1][3])>1,"prop hit did not move it");
    RD_Remove(1);Check(!RD_Count(),"prop removal leaked physics");
    points[0][0]=std::numeric_limits<float>::quiet_NaN();
    Check(!RD_CreateProp(1,2,rig.count,rig.bones,rig.pose,8,points,velocity,angular),"malformed prop accepted");
    std::cout<<"PASS rigid convex prop floor contact, settling, intact skeleton, hit wake, removal and malformed input\n";
}
static void ZombiePartsTests(const std::string &directory) {
    // Exercise all anatomical groups on the actual shipped zombie skeleton,
    // both while falling and after sleeping. Torso colliders must survive.
    const int groups[]={1,4,5,6,7};
    for(int asleep=0;asleep<2;++asleep) {
        Floor();auto rig=Model(directory+"/zombie.mdl");Create(rig);
        if(asleep) {Step(60,20);Check(RD_Frozen(1),"zombie cut test failed to settle");}
        for(int group:groups) {
            rd_snapshot before,after;Check(RD_Capture(1,&before),"zombie pre-cut snapshot unavailable");
            int expected=0;for(const auto &box:rig.boxes) if(box.group==group) ++expected;
            Check(expected>0,"zombie anatomy group has no collider");
            const int bodies=RD_BodyCount(1);
            Check(RD_RemoveGroup(1,group)==expected && RD_BodyCount(1)==bodies-expected,"zombie cut removed wrong colliders");
            Check(!RD_RemoveGroup(1,group),"duplicate zombie cut changed anatomy");
            Check(RD_Capture(1,&after),"zombie post-cut snapshot unavailable");
            for(int bone=0;bone<rig.count;++bone) for(int row=0;row<3;++row) for(int column=0;column<4;++column)
                Check(std::fabs(before.pose[bone][row][column]-after.pose[bone][row][column])<0.001f,"zombie cut jumped the corpse pose");
            Check(bool(RD_Frozen(1))==bool(asleep),"zombie cut changed sleep state");
        }
        Check(RD_BodyCount(1)>0,"fully dismembered torso has no colliders");
        rd_snapshot state;RD_Capture(1,&state);RD_Remove(1);
        Check(RD_Restore(1,2,rig.bones,(int)rig.boxes.size(),rig.boxes.data(),&state),"dismembered zombie restore failed");
        for(int group:groups) Check(RD_RemoveGroup(1,group)>0,"restored zombie cut failed");
        Check(RD_BodyCount(1)>0,"restored torso lost colliders");
    }
    // Read each bank submodel directly: the bank deliberately has no actor
    // hitboxes, and its geometry is rigidly attached to the identity root.
    std::ifstream file(directory+"/zombie_parts.mdl",std::ios::binary);Check(bool(file),"parts bank unavailable");
    std::vector<char> d((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    Check(Int(d,140)==1 && Int(d,156)==0 && Int(d,204)==5,"invalid parts bank layout");
    const int residentLimit=Int(d,188);Check(residentLimit>=244 && size_t(residentLimit)<=d.size(),"invalid parts texture boundary");
    d.resize(residentLimit);
    const int table=Int(d,208);int tested=0;
    for(int part=0;part<5;++part) for(int appearance=0;appearance<11;++appearance) {
        const int group=table+part*76;
        Check(Int(d,group+64)==12,"parts bank appearance count mismatch");
        const int model=Int(d,group+72)+(appearance+1)*112;
        const int count=Int(d,model+80),vertices=Int(d,model+88);
        if(count==0) {Check(part==0 && (appearance==1 || appearance==3),"unexpected empty detached asset");continue;}
        Check(count>=4 && size_t(vertices)+size_t(count)*12<=d.size(),"invalid detached vertices");
        const auto points=reinterpret_cast<const float (*)[3]>(d.data()+vertices);
        Floor();rd_bone root={};root.parent=-1;std::strcpy(root.name,"dismemberment_root");
        rd_matrix pose[1];Identity(pose[0]);pose[0][2][3]=90;
        float velocity[3]={100,20,70},angular[3]={120,-80,30};
        Check(RD_CreateProp(1,1,1,&root,pose,count,points,velocity,angular),"detached zombie asset rejected by physics");
        Step(60,20);Check(RD_Frozen(1),"detached zombie asset failed to settle");
        rd_matrix settled[1];Check(RD_Pose(1,settled,1),"detached zombie pose missing");
        float lowest=std::numeric_limits<float>::max();
        for(int i=0;i<count;++i) {float z=settled[0][2][3];for(int axis=0;axis<3;++axis) z+=settled[0][2][axis]*points[i][axis];lowest=std::min(lowest,z);}
        Check(lowest>=-0.12f,"detached zombie visible mesh clips floor");
        float hit[3]={settled[0][0][3],settled[0][1][3],settled[0][2][3]},kick[3]={0,100,80};
        RD_Impulse(1,hit,kick);Check(!RD_Frozen(1),"detached zombie asset cannot wake");++tested;
    }
    Check(tested==53,"wrong number of detached zombie assets tested");
    std::cout<<"PASS all five zombie cuts while active/asleep, sequential cuts/restore and 53 bank assets floor/settle/wake\n";
}
static void TimeoutTests() {
    RD_Reset();auto rig=Synthetic();Create(rig);RD_Step(0,0);
    // Keep the assembly active without floor contacts. Repeated damage used
    // to reset its settling age, but must not bypass the hard solver timeout.
    for(int tick=1;tick<=7205;++tick) {
        if(tick%30==0 && tick<7200) {
            rd_snapshot state;Check(RD_Capture(1,&state),"timeout snapshot missing");
            float point[3]={state.pose[0][0][3],state.pose[0][1][3],state.pose[0][2][3]};
            float kick[3]={0,(tick%60)?120.0f:-120.0f,0};RD_Impulse(1,point,kick);
        }
        RD_Step(double(tick)/120,0);
        if(tick==7199) Check(!RD_Frozen(1),"hard timeout fired before 60 seconds");
    }
    Check(RD_Frozen(1) && !RD_ActiveBodies(),"unsettled corpse bypassed 60-second safety timeout");
    rd_snapshot state;RD_Capture(1,&state);
    float point[3]={state.pose[0][0][3],state.pose[0][1][3],state.pose[0][2][3]},kick[3]={0,120,40};
    RD_Impulse(1,point,kick);Check(!RD_Frozen(1),"timed-out corpse cannot wake on a later hit");
    RD_Step(60.1,0);Check(!RD_Frozen(1),"woken corpse retained an expired timeout");
    std::cout<<"PASS 60-second hard settling timeout despite repeated impulses, and wake/reset after timeout\n";
}
int main(int argc,char **argv) {
    try {
        StateTests();
        DismembermentTests();
        PropTests();
        TimeoutTests();
        float a=Simulate(30), b=Simulate(60), c=Simulate(144);
        Check(std::fabs(a-b)<0.01f && std::fabs(a-c)<0.01f,"frame-rate-dependent simulation");
        std::cout<<"PASS fixed 120 Hz, floor collision, constrained joints, finite poses, pause\n";
        Floor();auto rig=Synthetic();Create(rig);Create(rig);
        Check(RD_Count()==1,"duplicate ragdoll created");
        Create(rig,1,2);Check(RD_Count()==1 && !RD_Has(1,1) && RD_Has(1,2),"entity reuse failed");
        RD_Remove(1);Check(RD_Count()==0,"corpse removal failed");
        for(int i=1;i<=RD_MAX_CORPSES;++i) Create(rig,i,float(i));
        float v[3]={};Check(!RD_Create(999,1,rig.count,rig.bones,(int)rig.boxes.size(),rig.boxes.data(),rig.pose,v,0),"budget exceeded");
        RD_Reset();Check(RD_Count()==0,"map reset leaked corpses");
        rig.bones[1].parent=2;Check(!RD_Create(1,1,rig.count,rig.bones,(int)rig.boxes.size(),rig.boxes.data(),rig.pose,v,0),"invalid hierarchy accepted");
        std::cout<<"PASS duplicate death, entity reuse, gib/removal, budget, map reset, malformed hierarchy\n";
        if(argc>1) {
            ZombiePartsTests(argv[1]);
            int failures = 0;
            const char *models[]={"zombie","zombie_headless","zombie2","zombie2_headless","zombie3","zombie3_headless","zombie_fresh","zombie_fresh_headless","lpzombie","lpzombie_headless","nurse","nurse_headless","nursezombie","nursezombie_headless","hgrunt","hgrunt_headless","zgrunt","zgrunt_headless","agrunt","islave","babykelly","zombiebull","hungercrab","houndeye_killfx","houndeye_headless","bullsquid","megasquid","chicken","thehand","../allies/barney","../allies/barney_headless"};
            for(const char *name:models) {
                try {
                    Floor();auto r=Model(std::string(argv[1])+"/"+name+".mdl");Create(r);
                    // Some bind skeletons extend far below the model origin
                    // (the animation supplies the standing offset in game).
                    // Start the complete collider assembly above the test floor.
                    float lo[3], hi[3]; RD_Bounds(1,lo,hi);
                    const float lift = std::max(0.0f, 48.0f-lo[2]);
                    if(lift>0) {
                        for(int i=0;i<r.count;++i) r.pose[i][2][3]+=lift;
                        RD_Remove(1);Create(r);
                    }
                    Step(60,6);Validate(r);
                    std::cout<<"PASS model "<<name<<": "<<RD_BodyCount(1)<<" bodies, joint error "<<RD_JointError(1)<<std::endl;
                } catch(const std::exception &e) {
                    float lo[3],hi[3]; RD_Bounds(1,lo,hi);
                    std::cerr<<"bounds Z "<<lo[2]<<".."<<hi[2]<<" ";
                    std::cerr<<"FAIL model "<<name<<": "<<e.what()<<", joint error "<<RD_JointError(1)<<std::endl; ++failures;
                }
            }
            Check(failures==0,"model suite failed");
            Floor();auto cop=Model(std::string(argv[1])+"/dismemberment_cop_pilot.mdl");Create(cop);
            Check(RD_SelfCollisionPairs(1)>0,"cop has no separated-limb self collisions");
            std::cout<<"PASS cop selective self collision: "<<RD_SelfCollisionPairs(1)<<" enabled pairs\n";
            Step(60,12);Validate(cop);
            Check(RD_Frozen(1),"cop did not settle after falling");
            Check(RD_SelfPenetration(1)<1.0f,"cop separated body parts interpenetrate");
            // A captured horizontal pose near the floor must settle without
            // relying on the old age/root-only freeze shortcuts.
            Floor();
            btTransform rotate(btQuaternion(btVector3(1,0,0),SIMD_HALF_PI),btVector3(0,0,0));
            for(int bone=0;bone<cop.count;++bone) {
                auto &m=cop.pose[bone];
                btTransform t(btMatrix3x3(m[0][0],m[0][1],m[0][2],m[1][0],m[1][1],m[1][2],m[2][0],m[2][1],m[2][2]),
                    btVector3(m[0][3],m[1][3],m[2][3]));t=rotate*t;
                for(int r=0;r<3;++r) { for(int c=0;c<3;++c) m[r][c]=t.getBasis()[r][c];m[r][3]=t.getOrigin()[r]; }
            }
            float zero[3]={},lo[3],hi[3];
            Check(RD_Create(1,1,cop.count,cop.bones,(int)cop.boxes.size(),cop.boxes.data(),cop.pose,zero,0),"prone cop creation failed");
            RD_Bounds(1,lo,hi);const float lift=4-(lo[2]+16);
            for(int bone=0;bone<cop.count;++bone) cop.pose[bone][2][3]+=lift;
            RD_Remove(1);Check(RD_Create(1,2,cop.count,cop.bones,(int)cop.boxes.size(),cop.boxes.data(),cop.pose,zero,0),"lifted prone cop failed");
            Step(60,12);Validate(cop);
            Check(RD_Frozen(1),"prone cop keeps twitching on flat ground");
            Check(RD_SelfPenetration(1)<1.0f,"prone cop separated body parts interpenetrate");
            std::cout<<"PASS falling and prone cop settling, floor contact and separated-part penetration below one unit\n";
            BootContactTests(argv[1]);
            CrowdTest(argv[1]);
        }
        RD_Reset(); return 0;
    } catch(const std::exception &e) { std::cerr<<"FAIL ragdoll physics: "<<e.what()<<"\n";RD_Reset();return 1; }
}
