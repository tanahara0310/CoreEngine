#ifndef LIGHTING_HLSLI

struct LightingResult
{
    float3 diffuse;
    float3 specular;
};


float CalculateToonFactor(float NdotL, float toonThreshold)
{
    
    if (NdotL < toonThreshold * 0.3f)
    {
        return 0.1f;
    }
    else if (NdotL < toonThreshold * 0.7f)
    {
        return 0.4f;
    }
    else if (NdotL < toonThreshold)
    {
        return 0.7f;
    }
    else
    {
        return 1.0f;
    }
}

float CalculateDiffuseFactor(float NdotL, int shadingMode, float toonThreshold)
{
    if (shadingMode == 1)
    {
        return saturate(NdotL);
    }
    else if (shadingMode == 2)
    {
        float halfLambert = NdotL * 0.5f + 0.5f;
        return saturate(halfLambert * halfLambert);
    }
    else if (shadingMode == 3)
    {
        return CalculateToonFactor(NdotL, toonThreshold);
    }
    
    return 1.0f;
}

float CalculateSpecularFactor(float3 normal, float3 lightDir, float3 toEye, float shininess, int shadingMode)
{
    float3 halfVector = normalize(lightDir + toEye);
    float NDotH = dot(normalize(normal), halfVector);
    float specularPow = pow(saturate(NDotH), shininess);
    
    if (shadingMode == 3)
    {
        specularPow = specularPow > 0.8f ? 1.0f : 0.0f;
    }
    
    return specularPow;
}

LightingResult CalculateLighting(
    float3 normal,
    float3 lightDir,
    float3 lightColor,
    float intensity,
    float attenuation,
    float3 toEye,
    float3 materialColor,
    float4 textureColor,
    float shininess,
    int shadingMode,
    float toonThreshold)
{
    LightingResult result;
    
    float NdotL = dot(normalize(normal), lightDir);
    float diffuseFactor = CalculateDiffuseFactor(NdotL, shadingMode, toonThreshold);
    result.diffuse = materialColor * textureColor.rgb * lightColor * 
                     diffuseFactor * intensity * attenuation;
    
    float specularFactor = CalculateSpecularFactor(normal, lightDir, toEye, shininess, shadingMode);
    result.specular = lightColor * intensity * specularFactor * attenuation;
    
    return result;
}

LightingResult CalculateDirectionalLight(
    float3 normal,
    float3 lightDirection,
    float3 lightColor,
    float intensity,
    float3 toEye,
    float3 materialColor,
    float4 textureColor,
    float shininess,
    int shadingMode,
    float toonThreshold)
{
    return CalculateLighting(
        normal,
        -lightDirection,
        lightColor,
        intensity,
        1.0f,
        toEye,
        materialColor,
        textureColor,
        shininess,
        shadingMode,
        toonThreshold
    );
}

LightingResult CalculatePointLight(
    float3 normal,
    float3 lightPosition,
    float3 worldPosition,
    float3 lightColor,
    float intensity,
    float radius,
    float decay,
    float3 toEye,
    float3 materialColor,
    float4 textureColor,
    float shininess,
    int shadingMode,
    float toonThreshold)
{
    float3 lightDir = normalize(lightPosition - worldPosition);
    float distance = length(lightPosition - worldPosition);
    float attenuation = pow(saturate(-distance / radius + 1.0f), decay);
    
    return CalculateLighting(
        normal,
        lightDir,
        lightColor,
        intensity,
        attenuation,
        toEye,
        materialColor,
        textureColor,
        shininess,
        shadingMode,
        toonThreshold
    );
}


LightingResult CalculateSpotLight(
    float3 normal,
    float3 lightPosition,
    float3 lightDirection,
    float3 worldPosition,
    float3 lightColor,
    float intensity,
    float distance,
    float decay,
    float cosAngle,
    float cosFalloffStart,
    float3 toEye,
    float3 materialColor,
    float4 textureColor,
    float shininess,
    int shadingMode,
    float toonThreshold)
{
   
    float3 lightDir = normalize(lightPosition - worldPosition);
    float3 spotLightDirectionOnSurface = -lightDir;
    
    
    float cosAngleValue = dot(spotLightDirectionOnSurface, lightDirection);
    float denominator = max(cosAngle - cosFalloffStart, 0.001f);
    float falloffFactor = saturate((cosAngleValue - cosAngle) / denominator);
    
   
    float distanceToLight = length(lightPosition - worldPosition);
    float distanceAttenuation = pow(saturate(-distanceToLight / distance + 1.0f), decay);
    
    float attenuation = distanceAttenuation * falloffFactor;
    
    return CalculateLighting(
        normal,
        lightDir,
        lightColor,
        intensity,
        attenuation,
        toEye,
        materialColor,
        textureColor,
        shininess,
        shadingMode,
        toonThreshold
    );
}

#endif
