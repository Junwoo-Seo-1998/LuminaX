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
    float3 PosL : POSITION;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

	// Use local vertex position as cubemap lookup vector.
    vout.PosL = vin.PosL;
	
	// Transform to world space.
    float4 posW = float4(vin.PosL, 1.0f);

	// Always center sky about camera.
    posW.xyz += gEyePosW;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
    vout.PosH = mul(posW, gViewProj).xyww;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // the sample direction equals the hemisphere's orientation 
    float3 normal = normalize(pin.PosL);
  
    float3 irradiance = (float3)0.0f;
    
    
    float3 up = (abs(normal.y) < 0.999) ? float3(0.f, 1.f, 0.f) : float3(0.f, 0.f, 1.f);
    float3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));
    
    float3x3 toWorld = float3x3(right, up, normal);

    float sampleDelta = 0.025;
    float nrSamples = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // spherical to cartesian (in tangent space)
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            float3 sampleVec = normalize(mul(tangentSample, toWorld));

            irradiance += gCubeMap.Sample(gsamLinearWrap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
    
    return float4(irradiance, 1.0f);
}