#pragma once
#include "frame.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>

namespace cc {
using Microsoft::WRL::ComPtr;
void check(HRESULT result, const char* operation);
struct Texture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
};
Texture texture(ID3D11Device*, int width, int height, DXGI_FORMAT format, const void* data = nullptr, bool target = false);
class Compositor {
public:
    // Isolated D3D11 lab device only. A real host must use ReShade's protected effect pass.
    Compositor(ID3D11Device*, ID3D11DeviceContext*);
    void upload(const Frame&);
    void draw(ID3D11ShaderResourceView* host_color, ID3D11ShaderResourceView* host_depth,
              ID3D11RenderTargetView* output, int width, int height,
              float host_near, float host_far, bool host_reversed, bool guest_active = true);
    void clear();
private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VertexShader> vertex_;
    ComPtr<ID3D11PixelShader> pixel_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11DepthStencilState> no_depth_;
    ComPtr<ID3D11RasterizerState> raster_;
    Texture color_, depth_;
    Metadata metadata_{};
};
std::vector<uint8_t> read_rgba(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*);
void save_bmp(const std::filesystem::path&, int width, int height, std::span<const uint8_t> rgba);
}
