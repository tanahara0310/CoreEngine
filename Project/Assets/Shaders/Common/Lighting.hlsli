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

LightingResult CalculateAreaLight(
    float3 normal,
    float3 lightPosition,
    float3 lightNormal,
    float3 lightRight,
    float3 lightUp,
    float lightWidth,
    float lightHeight,
    float3 worldPosition,
    float3 lightColor,
    float intensity,
    float range,
    float3 toEye,
    float3 materialColor,
    float4 textureColor,
    float shininess,
    int shadingMode,
    float toonThreshold)
{
    // ワールド座標から光源中心へのベクトル
	float3 toLight = lightPosition - worldPosition;
	float3 toLightNormalized = normalize(toLight);
    
    // 光源の各軸（すでに正規化されているはず）
	float3 axisRight = lightRight;
	float3 axisUp = lightUp;
	float3 axisNormal = lightNormal;
    
    // 光源平面への距離
	float distToPlane = dot(toLight, axisNormal);
    
    // 光源平面上への投影点
	float3 projectedPoint = worldPosition + axisNormal * distToPlane;
    
    // 光源中心から投影点へのオフセット
	float3 offset = projectedPoint - lightPosition;
    
    // ローカル座標での位置（u, v）
	float u = dot(offset, axisRight);
	float v = dot(offset, axisUp);
    
    // 矩形の範囲
	float halfW = lightWidth * 0.5f;
	float halfH = lightHeight * 0.5f;
    
    // 矩形内にクランプ
	float clampedU = clamp(u, -halfW, halfW);
	float clampedV = clamp(v, -halfH, halfH);
    
    // 矩形上の最近接点
	float3 closestPoint = lightPosition + axisRight * clampedU + axisUp * clampedV;
    
    // 最近接点への方向と距離
	float3 toClosest = closestPoint - worldPosition;
	float dist = length(toClosest);
	float3 L = toClosest / max(dist, 0.001f); // 正規化（ゼロ除算回避）
    
    // 距離減衰
	float distFactor = 1.0f - saturate(dist / range);
	float distAttenuation = distFactor * distFactor;
    
    // 矩形の形状係数
	float shapeFactor = 1.0f;
    
    // 矩形外の場合の減衰
	float outsideU = max(0.0f, abs(u) - halfW);
	float outsideV = max(0.0f, abs(v) - halfH);
	float outsideDist = sqrt(outsideU * outsideU + outsideV * outsideV);
    
	if (outsideDist > 0.001f)
	{
        // エッジからの距離による減衰
		float falloffRange = max(halfW, halfH);
		shapeFactor = 1.0f - saturate(outsideDist / falloffRange);
		shapeFactor = shapeFactor * shapeFactor * shapeFactor; // より急峻な減衰
	}
    
    // 光源の向きによる減衰（光源は片面のみ）
	float facingFactor = max(0.0f, dot(axisNormal, -L));
    
    // 総合減衰
	float finalAttenuation = distAttenuation * shapeFactor * facingFactor;
    
    // ライティング計算
	return CalculateLighting(
        normal,
        L,
        lightColor,
        intensity,
        finalAttenuation,
        toEye,
        materialColor,
        textureColor,
        shininess,
        shadingMode,
        toonThreshold
    );
}

#endif

