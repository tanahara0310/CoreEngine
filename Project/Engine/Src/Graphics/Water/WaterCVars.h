#pragma once

#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    /// @brief 水面パラメータの CVar 群（単一情報源。Phase 5 で Water.json / UI キャッシュから移行）
    /// @details 定義は WaterCVars.cpp。永続化は CVars.json（CVarSettingsSection）に一本化され、
    ///          エンジン側は WaterRenderFeature::ApplySettingsFromCVars が毎フレームここから
    ///          プルして各所（WaterPlaneObject / FFTOceanManager / コースティクス / RT屈折）へ
    ///          反映する。UI は CVar ツリー（Engine Settings）と水面パネルの両方から編集できる
    ///          （どちらも同じストレージを書くため食い違いは起きない）。
    ///          既定値は 2026-08 時点の調整値（旧 Water.json）を引き継いでいる。
    namespace WaterCVars
    {
        // ---- 見た目 ----
        extern CVar<Vector4> BaseColor;
        extern CVar<float>   Roughness;
        extern CVar<float>   Metallic;
        extern CVar<bool>    IBLEnabled;
        extern CVar<float>   FresnelScale;
        extern CVar<float>   FresnelF0;
        extern CVar<Vector2> ScrollSpeed;
        extern CVar<Vector2> UVTiling;
        extern CVar<bool>    DepthFadeEnabled;
        extern CVar<bool>    WriteMotionVector;

        // ---- 水質（ベース値。実効値は Effective* で濁度と合成）----
        extern CVar<Vector3> AbsorptionBase;
        extern CVar<Vector3> ScatteringBase;
        extern CVar<float>   Turbidity;
        extern CVar<int>     JerlovPreset; // コンボ表示の復元用（NoUI。実体は AbsorptionBase 等）

        // ---- 泡（whitecap）----
        extern CVar<bool>    FoamEnabled;
        extern CVar<float>   FoamBias;
        extern CVar<float>   FoamGain;
        extern CVar<float>   FoamOpacity;
        extern CVar<Vector3> FoamCascadeWeights;
        extern CVar<float>   FoamDecaySeconds;

        // ---- FFT Ocean ----
        extern CVar<bool>    FFTEnabled;
        extern CVar<float>   FFTAmplitudeScale;
        extern CVar<Vector2> FFTWindDirection;
        extern CVar<float>   FFTWindSpeed;
        extern CVar<float>   FFTFetchKilometers;
        extern CVar<float>   FFTChoppiness;
        extern CVar<int>     FFTActiveComponentCount;
        extern CVar<float>   FFTGravity;

        // ---- うねり（swell）。風波とは独立な、遠方由来の長周期成分 ----
        extern CVar<bool>    SwellEnabled;
        extern CVar<float>   SwellHeight;
        extern CVar<float>   SwellPeriod;
        extern CVar<Vector2> SwellDirection;
        extern CVar<float>   SwellRelativeWidth;
        extern CVar<float>   SwellSpread;

        // ---- コースティクス（RT / SS 共通の見た目パラメータ）----
        extern CVar<int>     CausticsBackend; // 0=RT / 1=ScreenSpace（コンボは水面パネル。NoUI）
        extern CVar<float>   CausticsIntensity;
        extern CVar<float>   CausticsDepthAttenuation;
        extern CVar<float>   CausticsCurvatureScale;
        extern CVar<float>   CausticsSurfaceSampleRadius;
        extern CVar<float>   CausticsRefractiveIndex; // RT / SS 両方へ同値を反映（真実の源はここ）
        extern CVar<float>   CausticsReceiverNormalStrength;
        extern CVar<float>   CausticsAlignmentPower;

        // ---- DXR 屈折 ----
        extern CVar<float>   RefractionMaxOffsetPixels;

        /// @brief 実効吸収係数 σa = ベース + 濁度ゲイン（CDOM の青吸収）
        /// @details 合成後の実効値からベースへ逆算はできないため、永続化は必ずベース値で行う
        Vector3 EffectiveAbsorption();

        /// @brief 実効散乱係数 σs = ベース + 濁度ゲイン（懸濁粒子のほぼ無選択散乱）
        Vector3 EffectiveScattering();
    }
}
