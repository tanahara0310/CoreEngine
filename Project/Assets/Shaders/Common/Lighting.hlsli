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

/// @brief エリアライト（矩形）の計算
LightingResult CalculateAreaLight(
    float3 normal,
    float3 lightPosition,
    float3 lightDirection,
    float3 lightRight,
    float3 lightUp,
    float lightWidth,
    float lightHeight,
    float3 worldPosition,
    float3 lightColor,
    float intensity,
    float decay,
    float3 toEye,
    float3 materialColor,
    float4 textureColor,
    float shininess,
    int shadingMode,
    float toonThreshold)
{
    // ライト中心から表面への方向ベクトル
    float3 toSurface = worldPosition - lightPosition;
    
    // ライト平面との距離（法線方向の成分）
    float distAlongNormal = dot(toSurface, lightDirection);
    
    // ライトの裏側にある場合は照明なし
    if (distAlongNormal <= 0.0f)
    {
        LightingResult result;
        result.diffuse = float3(0.0f, 0.0f, 0.0f);
        result.specular = float3(0.0f, 0.0f, 0.0f);
        return result;
    }
    
    // 表面の点をライト平面に投影
    float3 projectedPoint = worldPosition - lightDirection * distAlongNormal;
    
    // 投影された点の矩形ローカル座標系での位置
    float3 localPos = projectedPoint - lightPosition;
    float u = dot(localPos, lightRight);
    float v = dot(localPos, lightUp);
    
    // 矩形の範囲内にクランプ
    float clampedU = clamp(u, -lightWidth * 0.5f, lightWidth * 0.5f);
    float clampedV = clamp(v, -lightHeight * 0.5f, lightHeight * 0.5f);
    
    // 矩形上の最近接点
    float3 closestPoint = lightPosition + lightRight * clampedU + lightUp * clampedV;
    
    // 最近接点への方向と距離
    float3 lightDir = closestPoint - worldPosition;
    float distance = length(lightDir);
    lightDir = normalize(lightDir);
    
    // 減衰の最大距離を矩形のサイズに基づいて設定
    float maxDistance = max(lightWidth, lightHeight) * 1.5f;
    float distanceAttenuation = pow(saturate(1.0f - distance / maxDistance), decay);
    
    // ライト平面の法線との角度による減衰（ライトが表面を向いているか）
    float normalDot = saturate(dot(-lightDir, lightDirection));
    
    float attenuation = distanceAttenuation * normalDot;
    
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

