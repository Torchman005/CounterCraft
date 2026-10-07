// Optional diagnostic inset. This is not CS2 world/depth composition.
texture BackBuffer : COLOR;
sampler HostSampler { Texture = BackBuffer; };
texture GuestColor : COUNTERCRAFT_COLOR;
sampler GuestSampler { Texture = GuestColor; MinFilter = POINT; MagFilter = POINT; };
texture GuestDepth : COUNTERCRAFT_DEPTH;
sampler GuestDepthSampler { Texture = GuestDepth; MinFilter = POINT; MagFilter = POINT; };
uniform bool CCActive = false;
uniform float2 CCSize = float2(0, 0);

void ProbeVS(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD) {
    uv = float2((id << 1) & 2, id & 2);
    position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 ProbePS(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    float4 host = tex2D(HostSampler, uv);
    if (!CCActive || any(CCSize <= 0)) return host;
    float2 inset = (uv - float2(0.69, 0.04)) / float2(0.28, 0.28);
    if (any(inset < 0) || any(inset > 1)) return host;
    if (any(inset < 0.015) || any(inset > 0.985)) return float4(1, 0.1, 0.1, 1);
    float2 guestUV = float2(inset.x, 1 - inset.y);
    // Bottom strip displays raw guest depth to make both uploaded textures observable.
    if (inset.y > 0.9) return float4(tex2D(GuestDepthSampler, guestUV).rrr, 1);
    return tex2D(GuestSampler, guestUV);
}
technique CounterCraftProbe < ui_label = "CounterCraft diagnostic inset (no host depth)"; > {
    pass { VertexShader = ProbeVS; PixelShader = ProbePS; }
}
