#include "../ref/gl/ragdoll_physics.h"
#include "../3rdparty/bullet/src/btBulletDynamicsCommon.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
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
    int count = 0;
    rd_bone bones[RD_MAX_BONES] = {};
    rd_matrix pose[RD_MAX_BONES] = {};
    std::vector<rd_hitbox> boxes;
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
    return r;
}
int main(int argc,char **argv) {
    try {
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
            int failures = 0;
            const char *models[]={"zombie","zombie_headless","zombie2","zombie2_headless","zombie3","zombie3_headless","zombie_fresh","zombie_fresh_headless","lpzombie","lpzombie_headless","nurse","nurse_headless","nursezombie","nursezombie_headless","hgrunt","hgrunt_headless","zgrunt","zgrunt_headless","agrunt","islave","babykelly","zombiebull","hungercrab","houndeye_killfx","houndeye_headless","bullsquid","megasquid","chicken","thehand"};
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
        }
        RD_Reset(); return 0;
    } catch(const std::exception &e) { std::cerr<<"FAIL ragdoll physics: "<<e.what()<<"\n";RD_Reset();return 1; }
}
