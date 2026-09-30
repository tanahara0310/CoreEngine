#pragma once

#include "Graphics/Shader/CBufferLayout.h"
#include "Math/Vector/Vector2.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    /// @brief 頂点アニメーション（植物の揺れ・海草の寄せ返し・魚の泳ぎ）のフレーム共通定数
    /// @details モデル描画のシェーダーへルート定数 gVertexAnim（b9）として差す（BaseModelRenderer）。
    ///          種類と倍率はマテリアル（MaterialConstants::vertexAnimation など）、
    ///          頂点ごとの値は VertexData::animData（glTF の TEXCOORD_1 / TEXCOORD_2）が持つ。
    /// @warning Shaders/Include/Object/VertexAnimation.hlsli の VertexAnimationParams と一致させること
    struct VertexAnimationParams {
        float time = 0.0f;              ///< 秒（kWrapSeconds で折り返す）
        float prevTime = 0.0f;          ///< 前フレームの time（モーションベクター用）
        float windStrength = 0.0f;      ///< 風の強さ（1 = モデル作成時の想定）
        float waterStrength = 0.0f;     ///< 水の寄せ返しの強さ（海草）
        Vector2 windDir{ 1.0f, 0.0f };  ///< 風下の向き（ワールド XZ、正規化済み）
        Vector2 waterDir{ 1.0f, 0.0f }; ///< 寄せ返しの向き（ワールド XZ、正規化済み）

        /// @brief time の折り返し周期 [s]
        /// @details シェーダーは全ての周波数をこの逆数の整数倍に丸めるので、折り返しても動きが途切れない。
        ///          float の累積時間は長時間で精度が落ちるため、小さい値に保つ目的もある
        static constexpr float kWrapSeconds = 240.0f;

        /// @brief 今フレームの値を作る
        /// @details 時間は Time（ポーズ・タイムスケールに従う。水面と同じ）、風向・風速は海（FFT）の
        ///          CVar と同じものを使うので、波と植物が同じ風で動く。倍率は r.VertexAnim.* の CVar。
        static VertexAnimationParams Build();
    };

    static constexpr Cb::Field kVertexAnimationParamsFields[] = {
        CB_FIELD(VertexAnimationParams, time), CB_FIELD(VertexAnimationParams, prevTime),
        CB_FIELD(VertexAnimationParams, windStrength), CB_FIELD(VertexAnimationParams, waterStrength),
        CB_FIELD(VertexAnimationParams, windDir), CB_FIELD(VertexAnimationParams, waterDir),
    };
    CB_VERIFY_LAYOUT(VertexAnimationParams, kVertexAnimationParamsFields);

    /// @brief 頂点アニメーションの調整用 CVar（CVar ツリーの r.VertexAnim から編集・保存できる）
    namespace VertexAnimationCVars
    {
        extern CVar<bool>  Enabled;            ///< 頂点アニメーション全体の有効 / 無効
        extern CVar<float> ReferenceWindSpeed; ///< この風速 [m/s] のとき植物が「強さ 1」で揺れる
        extern CVar<float> WindScale;          ///< 植物の揺れの倍率
        extern CVar<float> WaterScale;         ///< 海草の揺れの倍率
    }
}
