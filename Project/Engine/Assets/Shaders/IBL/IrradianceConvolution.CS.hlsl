/// @file IrradianceConvolution.CS.hlsl
/// @brief Irradiance Map（拡散反射環境マップ）の生成
/// @details 環境マップから拡散反射成分を半球積分で計算し、
///          低解像度キューブマップ（32x32推奨）として保存

static const float PI = 3.14159265359;
static const float TWO_PI = 6.28318530718;
static const float HALF_PI = 1.57079632679;

// 入力: 環境マップ（キューブマップ）
TextureCube<float4> gInputEnvironment : register(t0);
SamplerState gSampler : register(s0);

// 出力: Irradianceキューブマップ（RWTexture2DArray: 6面）
RWTexture2DArray<float4> gOutputIrradiance : register(u0);

// ===================================================================
// キューブマップユーティリティ
// ===================================================================

/// @brief キューブマップのUV座標から方向ベクトルを計算
/// @param face キューブマップの面（0-5）
/// @param uv UV座標 [0,1]
/// @return 正規化された方向ベクトル
/// @details DirectX標準キューブマップ座標系に準拠
float3 GetDirectionFromCubemapUV(uint face, float2 uv)
{
    // [0,1] -> [-1,1] に変換
    float2 coord = uv * 2.0 - 1.0;
    
    float3 dir;
    switch(face)
    {
        case 0: // +X (右)
            dir = normalize(float3(1.0, -coord.y, -coord.x));
            break;
        case 1: // -X (左)
            dir = normalize(float3(-1.0, -coord.y, coord.x));
            break;
        case 2: // +Y (上)
            dir = normalize(float3(coord.x, 1.0, coord.y));
            break;
        case 3: // -Y (下)
            dir = normalize(float3(coord.x, -1.0, -coord.y));
            break;
        case 4: // +Z (前)
            dir = normalize(float3(coord.x, -coord.y, 1.0));
            break;
        case 5: // -Z (後ろ)
            dir = normalize(float3(-coord.x, -coord.y, -1.0));
            break;
        default:
            dir = float3(0.0, 1.0, 0.0);
            break;
    }
    return dir;
}

// ===================================================================
// タンジェント空間構築
// ===================================================================

/// @brief 法線周りのタンジェント空間を構築
/// @param N 法線ベクトル
/// @param outRight 出力: 右方向ベクトル
/// @param outUp 出力: 上方向ベクトル
void GetTangentBasis(float3 N, out float3 outRight, out float3 outUp)
{
    // 法線と平行でない適当なベクトルを選択
    float3 arbitrary = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    
    // グラム・シュミット正規直交化
    outRight = normalize(cross(arbitrary, N));
    outUp = cross(N, outRight);
}

// ===================================================================
// 半球積分（Irradiance計算）
// ===================================================================

/// @brief 法線方向Nに対する拡散反射Irradianceを計算
/// @param N 法線ベクトル（正規化済み）
/// @return 積分されたIrradiance色
float3 ComputeIrradiance(float3 N)
{
    // タンジェント空間構築
    float3 right, up;
    GetTangentBasis(N, right, up);
    
    float3 irradiance = float3(0, 0, 0);
    float totalWeight = 0.0;
    
    // サンプリングパラメータ（超高品質）
    const uint phiSamples = 1024;   // 方位角サンプル数
    const uint thetaSamples = 256;  // 極角サンプル数
    
    float deltaPhi = TWO_PI / float(phiSamples);
    float deltaTheta = HALF_PI / float(thetaSamples);
    
    // 適切なミップレベルを計算（半球全体からサンプリングするため）
    float mipLevel = 1.0; // わずかにぼかしたレベルを使用
    
    // 半球上でサンプリング
    for (uint phiIndex = 0; phiIndex < phiSamples; ++phiIndex)
    {
        float phi = (float(phiIndex) + 0.5) * deltaPhi; // テクセル中心サンプリング
        
        for (uint thetaIndex = 0; thetaIndex < thetaSamples; ++thetaIndex)
        {
            float theta = (float(thetaIndex) + 0.5) * deltaTheta; // テクセル中心サンプリング
            
            // 球面座標からカルテシアン座標へ変換
            float sinTheta = sin(theta);
            float cosTheta = cos(theta);
            
            float3 tangentSample = float3(
                sinTheta * cos(phi),
                sinTheta * sin(phi),
                cosTheta
            );
            
            // タンジェント空間からワールド空間へ変換
            float3 sampleVec = normalize(tangentSample.x * right + 
                                         tangentSample.y * up + 
                                         tangentSample.z * N);
            
            // 環境マップをサンプリング（適切なミップレベル使用）
            float3 envColor = gInputEnvironment.SampleLevel(gSampler, sampleVec, mipLevel).rgb;
            
            // Lambertian拡散反射の積分
            // ∫∫ L(ω) * cos(θ) * sin(θ) dω
            float weight = cosTheta * sinTheta;
            irradiance += envColor * weight;
            totalWeight += weight;
        }
    }
    
    // π倍して正規化
    irradiance = PI * irradiance / max(totalWeight, 0.0001);
    
    return irradiance;
}

// ===================================================================
// Compute Shader メインエントリーポイント
// ===================================================================

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint face = DTid.z;
    uint2 coords = DTid.xy;
    
    // 出力テクスチャの解像度を取得
    uint width, height, elements;
    gOutputIrradiance.GetDimensions(width, height, elements);
    
    // 範囲外チェック
    if (coords.x >= width || coords.y >= height || face >= 6)
        return;
    
    // このピクセルの法線方向を計算（キューブマップ方向）
    float2 uv = (float2(coords) + 0.5) / float2(width, height);
    float3 N = GetDirectionFromCubemapUV(face, uv);
    
    // Irradiance計算（半球積分）
    float3 irradiance = ComputeIrradiance(N);
    
    // 結果を書き込み
    gOutputIrradiance[uint3(coords, face)] = float4(irradiance, 1.0);
}
