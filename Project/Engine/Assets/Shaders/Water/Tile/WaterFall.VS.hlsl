// マップ端の水マスから垂れ下がる「落水カーテン」の頂点シェーダー。
// ------------------------------------------------------------
// 板はローカル XZ 平面を X 軸 ±90° で立てたもので、ローカル Z が世界の Y へ、
// ローカル X が世界の X へ写る（C++ 側 WaterWaveViewComponent::DrawFall）。
//
// 上端の頂点は必ず水面板の縁の頂点と同じ高さにする。そのために
// 「水面板と同じ GerstnerWave.hlsli を、同じ引数（ワールド XZ と時刻）で」評価する。
// 板は垂直なので 1 本の柱の中でワールド XZ は変わらず、restPos.xz をそのまま渡せば
// それが上端の XZ になる。ここを別式で近似すると、落ち口に隙間や段差が出る。
//
// 出力は Object3d.VS.hlsl と同じ VertexShaderOutput。
#include "Object3dVertex.hlsli"
#include "GerstnerWave.hlsli"

// ===== 落水の定数バッファ =====
// WaterFallShaderProvider::BindCustomResources() が "WaterFallConstants" という
// ブロック名でルートパラメータ番号を引いてバインドする（番号は直書きしない）。
// C++ 側の対応構造体: GameComponents::WaterFallConstants。
// b0〜b3・b7 は Object3d 系が使っているので、空いている b8 を使う。
// 同じ宣言を WaterFall.PS.hlsl にも置いてある。両方から読むと反射結果が
// D3D12_SHADER_VISIBILITY_ALL へ統合され、ルートパラメータは 1 本で済む。
cbuffer WaterFallConstants : register(b8)
{
    GerstnerWave gWaves[GERSTNER_MAX_WAVE_COUNT]; // 水面板と共有する波（最大 16 本）
    uint gActiveWaveCount; // 実際に評価する波本数
    float gTime; // 経過時間（秒）
    float gSurfaceY; // 静止水面のワールド Y（＝カーテンの上端）
    float gFallLength; // 落差[m]。カーテンの縦の長さ
    float gFlowSpeed; // 流れ落ちる速さ[m/s]
    float gFoamStrength; // 白泡の強さ
    float gPatternScale; // 模様の空間周波数スケール（1/マス）
    float gFallPadding;
};

/// @brief 波の変位を上端だけに効かせるための重み
/// @details 落ち始めてすぐ真っ直ぐへ移らせる。1 のまま下まで引きずると
///          カーテン全体が上下に揺れて「落ちている」ように見えなくなる。
float ResolveWaveFalloff(float fallT)
{
    const float t = saturate(1.0f - fallT * 3.0f);
    return t * t;
}

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    // ---- 1. ワールド変換 ----
    const float4 worldPos4 = mul(input.position, mtx.World);
    const float3 restPos = worldPos4.xyz;

    // 上端 0 → 下端 1。C++ 側が上端をちょうど gSurfaceY へ置いている。
    const float fallT = saturate((gSurfaceY - restPos.y) / max(gFallLength, 1.0e-4f));

    // ---- 2. 上端を水面板の縁へ合わせる ----
    float waveHeight = 0.0f;
    [unroll]
    for (int i = 0; i < GERSTNER_MAX_WAVE_COUNT; ++i)
    {
        if (i >= gActiveWaveCount)
        {
            break;
        }
        // 水面板（WaterWave.VS.hlsl）と同じ関数・同じ引数。Y 成分だけ使う。
        // プリセットの steepness は 0 なので XZ の横ずれは元々発生しない。
        waveHeight += EvaluateGerstnerWaveOffset(gWaves[i], gTime, restPos.xz).y;
    }
    const float3 totalOffset = float3(0.0f, waveHeight * ResolveWaveFalloff(fallT), 0.0f);

    // ---- 3. 出力組み立て ----
    VertexShaderOutput output;
    output.texcoord = input.texcoord;

    // 変位はワールド空間なので、クリップ空間へはローカルへ戻してから WVP を掛ける。
    // WorldInversTranspose = (World^-1)^T なので、転置すると World^-1 になる。
    const float3x3 invWorld3 = transpose((float3x3) mtx.WorldInversTranspose);
    const float3 offsetLS = mul(totalOffset, invWorld3);
    output.position = mul(input.position, mtx.WVP) + mul(float4(offsetLS, 0.0f), mtx.WVP);

    // カーテンは平らなので、法線は板の向き（ワールド ±Z）そのままでよい。
    // 見る側へ向け直すのと、流れによる揺らぎは PS 側で載せる。
    output.normal = normalize(mul(input.normal, (float3x3) mtx.WorldInversTranspose));
    output.tangent = normalize(mul(input.tangent, (float3x3) mtx.World));
    output.bitangent = normalize(cross(output.normal, output.tangent));

    const float3 worldPos = restPos + totalOffset;
    output.worldPosition = worldPos;
    output.lightSpacePos = mul(float4(worldPos, 1.0f), mtx.LightViewProjection);

    // モーションベクター用。前フレーム位置にも同じ変位を載せる
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(input.position, mtx.PrevWVP) + mul(float4(offsetLS, 0.0f), mtx.PrevWVP);

    return output;
}
