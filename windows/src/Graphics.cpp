#include "Graphics.hpp"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <thread>

using Microsoft::WRL::ComPtr;

static void checked(HRESULT hr) {
    if (SUCCEEDED(hr))
        return;
    std::ostringstream message;
    message << "Graphics HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
    throw std::runtime_error(message.str());
}

// All colors leaving the pixel shader are premultiplied. The D2D target and
// composition swapchain use the same convention, including transparent pixels.
static constexpr char SHADER[] = R"(
cbuffer Shape : register(b0) {
    float4 box;
    float4 color;
    float4 metrics; // viewport DIP width/height, radius, unused
};
struct Vertex { float4 position : SV_POSITION; float2 local : TEXCOORD0; };
Vertex vs(uint id : SV_VertexID) {
    float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    Vertex v;
    v.local = corners[id] * box.zw;
    float2 p = box.xy + v.local;
    v.position = float4(p.x / metrics.x * 2 - 1, 1 - p.y / metrics.y * 2, 0, 1);
    return v;
}
float4 ps(Vertex v) : SV_TARGET {
    float radius = min(metrics.z, min(box.z, box.w) * 0.5);
    float2 q = abs(v.local - box.zw * 0.5) - box.zw * 0.5 + radius;
    float d = length(max(q,0)) + min(max(q.x,q.y),0) - radius;
    float coverage = saturate(0.5 - d);
    float border = saturate(1.5 + d);
    float3 rgb = color.rgb * (1 - 0.18 * v.local.y / max(box.w,1));
    rgb = lerp(rgb, min(rgb + 0.16,1), border);
    float a = color.a * coverage;
    return float4(rgb * a, a);
}
)";

namespace WindowsUI {
    struct CGraphicEngineCore::SResources {
        ComPtr<ID3D11Device>           device;
        ComPtr<ID3D11DeviceContext>    context;
        ComPtr<IDXGISwapChain1>        swap;
        ComPtr<IDCompositionDevice>    composition;
        ComPtr<IDCompositionTarget>    target;
        ComPtr<IDCompositionVisual>    visual;
        ComPtr<ID3D11RenderTargetView> renderTarget;
        ComPtr<ID3D11VertexShader>     vertex;
        ComPtr<ID3D11PixelShader>      pixel;
        ComPtr<ID3D11Buffer>           constants;
        ComPtr<ID3D11BlendState>       blend;
        ComPtr<ID3D11RasterizerState>  rasterizer;
        ComPtr<ID2D1Factory1>          d2dFactory;
        ComPtr<ID2D1Device>            d2dDevice;
        ComPtr<ID2D1DeviceContext>     d2d;
        ComPtr<ID2D1Bitmap1>           bitmap;
        ComPtr<IDWriteFactory2>        write;
        ComPtr<IDWriteTextFormat>      format;
        ComPtr<IDWriteFontFallback>    fallback;
        ComPtr<ID2D1SolidColorBrush>   brush;
        std::thread::id                owner = std::this_thread::get_id();
    };

    CGraphicEngineCore::CGraphicEngineCore(HWND window) : m_window(window) {
        initialize();
    }

    CGraphicEngineCore::~CGraphicEngineCore() = default;

