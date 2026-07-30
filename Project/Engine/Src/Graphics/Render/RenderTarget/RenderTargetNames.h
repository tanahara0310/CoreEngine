#pragma once

/// @brief レンダーターゲット名を一元管理する名前空間
/// @details レンダーターゲットの登録と取得で同じ文字列を使用するため、タイポを防ぐ

namespace CoreEngine
{
    namespace RenderTargetNames {
        constexpr const char* SceneColor = "SceneColor";
        constexpr const char* SceneColorSnapshot = "SceneColorSnapshot";
        constexpr const char* PostEffectIntermediatePrefix = "PostEffectIntermediate";
        constexpr const char* PostEffectFinal = "PostEffectFinal";
        constexpr const char* BackBuffer = "BackBuffer";
        constexpr const char* SSAOBuffer = "SSAOBuffer";
        constexpr const char* SSAOBlurBuffer = "SSAOBlurBuffer";
        // SSAO テンポラル蓄積の履歴 ping-pong（どちらが書き込み側かは frameNumber の偶奇で決まる）
        constexpr const char* SSAOHistoryA = "SSAOHistoryA";
        constexpr const char* SSAOHistoryB = "SSAOHistoryB";
        constexpr const char* WaterCausticsBuffer = "WaterCausticsBuffer";
        // TAA 履歴の ping-pong 2 枚。フレームごとに読み書きを入れ替えるため、
        // どちらが「今フレームの出力」かは論理名（FrameBlackboard::TAAOutput）側で解決する。
        constexpr const char* TAAHistoryA = "TAAHistoryA";
        constexpr const char* TAAHistoryB = "TAAHistoryB";
        // CAS（シャープ化）の出力。履歴を持たないので 1 枚で足りる
        constexpr const char* CASOutput = "CASOutput";
    }
}
