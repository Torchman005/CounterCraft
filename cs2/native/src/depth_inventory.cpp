#include "depth_inventory.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cc {
namespace {
uint64_t add(uint64_t a,uint64_t b) { return a > UINT64_MAX-b ? UINT64_MAX : a+b; }
}
bool DepthDescription::sampling_shape() const {
    return width && height && layers==1 && levels==1 && samples==1
        && kind!=DepthKind::unknown && shader_readable;
}
DepthView normalize_depth_view(DepthViewType type,uint32_t format,
    uint32_t first_level,uint32_t levels,uint32_t first_layer,uint32_t layers,
    const DepthDescription& resource,bool format_compatible) {
    const bool ms=type==DepthViewType::texture_2d_multisample || type==DepthViewType::texture_2d_multisample_array;
    const bool array=type==DepthViewType::texture_2d_array || type==DepthViewType::texture_2d_multisample_array;
    const bool supported=type==DepthViewType::texture_2d || type==DepthViewType::texture_2d_array || ms;
    DepthView result{type,format,{first_level,levels,first_layer,layers},
        {first_level,levels,first_layer,layers},format_compatible,false};
    if(!supported) return result;
    auto& range=result.normalized;
    // MS DSVs have no mip selector. Non-array DSVs have no layer selector.
    // ReShade's D3D11 converter leaves those unused fields as zero.
    if(ms) { range.first_level=0; range.levels=1; }
    else if(levels==UINT32_MAX && first_level<resource.levels) range.levels=resource.levels-first_level;
    if(!array) { range.first_layer=0; range.layers=1; }
    else if(layers==UINT32_MAX && first_layer<resource.layers) range.layers=resource.layers-first_layer;
    const bool sample_shape=ms ? resource.samples>1 && resource.levels==1 : resource.samples==1;
    result.canonical=resource.width && resource.height && resource.layers && resource.levels
        && format_compatible && sample_shape && (array || resource.layers==1)
        && range.first_level==0 && range.levels==1 && range.first_layer==0 && range.layers==resource.layers;
    return result;
}
DepthInventory::Device* DepthInventory::device(uint64_t key) {
    if (!key) return nullptr;
    for (auto& d:devices_) if(d.key==key) return &d;
    for (auto& d:devices_) if(!d.key) { d.key=key; d.id=++next_device_; return &d; }
    overflow_=add(overflow_,1); return nullptr;
}
DepthInventory::Resource* DepthInventory::resource(DepthKey key) {
    for (auto& r:resources_) if(r.id && r.key==key) return &r;
    return nullptr;
}
DepthInventory::Resource* DepthInventory::by_id(uint64_t id) {
    if (!id) return nullptr;
    for (auto& r:resources_) if(r.id==id) return &r;
    return nullptr;
}
void DepthInventory::observe(DepthKey key,const DepthDescription& desc,bool fresh) {
    if(!key.resource || !desc.width || !desc.height || !device(key.device)) return;
    auto* r=resource(key);
    if(r && (fresh || r->desc!=desc)) { destroy_resource(key); r=nullptr; }
    if(r) return;
    for(auto& slot:resources_) if(!slot.id) { slot.key=key; slot.id=++next_resource_; slot.desc=desc; return; }
    overflow_=add(overflow_,1);
}
void DepthInventory::destroy_resource(DepthKey key) {
    if(auto* r=resource(key)) {
        for(auto& b:bindings_) if(b.resource_id==r->id) b={};
        *r={};
    }
}
uint32_t DepthInventory::view_slot(DepthCounts& counts,const DepthView& view) {
    for(uint32_t i=0;i<counts.view_count;++i) if(counts.views[i].description==view) return i;
    if(counts.view_count==counts.views.size()) { overflow_=add(overflow_,1); return UINT32_MAX; }
    const auto index=counts.view_count++;
    counts.views[index].description=view;
    return index;
}
void DepthInventory::bind(uint64_t dev,uint64_t cmd,uint64_t handle,const DepthDescription& desc,bool base) {
    bind_impl(dev,cmd,handle,desc,base,nullptr);
}
void DepthInventory::bind_view(uint64_t dev,uint64_t cmd,uint64_t handle,const DepthDescription& desc,const DepthView& view) {
    bind_impl(dev,cmd,handle,desc,view.canonical,&view);
}
void DepthInventory::bind_impl(uint64_t dev,uint64_t cmd,uint64_t handle,const DepthDescription& desc,bool base,const DepthView* view) {
    if(!cmd) return;
    if(!handle) { unbind(dev,cmd); return; }
    auto* d=device(dev); if(!d || d->effects) return;
    observe({dev,handle},desc);
    auto* r=resource({dev,handle});
    if(!r) { unbind(dev,cmd); return; }
    Binding* binding=nullptr;
    for(auto& b:bindings_) if(b.device==dev && b.command==cmd) { binding=&b; break; }
    if(!binding) for(auto& b:bindings_) if(!b.command) { binding=&b; break; }
    if(binding) {
        const auto index=view ? view_slot(r->current,*view) : UINT32_MAX;
        *binding={dev,cmd,r->id,base,index};
        if(index!=UINT32_MAX) {
            auto& v=r->current.views[index]; v.binds=add(v.binds,1);
        }
        return;
    }
    overflow_=add(overflow_,1);
}
void DepthInventory::unbind(uint64_t dev,uint64_t cmd) {
    for(auto& b:bindings_) if(b.device==dev && b.command==cmd) b={};
}
void DepthInventory::draw(uint64_t dev,uint64_t cmd,uint32_t elements,uint32_t instances,bool indirect) {
    auto* d=device(dev); if(!d || d->effects || !elements || !instances) return;
    for(const auto& b:bindings_) if(b.device==dev && b.command==cmd) {
        if(auto* r=by_id(b.resource_id)) {
            auto& c=r->current; c.draws=add(c.draws,indirect ? elements : 1);
            if(indirect) c.indirect=add(c.indirect,elements);
            else c.elements=add(c.elements,uint64_t(elements)*instances);
            if(!b.base_view) c.non_base_view_draws=add(c.non_base_view_draws,indirect ? elements : 1);
            if(b.view_index<c.view_count) {
                auto& v=c.views[b.view_index]; v.draws=add(v.draws,indirect ? elements : 1);
                if(indirect) v.indirect=add(v.indirect,elements);
                else v.elements=add(v.elements,uint64_t(elements)*instances);
            }
        }
        return;
    }
}
void DepthInventory::clear(DepthKey key,const DepthDescription& desc,double value,const DepthView* view) {
    auto* d=device(key.device);
    if(!d || d->effects || !std::isfinite(value) || value<0 || value>1) return;
    observe(key,desc);
    if(auto* r=resource(key)) {
        auto& c=r->current; c.clears=add(c.clears,1); c.last_clear=value; c.has_clear=true;
        if(view) {
            const auto index=view_slot(c,*view);
            if(index!=UINT32_MAX) {
                auto& v=c.views[index]; v.clears=add(v.clears,1); v.last_clear=value; v.has_clear=true;
            }
        }
    }
}
void DepthInventory::begin_effects(uint64_t dev,uint32_t w,uint32_t h) {
    auto* d=device(dev); if(!d) return;
    ++d->interval; d->width=w; d->height=h; d->effects=true;
    for(auto& r:resources_) if(r.id && r.key.device==dev) { r.last=r.current; r.current={}; }
}
void DepthInventory::end_effects(uint64_t dev) {
    for(auto& d:devices_) if(d.key==dev) d.effects=false;
    // ReShade may change native bindings during its pass. Require a fresh host bind.
    for(auto& b:bindings_) if(b.device==dev) b={};
}
void DepthInventory::destroy_device(uint64_t dev) {
    for(auto& r:resources_) if(r.key.device==dev) r={};
    for(auto& b:bindings_) if(b.device==dev) b={};
    for(auto& d:devices_) if(d.key==dev) d={};
}
std::array<DepthReport,DepthInventory::device_limit> DepthInventory::reports() const {
    std::array<DepthReport,device_limit> result{};
    for(size_t i=0;i<devices_.size();++i) {
        const auto& d=devices_[i]; if(!d.key) continue;
        auto& report=result[i]; report.device_id=d.id; report.interval=d.interval;
        report.width=d.width; report.height=d.height; report.overflow=overflow_;
        std::array<DepthCandidate,resource_limit> candidates{}; size_t count=0;
        for(const auto& r:resources_) if(r.id && r.key.device==d.key && (r.last.draws || r.last.clears))
            candidates[count++]={r.id,r.desc,r.last,r.desc.width==d.width && r.desc.height==d.height};
        std::sort(candidates.begin(),candidates.begin()+count,[](const auto& a,const auto& b) {
            if(a.matches_output!=b.matches_output) return a.matches_output;
            if(a.counts.draws!=b.counts.draws) return a.counts.draws>b.counts.draws;
            return a.id<b.id;
        });
        report.count=uint32_t(std::min(count,report.candidates.size()));
        std::copy_n(candidates.begin(),report.count,report.candidates.begin());
    }
    return result;
}
size_t DepthInventory::resource_count() const { return size_t(std::count_if(resources_.begin(),resources_.end(),[](const auto& r){return r.id!=0;})); }
size_t DepthInventory::binding_count() const { return size_t(std::count_if(bindings_.begin(),bindings_.end(),[](const auto& b){return b.command!=0;})); }
}
