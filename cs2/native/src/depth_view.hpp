#pragma once
#include "depth_inventory.hpp"
#include <reshade_api_resource.hpp>

namespace cc {
static_assert(uint32_t(DepthViewType::texture_2d)==uint32_t(reshade::api::resource_view_type::texture_2d));
static_assert(uint32_t(DepthViewType::texture_2d_array)==uint32_t(reshade::api::resource_view_type::texture_2d_array));
static_assert(uint32_t(DepthViewType::texture_2d_multisample)==uint32_t(reshade::api::resource_view_type::texture_2d_multisample));
static_assert(uint32_t(DepthViewType::texture_2d_multisample_array)==uint32_t(reshade::api::resource_view_type::texture_2d_multisample_array));

inline DepthView describe_depth_view(const reshade::api::resource_view_desc& view,const DepthDescription& resource) {
    namespace api=reshade::api;
    const auto type=static_cast<DepthViewType>(view.type);
    // A DSV must use the typed depth format for its resource's typed/typeless
    // family. An SRV interpretation or an unknown format is not sufficient.
    bool compatible=false;
    switch(view.format) {
    case api::format::d16_unorm:
        compatible=resource.format==uint32_t(api::format::d16_unorm) || resource.format==uint32_t(api::format::r16_typeless); break;
    case api::format::d24_unorm_s8_uint:
        compatible=resource.format==uint32_t(api::format::d24_unorm_s8_uint) || resource.format==uint32_t(api::format::r24_g8_typeless); break;
    case api::format::d32_float:
        compatible=resource.format==uint32_t(api::format::d32_float) || resource.format==uint32_t(api::format::r32_typeless); break;
    case api::format::d32_float_s8_uint:
        compatible=resource.format==uint32_t(api::format::d32_float_s8_uint) || resource.format==uint32_t(api::format::r32_g8_typeless); break;
    default: break;
    }
    switch(view.type) {
    case api::resource_view_type::texture_2d:
    case api::resource_view_type::texture_2d_array:
    case api::resource_view_type::texture_2d_multisample:
    case api::resource_view_type::texture_2d_multisample_array:
        return normalize_depth_view(type,uint32_t(view.format),view.texture.first_level,view.texture.levels,
            view.texture.first_layer,view.texture.layers,resource,compatible);
    default:
        // Do not inspect a texture union for an unexpected view type.
        return {type,uint32_t(view.format),{},{},compatible,false};
    }
}
}
