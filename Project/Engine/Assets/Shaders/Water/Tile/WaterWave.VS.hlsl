// 水マス 1 枚ぶんの板を Gerstner 波で変位させる頂点シェーダー。
// ------------------------------------------------------------
// 変位量は「ワールド座標と時刻」だけから決まる。マスごとに別々の板を
// 並べても、隣り合う板が共有する縁の頂点は必ず同じ値になるので継ぎ目が出ない。
// （ローカル座標で変位させると、板の端どうしで位相が食い違って段差になる）
//
// 出力は Object3d.VS.hlsl と同じ VertexShaderOutput。
// そのためピクセルシェーダーは既定の Object3d.PS.hlsl をそのまま使える。
// エンジンの hlsli はファイル名だけで引ける（AssetDatabase がシェーダーの
// 置き場を全部 -I に積むので、Application 側からでも相対パスは要らない）。
#include "Object3dVertex.hlsli"
// 波の数式は水面描画・コースティクスと共有しているものを必ず経由する。
// ここへ自前の sin を書くと「同じ波を別々に評価して食い違う」型のバグを生む。
#include "GerstnerWave.hlsli"

// ===== Gerstner 波の定数バッファ =====
// WaterWaveShaderProvider::BindCustomResources() が、この "WaterConstants" という
// ブロック名でルートパラメータ番号を引いてバインドする（番号は直書きしない）。
// C++ 側の対応構造体: WaterConstants（WaterSurfaceTypes.h。名前空間なしのグローバル）。
// b0〜b3・b7 は Object3d 系が使っているので、空いている b8 を使う。
cbuffer WaterConstants : register(b8)
{
    GerstnerWave gWaves[GERSTNER_MAX_WAVE_COUNT]; // 重ね合わせる波（最大 16 本）
    uint gActiveWaveCount; // 実際に評価する波本数
    float gTime; // 経過時間（秒）
    float2 gWavePadding;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    // ---- 1. ワールド変換 ----
    const float4 worldPos4 = mul(input.position, mtx.World);
    // 法線・接線の解析偏微分は変位前のワールド静止位置で評価する
    const float3 restPos = worldPos4.xyz;

    // ---- 2. Gerstner Wave 頂点変位（ワールド空間） ----
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);
    float3 dPdX = float3(1.0f, 0.0f, 0.0f);
    float3 dPdZ = float3(0.0f, 0.0f, 1.0f);
    [unroll]
    for (int i = 0; i < GERSTNER_MAX_WAVE_COUNT; ++i)
    {
        if (i >= gActiveWaveCount)
        {
            break;
        }

        totalOffset += EvaluateGerstnerWaveOffset(gWaves[i], gTime, restPos.xz);
        AccumulateGerstnerWaveDerivatives(gWaves[i], gTime, restPos.xz, dPdX, dPdZ);
    }

    // ---- 3. 出力組み立て ----
    VertexShaderOutput output;
    output.texcoord = input.texcoord;

    // 変位はワールド空間なので、クリップ空間へはローカルへ戻してから WVP を掛ける。
    // WorldInversTranspose = (World^-1)^T なので、転置すると World^-1 になる。
    // 板をスケールしても変位量が一緒に拡大されないよう、正しい逆行列で戻すこと。
    const float3x3 invWorld3 = transpose((float3x3) mtx.WorldInversTranspose);
    const float3 offsetLS = mul(totalOffset, invWorld3);
    const float4 offsetClip = mul(float4(offsetLS, 0.0f), mtx.WVP);
    output.position = mul(input.position, mtx.WVP) + offsetClip;

    // dPdX / dPdZ から再構成した基底は既にワールド空間。
    // ここで World / WorldInversTranspose を掛け直すとスケール依存の歪みが出る。
    output.normal = BuildGerstnerNormal(dPdX, dPdZ);
    output.tangent = normalize(dPdX);
    output.bitangent = normalize(dPdZ);

    const float3 worldPos = restPos + totalOffset;
    output.worldPosition = worldPos;
    output.lightSpacePos = mul(float4(worldPos, 1.0f), mtx.LightViewProjection);

    // モーションベクター用。前フレーム位置にも同じ変位を載せる
    // （載せないと、静止した板が波の分だけ毎フレーム動いたことになり TAA がぶれる）
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(input.position, mtx.PrevWVP) + mul(float4(offsetLS, 0.0f), mtx.PrevWVP);

    return output;
}
