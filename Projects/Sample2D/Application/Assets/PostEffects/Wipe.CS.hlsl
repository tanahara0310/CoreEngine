#include "PostEffectMaterial.hlsli"

// シーンの遷移で、斜めの境目を左から右へ動かして画面を覆い、同じ向きに明ける
// progress（覆っている度合い）と transitionIn（明ける段なら 1）は遷移が毎フレーム入れる
struct Params
{
    float4 color;        // 覆う色
    float  progress;     // 0 = 素通し ／ 1 = 覆いきり
    float  transitionIn; // 0 = 覆う段 ／ 1 = 明ける段
    float  softness;     // 境目のぼかしの幅（画面の幅に対する割合）
};
ConstantBuffer<Params> gMaterial : register(b0);

// 境目を斜めにした分だけ、横の長さが伸びる
static const float kSlant = 0.25f;

POST_EFFECT_THREADS
void main(uint3 id : SV_DispatchThreadID)
{
    if (IsOutsideScreen(id.xy)) {
        return;
    }
    float4 color = LoadScreen(id.xy);

    float2 uv = PostEffectUV(id.xy);
    float x = uv.x + uv.y * kSlant;
    float softness = max(gMaterial.softness, 0.001f);
    float span = 1.0f + kSlant + softness;

    float covered;
    if (gMaterial.transitionIn > 0.5f) {
        // 明ける段：左から順に覆いを外す
        float edge = (1.0f - gMaterial.progress) * span;
        covered = smoothstep(edge - softness, edge, x);
    } else {
        // 覆う段：左から順に覆う
        float edge = gMaterial.progress * span;
        covered = 1.0f - smoothstep(edge - softness, edge, x);
    }
    color.rgb = lerp(color.rgb, gMaterial.color.rgb, covered * gMaterial.color.a);

    gOutput[id.xy] = color;
}
