#include "world_boundary.hpp"
#include <iostream>

using namespace cc;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try {
    const auto policy=FusionPolicy::parse({{"schema",1},{"kind","local-world-boundary"},
        {"worldDepthRange",{0.f,.8f}},{"foregroundDepthRange",{0.f,.2f}},{"clearDepth",1.f},{"cameraDraw",64}});
    const std::array<double,6> world{0,0,160,100,0,double(.8f)},front{0,0,160,100,0,double(.2f)};
    WorldBoundary b;
    require(!b.observe(11,front,policy),"Unarmed boundary accepted");
    b.arm(11,7,world);require(!b.observe(11,world,policy),"World continuation is not a boundary");
    require(b.observe(11,front,policy) && b.ready(),"First world boundary lost");
    require(!b.observe(11,front,policy) && b.ready(),"Foreground continuation invalidated pair");
    b.clear_resource(11,true);require(b.ready(),"Observed post-world full clear discarded owned snapshot");
    require(!b.observe(11,world,policy) && b.ready(),"Later world draw replaced the protected snapshot");
    require(!b.observe(11,front,policy) && b.ready(),"Second boundary replaced first snapshot");
    b.reset();b.arm(11,8,world);require(!b.observe(12,front,policy) && !b.ready(),"Different resource paired");
    b.reset();b.arm(11,9,world);b.clear_resource(11);require(!b.observe(11,front,policy),"Clear reused camera");
    b.reset();b.arm(11,10,world);auto resize=front;resize[2]=320;
    require(!b.observe(11,resize,policy),"Resize paired");
    b.reset();b.arm(11,11,world);auto unknown=front;unknown[5]=.5;
    require(!b.observe(11,unknown,policy),"Unknown pass paired");
    b.reset();require(!b.ready() && !b.armed && !b.sequence,"Previous interval survived reset");
    Frame guest;HostCamera host;host.right_handed=true;host.viewport=world;
    host.projection[0]=host.projection[5]=1;host.position={12,34,56};
    require(!fusion_aligned(guest,host),"Unverified guest accepted");guest.relayed_camera=host;
    require(fusion_aligned(guest,host),"Equal rays rejected");
    guest.metadata.full_client=true;require(!fusion_aligned(guest,host),"Mixed hand/HUD layer accepted");guest.metadata.full_client=false;
    guest.metadata.gui_open=true;require(!fusion_aligned(guest,host),"GUI accepted");guest.metadata.gui_open=false;
    auto moved=host;moved.position[0]+=.01;require(!fusion_aligned(guest,moved),"Translation lag accepted");
    moved=host;moved.view[0]+=.001;require(!fusion_aligned(guest,moved),"Rotation lag accepted");
    moved=host;moved.projection[0]+=.01;require(!fusion_aligned(guest,moved),"Lens change accepted");
    moved=host;moved.viewport[2]*=2;require(!fusion_aligned(guest,moved),"Viewport change accepted");
    std::cout<<"World boundary identity, invalidation and static ray gates passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
