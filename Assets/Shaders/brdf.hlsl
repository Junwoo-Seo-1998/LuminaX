#include "common.hlsl"

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

    // Fetch the material data.
    MaterialData matData = gMaterialData[gMaterialIndex];
    
    // Output vertex attributes for interpolation across triangle.
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;

    return vout;
}


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

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max((float3) (1.0 - roughness), F0) -F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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
    
    // ambient lighting (we now use IBL as the ambient term)
    //float3 ambient = float3(0.03) * mat.DiffuseAlbedo * ao;
    float3 F = fresnelSchlickRoughness(max(dot(normal, view), 0.0f), mat.FresnelR0, mat.Roughness);
    float3 kS = F;
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    kD *= 1.0 - mat.Metallic;
    float3 irradiance = gIrradianceMap.Sample(gsamLinearWrap, normal).rgb;
    float3 diffuse = irradiance * mat.DiffuseAlbedo;
    float3 ambient = (kD * diffuse); // * ao;
    float3 color = ambient + Lo;
    return color;
}



float4 PS(VertexOut pin) : SV_Target
{
    MaterialData matData = gMaterialData[gMaterialIndex];
    
    float3 diffuseAlbedo = matData.DiffuseAlbedo.xyz;
    if(matData.DiffuseTexIndex!=-1)
        diffuseAlbedo *= gTextures[matData.DiffuseTexIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    
    // Interpolating normal can unnormalize it, so renormalize it.
    pin.NormalW = normalize(pin.NormalW);

    // Vector from point being lit to eye.
    float3 view = normalize(gEyePosW - pin.PosW);

    float3 F0 = 0.04f;
    float metallic = 0.0f;
    F0 = lerp(F0, diffuseAlbedo, metallic);

    Material mat = { diffuseAlbedo, F0, matData.Roughness, 0.0f };
    float3 color=BRDF(gLights, mat, pin.PosW, pin.NormalW, view);

    //gama correction 
    color = color / (color + float3(1.0f, 1.0f, 1.0f));
    color = pow(color, 1.0f/2.2f);  

    return float4(color, 1.0f);
}