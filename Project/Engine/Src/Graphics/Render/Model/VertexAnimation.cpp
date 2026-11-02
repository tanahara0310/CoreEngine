#include "pch.h"
#include "VertexAnimation.h"

#include "Scene/Environment/WindCVars.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    namespace VertexAnimationCVars
    {
        CVar<bool> Enabled{ "r.VertexAnim.Enabled", true,
            "頂点アニメーション（植物の揺れ・海草・魚の泳ぎ）を動かす（off で止まる）" };
        CVar<float> ReferenceWindSpeed{ "r.VertexAnim.ReferenceWindSpeed", 15.0f,
            "この風速 [m/s]（シーンの風 r.Wind.Speed）のとき植物がモデル作成時の想定どおりに揺れる",
            CVarRange{ 1.0f, 40.0f } };
        CVar<float> WindScale{ "r.VertexAnim.WindScale", 1.0f,
            "植物の揺れの倍率", CVarRange{ 0.0f, 3.0f } };
        CVar<float> WaterScale{ "r.VertexAnim.WaterScale", 1.0f,
            "海草の揺れ（波の寄せ返し）の倍率", CVarRange{ 0.0f, 3.0f } };
    }

    namespace
    {
        /// @brief シーンの水のうねりの進む向き（水もうねりも無ければ無し）
        std::optional<Vector2> sSwellDirection;

        /// @brief XZ の向きを正規化する（長さ 0 なら +X）
        Vector2 NormalizeXZ(const Vector2& v)
        {
            const float len = std::sqrt(v.x * v.x + v.y * v.y);
            if (len < 1e-5f) {
                return Vector2{ 1.0f, 0.0f };
            }
            return Vector2{ v.x / len, v.y / len };
        }
    }

    VertexAnimationParams VertexAnimationParams::Build()
    {
        VertexAnimationParams params{};
        if (!VertexAnimationCVars::Enabled.Get()) {
            // 強さ 0・時間 0 で止める（魚は時間 0 の姿勢で止まる）
            return params;
        }

        // 折り返した時間。前フレームの値は折り返し直後に負になるが、シェーダーの式は
        // kWrapSeconds の周期関数なので、そのまま使ってもモーションベクターはつながる
        params.time = std::fmod(Time::TimeSinceStartup(), kWrapSeconds);
        params.prevTime = params.time - Time::DeltaTime();

        // 植物: 海と同じシーンの風。風速が基準のとき強さ 1（上限 2）
        params.windDir = WindCVars::NormalizedDirection();
        const float reference = (std::max)(VertexAnimationCVars::ReferenceWindSpeed.Get(), 0.1f);
        params.windStrength = std::clamp(WindCVars::Speed.Get() / reference, 0.0f, 2.0f)
            * VertexAnimationCVars::WindScale.Get();

        // 海草: うねりがあればその進行方向に寄せ返す（無ければ風向き）
        params.waterDir = sSwellDirection ? NormalizeXZ(*sSwellDirection) : params.windDir;
        params.waterStrength = VertexAnimationCVars::WaterScale.Get();
        return params;
    }

    void VertexAnimationParams::SetSwellDirection(const std::optional<Vector2>& direction)
    {
        sSwellDirection = direction;
    }
}
