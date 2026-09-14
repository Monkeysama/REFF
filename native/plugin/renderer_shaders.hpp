#pragma once
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>

namespace reff {
// DX12 面板与光标共用的 HLSL 源码；运行时和独立测试编译同一份源码，避免 C++ 编译通过却无法显示面板。
inline constexpr char overlay_shader[] = R"(
// cbuffer 按 16 字节寄存器对齐，显式使用三个 float4，避免 C++ 根常量与 HLSL 偏移不一致。
cbuffer DrawParameters : register(b0) { float4 cursorRect; float4 targetAndMode; float4 contentScalePad; };
struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) { V v; v.uv = float2((id << 1) & 2, id & 2);
if (cursorRect.z > 0) {
    // 光标使用有边界的两个三角形，不能沿用 UV 达到 2 的全屏覆盖三角形。
    const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1), float2(0,1), float2(1,0), float2(1,1)};
    v.uv = corners[id];
    float2 cursorPixel = cursorRect.xy + v.uv * cursorRect.zw;
    v.position = float4(cursorPixel / targetAndMode.xy * float2(2,-2) + float2(-1,1), 0, 1);
} else if (cursorRect.z == 0) v.position = float4(v.uv * float2(2,-2) + float2(-1,1), 0, 1);
else v.position = float4(0, 0, 0, 1); return v; }
Texture2D picture : register(t0); Texture2D cursorPicture : register(t1); SamplerState linearSampler : register(s0);
float4 ps(V v) : SV_Target {
    if (cursorRect.z == 0) return picture.Sample(linearSampler, v.uv * contentScalePad.xy);
    if (cursorRect.z < 0) discard;
    if (any(v.uv < 0) || any(v.uv > 1)) discard;
    if (targetAndMode.z > 0) {
        float icon = (targetAndMode.z - 1.0) * 0.25;
        return cursorPicture.Sample(linearSampler, float2(icon + v.uv.x * 0.25, v.uv.y));
    }
    // 在固定 18x24 设计坐标内测试封闭箭头轮廓；尖端为热点，内部黑色描边在亮背景上仍清晰。
    const float2 outline[7] = {float2(0,0), float2(0,21), float2(5,16), float2(9,24),
        float2(12,22), float2(8,14), float2(17,14)};
    float2 samplePixel = v.uv * float2(18,24);
    bool inside = false;
    float edgeDistance = 100;
    [unroll] for (int i = 0; i < 7; ++i) {
        float2 a = outline[i], b = outline[(i+1)%7];
        float2 edge = b-a;
        float along = saturate(dot(samplePixel-a, edge) / dot(edge,edge));
        edgeDistance = min(edgeDistance, length(samplePixel-a-along*edge));
        if ((a.y > samplePixel.y) != (b.y > samplePixel.y)) {
            if (samplePixel.x < a.x + (samplePixel.y-a.y)*edge.x/edge.y) inside = !inside;
        }
    }
    if (!inside) discard;
    float shade = edgeDistance < 1 ? 0.08 : 1.0;
    return float4(shade, shade, shade, 1);
}
)";

// Present 初始化阶段或独立测试中同步编译；ComPtr 持有字节码，失败时保留入口、HRESULT 和编译器原始诊断。
inline Microsoft::WRL::ComPtr<ID3DBlob> compile_overlay_shader(const char* entry, const char* profile) {
    Microsoft::WRL::ComPtr<ID3DBlob> bytecode, diagnostics;
    const HRESULT result = D3DCompile(overlay_shader, sizeof(overlay_shader) - 1, "REFF.overlay.hlsl", nullptr, nullptr,
        entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &diagnostics);
    if (FAILED(result)) {
        std::string message = std::string("D3DCompile ") + entry + "/" + profile + " HRESULT=" + std::to_string(result);
        if (diagnostics) message += ": " + std::string(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize());
        throw std::runtime_error(message);
    }
    return bytecode;
}
}
