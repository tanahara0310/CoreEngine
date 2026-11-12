#pragma once

#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    /// @brief 水の描画の品質と負荷の CVar 群（プロジェクトの CVars.json に保存する）
    /// @details 定義は WaterCVars.cpp。水そのものの性質は水面コンポーネントの WaterProfile、風はシーンの `r.Wind.*` が持つ。
    namespace WaterCVars
    {
        // ---- 描画 ----
        extern CVar<bool>    WriteMotionVector;

        // ---- 岸 ----
        extern CVar<bool>    ShoreFineMesh;

        // ---- 海底の高さ ----
        extern CVar<int>     SeabedRefreshFrames;

        // ---- 海の範囲マップ ----
        extern CVar<bool>    SeaMapEnabled;
        extern CVar<int>     SeaMapResolution;

        // ---- 濡れ跡 ----
        extern CVar<bool>    WetnessEnabled;
        extern CVar<float>   WetnessDrySeconds;

        // ---- コースティクス ----
        extern CVar<int>     CausticsBackend; // 0=RT / 1=ScreenSpace（コンボは水面パネル。NoUI）
        extern CVar<float>   CausticsIntensity;
        extern CVar<float>   CausticsDepthAttenuation;
        extern CVar<float>   CausticsCurvatureScale;
        extern CVar<float>   CausticsSurfaceSampleRadius;
        extern CVar<float>   CausticsReceiverNormalStrength;
        extern CVar<float>   CausticsAlignmentPower;

        // ---- DXR 屈折 ----
        extern CVar<float>   RefractionMaxOffsetPixels;
    }
}
