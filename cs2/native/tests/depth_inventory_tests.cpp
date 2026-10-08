#include "depth_inventory.hpp"
#include "depth_view.hpp"
#include <iostream>
#include <memory>
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
void test_views() {
    namespace api=reshade::api;
    auto single=depth();
    // Mirrors ReShade v6.8.0 D3D11 DSV conversion: levels=1, unused layers=0.
    const api::resource_view_desc single_desc(api::resource_view_type::texture_2d,api::format::d32_float,0,1,0,0);
    auto base=describe_depth_view(single_desc,single);
    require(base.canonical && base.raw.layers==0 && base.normalized.layers==1,"D3D11 ignored layer field is not a non-base slice");
    auto whole=describe_depth_view({api::resource_view_type::texture_2d,api::format::d32_float,0,UINT32_MAX,0,UINT32_MAX},single);
    require(whole.canonical && whole.normalized.levels==1 && whole.normalized.layers==1,"Whole single-level resource defaults");
    auto bad=describe_depth_view({api::resource_view_type::texture_2d,api::format::d32_float,0,0,0,0},single);
    require(!bad.canonical,"Zero active mip count is not an all-resource range");

    auto ms=single; ms.samples=4;
    auto ms_view=describe_depth_view({api::resource_view_type::texture_2d_multisample,api::format::d32_float,0,1,0,0},ms);
    require(ms_view.canonical && ms_view.normalized==DepthSubresources{0,1,0,1} && !ms.sampling_shape(),"Canonical MSAA DSV remains unsupported by single-sample compositor");
    require(!describe_depth_view(single_desc,ms).canonical,"Single-sample view cannot classify MSAA resource as canonical");
    require(!describe_depth_view({api::resource_view_type::texture_2d_multisample,api::format::d32_float,0,1,0,0},single).canonical,"MSAA view cannot classify single-sample resource as canonical");
    auto ms_defaults=describe_depth_view({api::resource_view_type::texture_2d_multisample,api::format::d32_float,0,UINT32_MAX,0,UINT32_MAX},ms);
    require(ms_defaults.canonical && ms_defaults.normalized==DepthSubresources{0,1,0,1},"MSAA default fields are ignored");

    auto mip=single; mip.levels=4;
    require(describe_depth_view(single_desc,mip).canonical,"Base mip DSV of mipmapped resource");
    require(!describe_depth_view({api::resource_view_type::texture_2d,api::format::d32_float,1,1,0,0},mip).canonical,"Nonzero mip stays noncanonical");
    require(!describe_depth_view({api::resource_view_type::texture_2d,api::format::d32_float,0,UINT32_MAX,0,0},mip).canonical,"All mips is not a one-mip DSV");
    require(!describe_depth_view({api::resource_view_type::texture_2d,api::format::d32_float,UINT32_MAX,1,0,0},mip).canonical,"Out-of-range mip cannot wrap");

    auto array=single; array.layers=6;
    auto array_view=describe_depth_view({api::resource_view_type::texture_2d_array,api::format::d32_float,0,1,0,UINT32_MAX},array);
    require(array_view.canonical && array_view.normalized.layers==6,"Whole array default is normalized to the resource layers");
    require(!describe_depth_view(single_desc,array).canonical,"Non-array view does not cover layered resource");
    require(!describe_depth_view({api::resource_view_type::texture_2d_array,api::format::d32_float,0,1,0,0},array).canonical,"Zero array range is invalid");
    require(!describe_depth_view({api::resource_view_type::texture_2d_array,api::format::d32_float,0,1,0,1},array).canonical,"Partial array at slice zero is noncanonical");
    auto slice=describe_depth_view({api::resource_view_type::texture_2d_array,api::format::d32_float,0,1,2,UINT32_MAX},array);
    require(!slice.canonical && slice.normalized.first_layer==2 && slice.normalized.layers==4,"Nonzero first slice remains visible after normalization");
    require(!describe_depth_view({api::resource_view_type::texture_2d_array,api::format::d32_float,0,1,UINT32_MAX,UINT32_MAX},array).canonical,"Out-of-range default array range cannot wrap");
    array.samples=4;
    require(describe_depth_view({api::resource_view_type::texture_2d_multisample_array,api::format::d32_float,0,1,0,6},array).canonical,"Full MSAA array DSV");
    require(!describe_depth_view({api::resource_view_type::texture_2d_multisample_array,api::format::d32_float,0,1,1,1},array).canonical,"MSAA nonzero slice remains noncanonical");

    require(!describe_depth_view({api::resource_view_type::texture_2d,api::format::d16_unorm,0,1,0,0},single).format_compatible,"Different depth format family");
    require(!describe_depth_view({api::resource_view_type::texture_2d,api::format::r32_float,0,1,0,0},single).canonical,"SRV interpretation is not a typed DSV");
    require(!describe_depth_view({api::resource_view_type::texture_2d,api::format::unknown,0,1,0,0},single).canonical,"Unknown view format cannot be canonical");
    require(!describe_depth_view({api::resource_view_type::texture_cube,api::format::d32_float,0,1,0,6},single).canonical,"Unsupported view type rejected");
    for(const auto [typed,typeless]:{std::pair{api::format::d16_unorm,api::format::r16_typeless},
        {api::format::d24_unorm_s8_uint,api::format::r24_g8_typeless},{api::format::d32_float,api::format::r32_typeless},
        {api::format::d32_float_s8_uint,api::format::r32_g8_typeless}}) {
        auto family=single; family.format=uint32_t(typeless);
        require(describe_depth_view({api::resource_view_type::texture_2d,typed,0,1,0,0},family).canonical,"Typeless resource / typed DSV family");
        family.format=uint32_t(typed);
        require(describe_depth_view({api::resource_view_type::texture_2d,typed,0,1,0,0},family).canonical,"Typed resource / typed DSV family");
    }

    auto owned=std::make_unique<DepthInventory>(); auto& inventory=*owned;
    inventory.bind_view(1,1,1,ms,ms_view); inventory.draw(1,1,36,2); inventory.draw(1,1,2,1,true);
    inventory.clear({1,1},ms,1,&ms_view);
    auto report=finish(inventory);
    const auto& counts=report.candidates[0].counts;
    require(counts.non_base_view_draws==0 && counts.draws==3 && counts.elements==72 && counts.indirect==2,"MSAA draws classified without guessing indirect elements");
    require(counts.view_count==1 && counts.views[0].binds==1 && counts.views[0].draws==3 && counts.views[0].clears==1
        && counts.views[0].description.raw.layers==0 && counts.views[0].description.canonical,"Per-view raw/normalized metadata and clear accounting");
    inventory.bind_view(1,1,2,mip,base); inventory.draw(1,1,9,1);
    auto mip_view=describe_depth_view({api::resource_view_type::texture_2d,api::format::d32_float,1,1,0,0},mip);
    inventory.bind_view(1,1,2,mip,mip_view); inventory.draw(1,1,18,1); inventory.clear({1,2},mip,0,&mip_view);
    report=finish(inventory);
    require(report.candidates[0].counts.view_count==2 && report.candidates[0].counts.non_base_view_draws==1
        && report.candidates[0].counts.views[0].elements==9 && report.candidates[0].counts.views[1].elements==18,"Distinct subresources keep independent counts");
    inventory.bind_view(1,1,2,mip,base); inventory.begin_effects(1,1280,720);
    inventory.bind_view(1,2,2,mip,mip_view); inventory.clear({1,2},mip,1,&mip_view); inventory.end_effects(1);
    require(finish(inventory).count==0,"Effects do not create view statistics");
    inventory.bind_view(1,1,2,mip,base); inventory.observe({1,2},mip,true); inventory.draw(1,1,3,1);
    require(finish(inventory).count==0,"New resource lifetime invalidates view statistics and bindings");

    auto bounded=std::make_unique<DepthInventory>();
    mip.levels=8;
    for(uint32_t i=0;i<5;++i) {
        auto view=describe_depth_view({api::resource_view_type::texture_2d,api::format::d32_float,i,1,0,0},mip);
        bounded->bind_view(1,1,1,mip,view); bounded->draw(1,1,3,1);
    }
    report=finish(*bounded);
    require(report.candidates[0].counts.view_count==4 && report.candidates[0].counts.draws==5
        && report.candidates[0].counts.non_base_view_draws==4 && bounded->overflow()==1,"View capacity reports overflow while preserving aggregate accounting");
    bounded->bind_view(1,1,1,mip,base); bounded->draw(1,1,3,1);
    require(finish(*bounded).candidates[0].counts.view_count==1,"View slots reset at each interval");
}
}
int main() {
    try {
        auto p_owned=std::make_unique<DepthInventory>(); auto& p=*p_owned;
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

        auto ranking_owned=std::make_unique<DepthInventory>(); auto& ranking=*ranking_owned;
        ranking.bind(1,1,10,depth(1024,1024)); ranking.bind(1,2,20,depth());
        for(int i=0;i<100;++i) ranking.draw(1,1,36,1);
        ranking.draw(1,2,36,1); r=finish(ranking);
        require(r.count==2 && r.candidates[0].matches_output && r.candidates[1].counts.draws==100,"Rank output extent before shadow draw count");
        ranking.bind(1,2,20,depth(),false); ranking.draw(1,2,3,1,true);
        r=finish(ranking); require(r.candidates[0].counts.indirect==3 && r.candidates[0].counts.elements==0
            && r.candidates[0].counts.non_base_view_draws==3,"Indirect does not invent vertex count/subresource validity");

        auto devices_owned=std::make_unique<DepthInventory>(); auto& devices=*devices_owned;
        devices.bind(1,1,5,depth()); devices.bind(2,1,5,depth());
        devices.draw(1,1,36,1); devices.draw(2,1,72,1); devices.begin_effects(1,1280,720);
        require(devices.reports()[0].candidates[0].counts.elements==36 && devices.reports()[1].count==0,"Device intervals independent");
        devices.begin_effects(2,1280,720);require(devices.reports()[1].candidates[0].counts.elements==72,"Same native handles on another device");
        devices.destroy_device(1);require(devices.resource_count()==1 && devices.binding_count()==1,"Device teardown releases only its metadata");

        auto bounded_owned=std::make_unique<DepthInventory>(); auto& bounded=*bounded_owned;
        for(uint64_t i=1;i<=DepthInventory::resource_limit+1;++i) bounded.observe({1,i},depth());
        require(bounded.resource_count()==DepthInventory::resource_limit && bounded.overflow()>0,"Bounded resource storage");
        for(uint64_t i=1;i<=DepthInventory::binding_limit+1;++i) bounded.bind(1,i,1,depth());
        require(bounded.binding_count()==DepthInventory::binding_limit,"Bounded command storage");
        for(uint64_t i=2;i<=DepthInventory::device_limit+1;++i) bounded.begin_effects(i,1280,720);
        require(bounded.reports().back().device_id!=0 && bounded.overflow()>1,"Bounded devices");
        auto many_owned=std::make_unique<DepthInventory>(); auto& many=*many_owned;
        for(uint64_t i=1;i<=12;++i) {many.bind(1,i,i,depth());many.draw(1,i,36,1);}
        require(finish(many).count==8,"Telemetry candidate count is bounded");

        auto saturation_owned=std::make_unique<DepthInventory>(); auto& saturation=*saturation_owned;
        saturation.bind(1,1,10,depth()); saturation.draw(1,1,UINT32_MAX,UINT32_MAX); saturation.draw(1,1,UINT32_MAX,UINT32_MAX);
        require(finish(saturation).candidates[0].counts.elements==UINT64_MAX,"Counters saturate instead of wrap");
        saturation.bind(1,1,10,depth()); saturation.draw(1,1,0,1); saturation.draw(1,1,36,0); saturation.unbind(1,1);
        saturation.draw(1,1,36,1); require(finish(saturation).count==0,"Empty/unbound draws ignored");
        test_views();
        std::cout<<"{\"depthInventoryTests\":\"passed\",\"groups\":16}\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