    void CGraphicEngineCore::initialize() {
        m_resources             = std::make_unique<SResources>();
        auto&             r     = *m_resources;
        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &r.device, &level, &r.context);
        if (FAILED(hr))
            checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &r.device, &level, &r.context));
        ComPtr<IDXGIDevice> dxgi;
        checked(r.device.As(&dxgi));
        ComPtr<IDXGIAdapter> adapter;
        checked(dxgi->GetAdapter(&adapter));
        ComPtr<IDXGIFactory2> factory;
        checked(adapter->GetParent(IID_PPV_ARGS(&factory)));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width            = m_width;
        desc.Height           = m_height;
        desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount      = 2;
        desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;
        checked(factory->CreateSwapChainForComposition(r.device.Get(), &desc, nullptr, &r.swap));
        checked(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&r.composition)));
        checked(r.composition->CreateTargetForHwnd(m_window, TRUE, &r.target));
        checked(r.composition->CreateVisual(&r.visual));
        checked(r.visual->SetContent(r.swap.Get()));
        checked(r.target->SetRoot(r.visual.Get()));
        checked(r.composition->Commit());

        ComPtr<ID3DBlob> vertexCode, pixelCode, errors;
        checked(D3DCompile(SHADER, sizeof(SHADER) - 1, nullptr, nullptr, nullptr, "vs", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vertexCode, &errors));
        checked(D3DCompile(SHADER, sizeof(SHADER) - 1, nullptr, nullptr, nullptr, "ps", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &pixelCode, &errors));
        checked(r.device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &r.vertex));
        checked(r.device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &r.pixel));
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 48;
        buffer.Usage     = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        checked(r.device->CreateBuffer(&buffer, nullptr, &r.constants));
        D3D11_BLEND_DESC blend{};
        auto&            b = blend.RenderTarget[0];
        b.BlendEnable      = TRUE;
        b.SrcBlend = b.SrcBlendAlpha = D3D11_BLEND_ONE;
        b.DestBlend = b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        b.BlendOp = b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTargetWriteMask    = D3D11_COLOR_WRITE_ENABLE_ALL;
        checked(r.device->CreateBlendState(&blend, &r.blend));
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode      = D3D11_FILL_SOLID;
        raster.CullMode      = D3D11_CULL_NONE;
        raster.ScissorEnable = TRUE;
        checked(r.device->CreateRasterizerState(&raster, &r.rasterizer));
        checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&r.d2dFactory)));
        checked(r.d2dFactory->CreateDevice(dxgi.Get(), &r.d2dDevice));
        checked(r.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &r.d2d));
        checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), reinterpret_cast<IUnknown**>(r.write.GetAddressOf())));
        checked(r.write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15, L"en-US", &r.format));
        checked(r.write->GetSystemFontFallback(&r.fallback));
        checked(r.d2d->CreateSolidColorBrush(D2D1::ColorF(0.92F, 0.95F, 1), &r.brush));
        createTarget();
    }

    void CGraphicEngineCore::createTarget() {
        auto&                   r = *m_resources;
        ComPtr<ID3D11Texture2D> texture;
        checked(r.swap->GetBuffer(0, IID_PPV_ARGS(&texture)));
        checked(r.device->CreateRenderTargetView(texture.Get(), nullptr, &r.renderTarget));
        ComPtr<IDXGISurface> surface;
        checked(texture.As(&surface));
        const auto properties =
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                    static_cast<float>(m_dpi), static_cast<float>(m_dpi));
        checked(r.d2d->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &r.bitmap));
        r.d2d->SetTarget(r.bitmap.Get());
        r.d2d->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
        r.d2d->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }

    void CGraphicEngineCore::resize(unsigned width, unsigned height, unsigned dpi) {
        if (m_width == width && m_height == height && m_dpi == dpi)
            return;
        m_width  = std::max(1U, width);
        m_height = std::max(1U, height);
        m_dpi    = std::max(96U, dpi);
        auto& r  = *m_resources;
        r.d2d->SetTarget(nullptr);
        r.bitmap.Reset();
        r.renderTarget.Reset();
        r.context->ClearState();
        const HRESULT hr = r.swap->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            initialize();
            return;
        }
        checked(hr);
        createTarget();
    }

    bool CGraphicEngineCore::draw(const std::vector<SDrawCommand>& commands) {
        auto& r = *m_resources;
        if (r.owner != std::this_thread::get_id())
            throw std::logic_error("D3D immediate context used outside UI thread");
        const float scale   = m_dpi / 96.0F;
        const float clear[] = {
            0,
            0,
            0,
            0,
        };
        r.context->ClearRenderTargetView(r.renderTarget.Get(), clear);
        ID3D11RenderTargetView* target = r.renderTarget.Get();
        r.context->OMSetRenderTargets(1, &target, nullptr);
        r.context->OMSetBlendState(r.blend.Get(), nullptr, 0xffffffff);
        r.context->RSSetState(r.rasterizer.Get());
        const D3D11_VIEWPORT viewport{
            0, 0, static_cast<float>(m_width), static_cast<float>(m_height), 0, 1,
        };
        r.context->RSSetViewports(1, &viewport);
        r.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        r.context->VSSetShader(r.vertex.Get(), nullptr, 0);
        r.context->PSSetShader(r.pixel.Get(), nullptr, 0);
        ID3D11Buffer* constants = r.constants.Get();
        r.context->VSSetConstantBuffers(0, 1, &constants);
        r.context->PSSetConstantBuffers(0, 1, &constants);
        for (const auto& c : commands) {
            const D3D11_RECT clip{
                static_cast<LONG>(c.clip.x * scale),
                static_cast<LONG>(c.clip.y * scale),
                static_cast<LONG>((c.clip.x + c.clip.width) * scale),
                static_cast<LONG>((c.clip.y + c.clip.height) * scale),
            };
            r.context->RSSetScissorRects(1, &clip);
            const float data[] = {
                c.box.x,          c.box.y,  c.box.width, c.box.height, c.active ? 0.12F : 0.08F, c.active ? 0.46F : 0.12F, c.active ? 0.55F : 0.18F, 0.97F, m_width / scale,
                m_height / scale, c.radius, 0,
            };
            r.context->UpdateSubresource(r.constants.Get(), 0, nullptr, data, 0, 0);
            r.context->Draw(6, 0);
        }
        r.context->OMSetRenderTargets(0, nullptr, nullptr);
        r.context->Flush();
        r.d2d->BeginDraw();
        for (const auto& c : commands) {
            if (c.text.empty())
                continue;
            const auto clip = D2D1::RectF(c.clip.x, c.clip.y, c.clip.x + c.clip.width, c.clip.y + c.clip.height);
            r.d2d->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
            ComPtr<IDWriteTextLayout> layout;
            checked(r.write->CreateTextLayout(c.text.data(), static_cast<UINT32>(c.text.size()), r.format.Get(), std::max(1.0F, c.box.width - 24),
                                              std::max(1.0F, c.box.height - 12), &layout));
            ComPtr<IDWriteTextLayout2> layout2;
            checked(layout.As(&layout2));
            checked(layout2->SetFontFallback(r.fallback.Get()));
            r.d2d->DrawTextLayout(D2D1::Point2F(c.box.x + 12, c.box.y + 8), layout.Get(), r.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            r.d2d->PopAxisAlignedClip();
        }
        const HRESULT textResult = r.d2d->EndDraw();
        if (textResult == D2DERR_RECREATE_TARGET) {
            initialize();
            return false;
        }
        checked(textResult);
        const HRESULT hr = r.swap->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            initialize();
            return false;
        }
        checked(hr);
        ++m_frames;
        return true;
    }

    unsigned long long CGraphicEngineCore::frames() const {
        return m_frames;
    }
}
