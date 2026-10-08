// Offline modes: upload inset, full-client gameplay, calibrated static world fusion.
#include "WorldFusion.fxh"
texture BackBuffer : COLOR;
sampler HostSampler { Texture = BackBuffer; };
texture GuestColor : COUNTERCRAFT_COLOR;
sampler GuestSampler { Texture = GuestColor; MinFilter = POINT; MagFilter = POINT; };
texture GuestDepth : COUNTERCRAFT_DEPTH;
sampler GuestDepthSampler { Texture = GuestDepth; MinFilter = POINT; MagFilter = POINT; };
uniform bool CCActive = false;
uniform float2 CCSize = float2(0, 0);
uniform bool CCFullClient = false;
texture HostWorldDepth : COUNTERCRAFT_HOST_WORLD;
sampler HostWorldSampler { Texture = HostWorldDepth; MinFilter = POINT; MagFilter = POINT; };
texture HostFinalDepth : COUNTERCRAFT_HOST_FINAL;
sampler HostFinalSampler { Texture = HostFinalDepth; MinFilter = POINT; MagFilter = POINT; };
uniform bool CCFusion = false;
uniform bool CCWorldFusion = false;
texture HostCamera : COUNTERCRAFT_HOST_CAMERA;
sampler HostCameraSampler { Texture = HostCamera; MinFilter = POINT; MagFilter = POINT; };
uniform float4 CCExpectedCamera[16];
texture CameraMatch { Width = 1; Height = 1; Format = R8; };
sampler CameraMatchSampler { Texture = CameraMatch; MinFilter = POINT; MagFilter = POINT; };
uniform bool CCHostReversed = false;
uniform bool CCAllowFinalClear = false;
uniform float4 CCGuestPlanes; // near, far, guest/host horizontal/vertical projection scales
uniform float4 CCHostProjection; // clip Z=a*z+b; clip W=c*z+d
uniform float4 CCHostRange; // viewport min, max, observed clear, blocks per Source unit
uniform bool CCGui = false;
uniform float2 CCCursor = float2(.5,.5);

void ProbeVS(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD) {
    uv = float2((id << 1) & 2, id & 2);
    position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 MatchCameraPS(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    if(CCActive && CCFusion && !CCFullClient) {
        for(int matrixIndex=0;matrixIndex<4;matrixIndex++)for(int row=0;row<4;row++) {
            float4 actual=tex2Dlod(HostCameraSampler,float4((row+.5)/4,(matrixIndex+.5)/4,0,0));
            float4 expected=CCExpectedCamera[matrixIndex*4+row];
            if(!CCMatrixMatches(actual,expected,matrixIndex))return float4(0,0,0,1);
        }
        return float4(1,0,0,1);
    }
    return float4(0,0,0,1);
}
float4 ProbePS(float4 position : SV_Position, float2 uv : TEXCOORD) : SV_Target {
    float4 host = tex2D(HostSampler, uv);
    if (!CCActive || any(CCSize <= 0)) return host;
    if(CCFusion && !CCFullClient) {
        if(tex2D(CameraMatchSampler,float2(.5,.5)).r<.5)return host;
        float2 world = tex2D(HostWorldSampler,uv).rg;
        float2 finalDepth = tex2D(HostFinalSampler,uv).rg;
        float2 guestUV = (uv-.5)*CCGuestPlanes.zw+.5;
        if(any(guestUV<0) || any(guestUV>1))return host;
        guestUV.y=1-guestUV.y;
        float guestZ=tex2D(GuestDepthSampler,guestUV).r;
        return CCGuestVisible(world,finalDepth,guestZ,CCGuestPlanes,CCHostProjection,CCHostRange,CCHostReversed,CCAllowFinalClear)
            ?tex2D(GuestSampler,guestUV):host;
    }
    if(CCWorldFusion)return host;
    if (CCFullClient) {
        float2 hostSize = float2(BUFFER_WIDTH, BUFFER_HEIGHT);
        float scale = min(hostSize.x/CCSize.x, hostSize.y/CCSize.y);
        float2 fit = CCSize*scale/hostSize;
        float2 guest = (uv-(1-fit)*.5)/fit;
        if(any(guest<0) || any(guest>1)) return float4(0,0,0,1);
        float4 color = tex2D(GuestSampler, float2(guest.x,1-guest.y));
        float2 cursor = abs((guest-CCCursor)*CCSize);
        if(CCGui && ((cursor.x<1.5 && cursor.y<7) || (cursor.y<1.5 && cursor.x<7)))return float4(1,1,1,1);
        return color;
    }
    float2 inset = (uv - float2(0.69, 0.04)) / float2(0.28, 0.28);
    if (any(inset < 0) || any(inset > 1)) return host;
    if (any(inset < 0.015) || any(inset > 0.985)) return float4(1, 0.1, 0.1, 1);
    float2 guestUV = float2(inset.x, 1 - inset.y);
    // Bottom strip displays raw guest depth to make both uploaded textures observable.
    if (inset.y > 0.9) return float4(tex2D(GuestDepthSampler, guestUV).rrr, 1);
    return tex2D(GuestSampler, guestUV);
}
technique CounterCraftProbe < ui_label = "CounterCraft diagnostic inset (no host depth)"; > {
    pass { VertexShader = ProbeVS; PixelShader = MatchCameraPS; RenderTarget = CameraMatch; }
    pass { VertexShader = ProbeVS; PixelShader = ProbePS; }
}
