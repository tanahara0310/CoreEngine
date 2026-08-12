// ToneMapping.CS.hlsl - ACES トーンマッピング コンピュートシェーダー

#include "ColorSpace.hlsli" // ACESFilm / LinearToSRGB / SRGBToLinear

Texture2D<float4> gTexture : register(t0);
RWTexture2D<float4> gOutput : register(u0);

/// @brief Interleaved Gradient Noise（Jorge Jimenez）
/// @details 数命令で画面全体に均一に散る。乱数表もサンプラーも要らない。
///          白色ノイズより粒が揃うので、ディザ用途ではこちらが好ましい。
float InterleavedGradientNoise(float2 pixelPosition)
{
    return frac(52.9829189f * frac(dot(pixelPosition, float2(0.06711056f, 0.00583715f))));
}

cbuffer ScreenParams : register(b0)
{
    uint screenWidth;
    uint screenHeight;
    float exposureEV; // 露出補正 [EV]。トーンカーブ適用前に exp2(EV) を乗算（0 = 従来動作）
    uint toneMapOperator; // 0=ACES / 1=GT / 2=AgX（CVar r.ToneMapping.Operator）
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float4 color = gTexture.Load(int3(dispatchId.xy, 0));

    // 露出補正（トーンカーブ適用前のリニア HDR 値に対して行う）
    color.rgb *= exp2(exposureEV);

    // HDR → LDR 変換。オペレータの違いはハイライトの白への抜け方に最も出る。
    // ACES: コントラスト強め・肩が早い / GT: 中間調が素直・リニア区間持ち / AgX: 高彩度光源に最強
    if (toneMapOperator == 1)
    {
        color.rgb = GTTonemap(color.rgb);
    }
    else if (toneMapOperator == 2)
    {
        color.rgb = AgXTonemap(color.rgb);
    }
    else
    {
        color.rgb = ACESFilm(color.rgb);
    }

    // NOTE: ガンマ補正（リニア→sRGB）はここで行わない。
    // バックバッファのRTVフォーマットが DXGI_FORMAT_R8G8B8A8_UNORM_SRGB のため、
    // GPUがハードウェアで自動的に変換を行う。ここで行うと二重補正になる。

    // ===== 出力ディザ（バンディング対策・常時） =====
    // 最終的に 8bit へ丸めるときの量子化誤差をランダム化し、空や霧のような
    // 緩やかなグラデーションに出る縞を消す。加える量は 1LSB 未満なので粒は見えない。
    //
    // 【なぜトーンマッパで掛けるか】
    // 最終書き込みを行う FullScreen.PS は「バックバッファ経路」専用で、
    // エディタの Game ビューは PostEffectFinal を ImGui::Image で直接表示するため
    // そこを通らない。チェーン内で掛けておけば全ての表示経路に効く。
    // ここで足したノイズは中間バッファが float なので減衰せず、最終的な 8bit 丸めまで残る。
    //
    // 【なぜ sRGB 空間で足すか】
    // 丸めが起きるのは sRGB エンコード後。リニア空間で 1/255 を足すと、
    // sRGB カーブが暗部を大きく引き伸ばすため暗部だけ 1LSB を大幅に超え、
    // ノイズがはっきり見えてしまう。
    float3 encoded = LinearToSRGB(color.rgb);
    encoded += (InterleavedGradientNoise(float2(dispatchId.xy)) - 0.5f) / 255.0f;
    color.rgb = SRGBToLinear(encoded);

    gOutput[dispatchId.xy] = color;
}
