#pragma once

#ifdef _DEBUG

#include <cstdint>

namespace CoreEngine
{
    // 前方宣言
    class ParticleSystem;

    /// @brief ParticleSystemのデバッグUI表示クラス
    /// ImGuiを使用したデバッグ情報の表示を担当
    class ParticleSystemDebugUI {
    public:
        ParticleSystemDebugUI() = default;
        ~ParticleSystemDebugUI() = default;

        /// @brief ImGuiデバッグUI表示
        /// @param particleSystem 対象のパーティクルシステム
        /// @return UIに変更があった場合true
        static bool ShowImGui(ParticleSystem* particleSystem);

    private:
        /// @brief システム状態表示
        static void ShowSystemStatus(ParticleSystem* particleSystem);

        /// @brief 制御ボタン表示
        static void ShowControlButtons(ParticleSystem* particleSystem);

        /// @brief プリセット管理UI表示
        static void ShowPresetManager(ParticleSystem* particleSystem);

        /// @brief 各モジュールのUI表示
        static void ShowModules(ParticleSystem* particleSystem);

        /// @brief エミッター設定UI表示
        static void ShowEmitterSettings(ParticleSystem* particleSystem);

        /// @brief 統計情報表示
        static void ShowStatistics(ParticleSystem* particleSystem);
    };

} // namespace CoreEngine

#endif // _DEBUG
