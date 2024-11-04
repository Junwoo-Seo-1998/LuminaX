#define MaxLights 16
// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 1
#endif

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction; // directional/spot light only
    float FalloffEnd; // point/spot light only
    float3 Position; // point light only
    float SpotPower; // spot light only
};


SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
};

Texture2D gDiffuseMap : register(t0);

cbuffer cbMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
}

cbuffer cbPass : register(b2)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];
};



struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

    // Transform to homogeneous clip space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Assumes nonuniform scaling; otherwise, need to use inverse-transpose of world matrix
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);

    // Transform to homogeneous clip space
    vout.PosH = mul(posW, gViewProj);

    // Output vertex attributes for interpolation across triangle.
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;

    return vout;
}

#define PI 3.14159265358979f

float D_GGX(float NdotH, float roughness) {
    float a = NdotH * roughness;
    float k = roughness / (1.0f - NdotH * NdotH + a * a);
    return k * k * (1.0f / PI);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
	
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    NdotV = max(NdotV, 0.0);
    NdotL = max(NdotL, 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

float G_SmithGGXCorrelatedFast(float NdotV, float NdotL, float roughness) {
    float a = roughness;
    float GGXV = NdotL * (NdotV * (1.0f - a) + a);
    float GGXL = NdotV * (NdotL * (1.0f - a) + a);
    return 0.5f / (GGXV + GGXL);
}

float3 F_Schlick(float VdotH, float3 f0) {
    float f = pow(1.0f - VdotH, 5.0f);
    return f + f0 * (1.0f - f);
}

float3 fresnelSchlick(float VdotH, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}


struct Material
{
    float3 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float Metallic;
};

float3 BRDF(Light gLights[MaxLights], Material mat,
                       float3 pos, float3 normal, float3 view)
{
    float3 Lo = 0.0f;

    //dir only for now
    for(int i = 0; i < NUM_DIR_LIGHTS; ++i) 
    {
        // calculate per-light radiance
        // The vector from the surface to the light
        //float3 lightVec = gLights[i].Position - pos;
        float3 lightVec = -gLights[i].Direction;
        float distance = length(lightVec);
        lightVec /= distance;
        
        float3 halfVec = normalize(view + lightVec);

        //float attenuation = 1.0f / (distance * distance);

        //for now light color is white
        //float3 radiance   = float3(1.0f, 1.0f, 1.0f) * attenuation;
        float3 radiance = float3(1.0f, 1.0f, 1.0f);
        
        // cook-torrance brdf
        float D = D_GGX(dot(normal, halfVec), mat.Roughness);   
        //float D = DistributionGGX(normal, halfVec, mat.Roughness);
        //float G = G_SmithGGXCorrelatedFast(dot(normal, view), dot(normal, lightVec), mat.Roughness);
        float G = GeometrySmith(dot(normal, view), dot(normal, lightVec), mat.Roughness);
        
        float3 F = F_Schlick(max(dot(view, halfVec), 0.0f), mat.FresnelR0);
        //float3 F = fresnelSchlick(max(dot(view, halfVec), 0.0f), mat.FresnelR0);
        
        float3 kS = F;
        float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
        kD *= 1.0f - mat.Metallic;	  
        
        float3 numerator  = D * G * F;
        float denominator = 4.0f * max(dot(normal, view), 0.0f) * max(dot(normal, lightVec), 0.0f) + 0.0001f;
        float3 specular   = numerator / denominator;  
            
        // add to outgoing radiance Lo
        float NdotL = max(dot(normal, lightVec), 0.0f);                
        Lo += (kD * mat.DiffuseAlbedo / PI + specular) * radiance * NdotL; 
    }

    //not for now
    //float3 ambient = float3(0.03) * mat.DiffuseAlbedo * ao;
    float3 ambient = float3(0.03f, 0.03f, 0.03f) * mat.DiffuseAlbedo;
    float3 color = ambient + Lo;
    return color;
}



float4 PS(VertexOut pin) : SV_Target
{
    float3 diffuseAlbedo = (gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) * gDiffuseAlbedo).xyz;
    //float3 diffuseAlbedo = gDiffuseAlbedo;
    //float3 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC);
    // Interpolating normal can unnormalize it, so renormalize it.
    pin.NormalW = normalize(pin.NormalW);

    // Vector from point being lit to eye.
    float3 view = normalize(gEyePosW - pin.PosW);

    float3 F0 = 0.04f;
    float metallic = 0.0f;
    F0 = lerp(F0, diffuseAlbedo, metallic);

    Material mat = { diffuseAlbedo, gFresnelR0, gRoughness, 0.0f };
    float3 color=BRDF(gLights, mat, pin.PosW, pin.NormalW, view);

    //gama correction 
    color = color / (color + float3(1.0f, 1.0f, 1.0f));
    color = pow(color, 1.0f/2.2f);  

    return float4(color, 1.0f);
}