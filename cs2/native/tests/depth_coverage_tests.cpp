#include "depth_coverage.hpp"
#include "coverage_journal.hpp"
#include "compositor.hpp"
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <iostream>
#include <limits>

using namespace cc;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    auto h=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    if(FAILED(h))check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Coverage hardware device");
    ComPtr<ID3D11InfoQueue> info;device.As(&info);
    std::array<std::array<float,2>,4> pixels{{{.75f,.75f},{.75f,.75f},{.75f,.75f},{.75f,.75f}}};
    D3D11_TEXTURE2D_DESC desc{};desc.Width=4;desc.Height=1;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R32G32_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels.data(),sizeof(pixels),0};
    ComPtr<ID3D11Texture2D> world,current;check(device->CreateTexture2D(&desc,&initial,&world),"Coverage world");check(device->CreateTexture2D(&desc,&initial,&current),"Coverage current");
    DepthCoverage coverage(device.Get());coverage.reset(context.Get(),world.Get());
    auto sentinel=texture(device.Get(),4,1,DXGI_FORMAT_R32_FLOAT);
    ID3D11ShaderResourceView* srvs[]{sentinel.srv.Get(),sentinel.srv.Get()};context->CSSetShaderResources(0,2,srvs);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=512;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> buffer;
    check(device->CreateBuffer(&bd,nullptr,&buffer),"Coverage sentinel CB");
    ComPtr<ID3D11DeviceContext1> c1;context.As(&c1);auto* cb=buffer.Get();UINT first=16,count=16;
    if(c1)c1->CSSetConstantBuffers1(0,1,&cb,&first,&count);else context->CSSetConstantBuffers(0,1,&cb);
    unsigned oracle_case=0;
    auto verify=[&](const std::array<float,4>& expected){
        ++oracle_case;
        auto d=desc;d.Format=DXGI_FORMAT_R32_FLOAT;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"Coverage staging");
        context->CopyResource(staging.Get(),coverage.output());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Coverage oracle map");
        std::array<float,4> actual{};std::memcpy(actual.data(),mapped.pData,sizeof(actual));context->Unmap(staging.Get(),0);
        if(actual!=expected){std::cerr<<"Coverage case "<<oracle_case<<" expected";
            for(auto v:expected)std::cerr<<' '<<v;std::cerr<<" actual";
            for(auto v:actual)std::cerr<<' '<<v;std::cerr<<'\n';}
        require(actual==expected,"Accumulated mask lost/added an occluder");
        ID3D11ShaderResourceView* retained[2]{};context->CSGetShaderResources(0,2,retained);
        const bool restored=retained[0]==srvs[0] && retained[1]==srvs[1];for(auto* s:retained)if(s)s->Release();require(restored,"Coverage changed CS SRVs");
        ComPtr<ID3D11Buffer> retained_cb;UINT f{},n{};
        if(c1){c1->CSGetConstantBuffers1(0,1,&retained_cb,&f,&n);require(f==first && n==count,"Coverage changed CB range");}
        else context->CSGetConstantBuffers(0,1,&retained_cb);
        require(retained_cb.Get()==buffer.Get(),"Coverage changed host CB");
        ComPtr<ID3D11ComputeShader> shader;context->CSGetShader(&shader,nullptr,nullptr);require(!shader,"Coverage changed host CS");
        ComPtr<ID3D11UnorderedAccessView> uav;context->CSGetUnorderedAccessViews(0,1,&uav);require(!uav,"Coverage left UAV bound");
    };
    coverage.accumulate(context.Get(),world.Get(),current.Get(),false,1);verify({0,0,0,0});
    pixels[0]={.01f,.01f};context->UpdateSubresource(current.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),current.Get(),false,1);verify({1,0,0,0}); // weapon before clear
    pixels.fill({1,1});context->UpdateSubresource(current.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),current.Get(),true,1);verify({1,0,0,0}); // clear cannot erase weapon
    pixels[1]={.9f,.9f};pixels[2]={1,.99f};pixels[3]={std::numeric_limits<float>::quiet_NaN(),1};
    context->UpdateSubresource(current.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),current.Get(),true,1);verify({1,1,1,1}); // sky/later pass, MSAA edge, NaN
    pixels.fill({1,1});context->UpdateSubresource(current.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),current.Get(),true,1);verify({1,1,1,1}); // second clear
    coverage.reset(context.Get(),world.Get());verify({0,0,0,0}); // next interval resets
    ComPtr<ID3D11Texture2D> second;
    check(device->CreateTexture2D(&desc,&initial,&second),"Second coverage resource");
    pixels={{{.75f,.75f},{.02f,.02f},{.75f,.75f},{.75f,.75f}}};
    context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),second.Get(),false,1,true);verify({0,1,0,0});
    pixels.fill({1,1});context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),second.Get(),true,1,true);verify({0,1,0,0});
    pixels={{{.01f,.01f},{.75f,.75f},{.75f,.75f},{.75f,.75f}}};
    context->UpdateSubresource(current.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),current.Get(),false,1);verify({1,1,0,0}); // same-resource write still accumulates
    coverage.reset(context.Get(),world.Get());
    pixels.fill({.7500001f,.7500001f});context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),second.Get(),false,1,true);verify({0,0,0,0}); // resolved quantization
    pixels.fill({.7f,.8f});context->UpdateSubresource(world.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    pixels={{{.75f,.75f},{.65f,.75f},{.75f,.85f},{.7f,.8f}}};
    context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),second.Get(),false,1,true);verify({0,1,1,0}); // overlap is insufficient
    pixels={{{std::numeric_limits<float>::quiet_NaN(),.75f},{.75f,.75f},{.75f,.75f},{.75f,std::numeric_limits<float>::infinity()}}};
    context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),second.Get(),false,1,true);verify({1,1,1,1}); // foreign invalid ranges stay protected
    pixels.fill({.75f,.75f});context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    coverage.accumulate(context.Get(),world.Get(),second.Get(),false,1,true);verify({1,1,1,1}); // no later resolve erases coverage
    // Replay actual GPU overwrites through the same journal used by the
    // adapter: copy destinations may never be bound when effects begin.
    pixels.fill({.75f,.75f});context->UpdateSubresource(world.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    context->CopyResource(current.Get(),world.Get());context->CopyResource(second.Get(),world.Get());
    coverage.reset(context.Get(),world.Get());CoverageJournal journal;journal.begin(1);
    auto capture=[&](uint64_t resource,bool cleared){
        coverage.accumulate(context.Get(),world.Get(),resource==1?current.Get():second.Get(),cleared,1,resource!=1);
    };
    pixels[0]={.01f,.01f};context->UpdateSubresource(current.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    journal.copy(1,capture);context->CopyResource(current.Get(),world.Get());verify({1,0,0,0});
    journal.copy(2,capture);context->CopyResource(second.Get(),world.Get());
    pixels.fill({.75f,.75f});pixels[1]={.02f,.02f};
    context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    journal.clear(2,true,capture);pixels.fill({1,1});
    context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);verify({1,1,0,0});
    journal.copy(2,capture);context->CopyResource(second.Get(),world.Get());
    pixels.fill({.75f,.75f});pixels[2]={.03f,.03f};
    context->UpdateSubresource(second.Get(),0,nullptr,pixels.data(),sizeof(pixels),0);
    journal.capture(1,capture,true);journal.flush(capture);verify({1,1,1,0});
    journal.reset();coverage.reset(context.Get(),world.Get());verify({0,0,0,0});
    auto small_desc=desc;small_desc.Width=2;
    std::array<std::array<float,2>,2> small_pixels{{{.75f,.75f},{.04f,.04f}}};
    D3D11_SUBRESOURCE_DATA small_initial{small_pixels.data(),sizeof(small_pixels),0};
    ComPtr<ID3D11Texture2D> smaller;check(device->CreateTexture2D(&small_desc,&small_initial,&smaller),"Reduced coverage resource");
    coverage.accumulate(context.Get(),world.Get(),smaller.Get(),false,1,true);verify({0,0,1,1});
    small_pixels.fill({1,1});context->UpdateSubresource(smaller.Get(),0,nullptr,small_pixels.data(),sizeof(small_pixels),0);
    coverage.accumulate(context.Get(),world.Get(),smaller.Get(),true,1,true);verify({0,0,1,1});
    coverage.reset(context.Get(),world.Get());small_pixels[0]={.03f,.04f};
    context->UpdateSubresource(smaller.Get(),0,nullptr,small_pixels.data(),sizeof(small_pixels),0);
    coverage.accumulate(context.Get(),world.Get(),smaller.Get(),true,1,true);verify({1,1,0,0});
    bool shape_refused=false;
    try{coverage.accumulate(context.Get(),world.Get(),smaller.Get(),false,1);}catch(const std::exception&){shape_refused=true;}
    require(shape_refused,"Same-resource coverage accepted a reduced shape");
    // Odd dimensions exercise both axes and the non-integral edge footprints
    // observed in live reduced-resolution passes. Read back pitched rows.
    {
        auto wide_desc=desc;wide_desc.Width=5;wide_desc.Height=3;
        std::array<std::array<float,2>,15> wide_pixels;wide_pixels.fill({.75f,.75f});
        D3D11_SUBRESOURCE_DATA wide_initial{wide_pixels.data(),5*sizeof(wide_pixels[0]),0};
        ComPtr<ID3D11Texture2D> wide;check(device->CreateTexture2D(&wide_desc,&wide_initial,&wide),"Odd coverage world");
        auto grid_desc=desc;grid_desc.Width=grid_desc.Height=2;
        std::array<std::array<float,2>,4> grid_pixels{{{.75f,.75f},{.03f,.03f},{.04f,.04f},{.75f,.75f}}};
        D3D11_SUBRESOURCE_DATA grid_initial{grid_pixels.data(),2*sizeof(grid_pixels[0]),0};
        ComPtr<ID3D11Texture2D> grid;check(device->CreateTexture2D(&grid_desc,&grid_initial,&grid),"Reduced coverage grid");
        DepthCoverage odd(device.Get());odd.reset(context.Get(),wide.Get());
        odd.accumulate(context.Get(),wide.Get(),grid.Get(),false,1,true);
        wide_desc.Format=DXGI_FORMAT_R32_FLOAT;wide_desc.BindFlags=0;
        wide_desc.Usage=D3D11_USAGE_STAGING;wide_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&wide_desc,nullptr,&staging),"Odd coverage staging");
        context->CopyResource(staging.Get(),odd.output());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Odd coverage map");
        std::array<float,15> actual{};
        for(size_t row=0;row<3;++row)std::memcpy(actual.data()+row*5,static_cast<const uint8_t*>(mapped.pData)+row*mapped.RowPitch,5*sizeof(float));
        context->Unmap(staging.Get(),0);
        require(actual==std::array<float,15>{0,0,0,1,1,0,0,0,1,1,1,1,1,0,0},"Odd two-axis coverage footprint lost an occluder");
    }
    bool refused=false;try{coverage.accumulate(context.Get(),world.Get(),sentinel.texture.Get(),false,1);}catch(const std::exception&){refused=true;}
    require(refused,"Coverage accepted invalid format");
    ComPtr<ID3D11Device> foreign_device;ComPtr<ID3D11DeviceContext> foreign_context;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&foreign_device,nullptr,&foreign_context),"Foreign coverage device");
    ComPtr<ID3D11Texture2D> foreign;
    check(foreign_device->CreateTexture2D(&desc,&initial,&foreign),"Foreign coverage texture");
    refused=false;try{coverage.accumulate(context.Get(),world.Get(),foreign.Get(),false,1);}catch(const std::exception&){refused=true;}
    require(refused,"Device marker admitted foreign coverage texture");
    refused=false;try{coverage.reset(foreign_context.Get(),world.Get());}catch(const std::exception&){refused=true;}
    require(refused,"Device marker admitted foreign coverage context");
    if(info)for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> storage(bytes);auto* m=reinterpret_cast<D3D11_MESSAGE*>(storage.data());info->GetMessage(i,m,&bytes);require(m->Severity>D3D11_MESSAGE_SEVERITY_WARNING,"Coverage D3D11 warning/error");}
    context->ClearState();std::cout<<"Hardware accumulated coverage, clear retention and state restoration passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
