#include "depth_inventory.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace cc;
void require(bool value,const char* reason) { if(!value) throw std::runtime_error(reason); }
DepthDescription depth(uint32_t w=1280,uint32_t h=720) {return {w,h,1,1,1,39,DepthKind::d32,true};}
DepthReport finish(DepthInventory& p,uint64_t dev=1,uint32_t w=1280,uint32_t h=720) {
    p.begin_effects(dev,w,h); auto reports=p.reports(); p.end_effects(dev);
    for(const auto& r:reports) if(r.device_id && r.width==w && r.height==h) return r;
    throw std::runtime_error("No report");
}
}
int main() {
    try {
        DepthInventory p;
        p.bind(1,1,100,depth()); p.draw(1,1,36,2); p.clear({1,100},depth(),0);
        auto r=finish(p);
        require(r.count==1 && r.candidates[0].counts.draws==1 && r.candidates[0].counts.elements==72,"Direct draw accounting");
        require(r.candidates[0].counts.has_clear && r.candidates[0].counts.last_clear==0,"Clear evidence");
        auto first_id=r.candidates[0].id;
        p.draw(1,1,36,1); require(finish(p).count==0,"Effects restore requires fresh bind");

        p.bind(1,1,100,depth()); p.begin_effects(1,1280,720);
        p.draw(1,1,36,1); p.clear({1,100},depth(),1); p.bind(1,2,200,depth());
        p.end_effects(1); require(finish(p).count==0 && p.resource_count()==1,"Exclude effect draws/clears/bindings");

        p.bind(1,1,100,depth()); p.destroy_resource({1,100});
        p.observe({1,100},depth(),true); p.draw(1,1,36,1);
        require(finish(p).count==0 && p.binding_count()==0,"Reused handle must not revive old binding");
        p.bind(1,1,100,depth()); p.draw(1,1,36,1);
        require(finish(p).candidates[0].id!=first_id,"Distinct lifetime token");
        p.bind(1,1,100,depth()); p.observe({1,100},depth(),true); p.draw(1,1,36,1);
        require(finish(p).count==0,"New lifetime event invalidates an existing binding");

        p.bind(1,1,100,depth(640,360)); p.draw(1,1,36,1);
        r=finish(p); require(!r.candidates[0].matches_output,"Resolution mismatch is evidence only");
        auto single=depth(); single.samples=4; require(!single.sampling_shape(),"MSAA is not direct single-sample depth");
        single=depth();single.layers=2;require(!single.sampling_shape(),"Array shape unsupported");
        single=depth();single.shader_readable=false;require(!single.sampling_shape(),"No shader-readable flag");

        DepthInventory ranking;
        ranking.bind(1,1,10,depth(1024,1024)); ranking.bind(1,2,20,depth());
        for(int i=0;i<100;++i) ranking.draw(1,1,36,1);
        ranking.draw(1,2,36,1); r=finish(ranking);
        require(r.count==2 && r.candidates[0].matches_output && r.candidates[1].counts.draws==100,"Rank output extent before shadow draw count");
        ranking.bind(1,2,20,depth(),false); ranking.draw(1,2,3,1,true);
        r=finish(ranking); require(r.candidates[0].counts.indirect==3 && r.candidates[0].counts.elements==0
            && r.candidates[0].counts.non_base_view_draws==3,"Indirect does not invent vertex count/subresource validity");

        DepthInventory devices;
        devices.bind(1,1,5,depth()); devices.bind(2,1,5,depth());
        devices.draw(1,1,36,1); devices.draw(2,1,72,1); devices.begin_effects(1,1280,720);
        require(devices.reports()[0].candidates[0].counts.elements==36 && devices.reports()[1].count==0,"Device intervals independent");
        devices.begin_effects(2,1280,720);require(devices.reports()[1].candidates[0].counts.elements==72,"Same native handles on another device");
        devices.destroy_device(1);require(devices.resource_count()==1 && devices.binding_count()==1,"Device teardown releases only its metadata");

        DepthInventory bounded;
        for(uint64_t i=1;i<=DepthInventory::resource_limit+1;++i) bounded.observe({1,i},depth());
        require(bounded.resource_count()==DepthInventory::resource_limit && bounded.overflow()>0,"Bounded resource storage");
        for(uint64_t i=1;i<=DepthInventory::binding_limit+1;++i) bounded.bind(1,i,1,depth());
        require(bounded.binding_count()==DepthInventory::binding_limit,"Bounded command storage");
        for(uint64_t i=2;i<=DepthInventory::device_limit+1;++i) bounded.begin_effects(i,1280,720);
        require(bounded.reports().back().device_id!=0 && bounded.overflow()>1,"Bounded devices");
        DepthInventory many;
        for(uint64_t i=1;i<=12;++i) {many.bind(1,i,i,depth());many.draw(1,i,36,1);}
        require(finish(many).count==8,"Telemetry candidate count is bounded");

        DepthInventory saturation;
        saturation.bind(1,1,10,depth()); saturation.draw(1,1,UINT32_MAX,UINT32_MAX); saturation.draw(1,1,UINT32_MAX,UINT32_MAX);
        require(finish(saturation).candidates[0].counts.elements==UINT64_MAX,"Counters saturate instead of wrap");
        saturation.bind(1,1,10,depth()); saturation.draw(1,1,0,1); saturation.draw(1,1,36,0); saturation.unbind(1,1);
        saturation.draw(1,1,36,1); require(finish(saturation).count==0,"Empty/unbound draws ignored");
        std::cout<<"{\"depthInventoryTests\":\"passed\",\"groups\":9}\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
