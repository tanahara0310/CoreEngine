#pragma once

#ifdef USE_IMGUI

#include <cstdint>

namespace CoreEngine
{
    // 前方宣言
    class ParticleSystem;
    class GpuParticleSystem;
    class IParticleSystem;
    class ParticleModule;

    /// @brief パーティクルシステムの開発UI（CPU/GPU共通）
    /// @details Unity の Particle System インスペクターに近い構成:
    ///          ステータス → 再生コントロール → プリセット → モジュール一覧 → レンダラー
    ///          モジュールの有効/無効はヘッダー右端のトグルで切り替える。
    class ParticleSystemDebugUI {
    public:
        ParticleSystemDebugUI() = default;
        ~ParticleSystemDebugUI() = default;

        /// @brief CPUパーティクルシステムのUI表示
        /// @return UIに変更があった場合true
        static bool ShowImGui(ParticleSystem* particleSystem);

        /// @brief GPUパーティクルシステムのUI表示
        /// @return UIに変更があった場合true
        static bool ShowImGui(GpuParticleSystem* particleSystem);

    private:
        /// @brief ステータスヘッダー（バックエンド・再生状態・粒子数バー）
        static void ShowStatusHeader(IParticleSystem* system, bool isGpu,
                                     uint32_t aliveCount, uint32_t capacity);

        /// @brief 再生コントロールとエミッター位置
        /// @param cpuSystem CPU版のみ渡す（クリアボタン用。GPU版はnullptr）
        static bool ShowTransport(IParticleSystem* system, ParticleSystem* cpuSystem);

        /// @brief モジュール一覧（有効トグル付きヘッダー）
        static bool ShowModules(IParticleSystem* system);

        /// @brief 有効トグル付き折りたたみヘッダーでモジュールUIを表示する
        static bool DrawModuleSection(const char* label, ParticleModule& module,
                                      bool defaultOpen = false);

        /// @brief レンダラー設定（ブレンド・ビルボード・描画モード）
        /// @param cpuSystem CPU版のみ渡す（モデルパーティクル情報用。GPU版はnullptr）
        static bool ShowRendererSection(IParticleSystem* system, ParticleSystem* cpuSystem);

        /// @brief 統計情報（CPU版のみ）
        static void ShowStatistics(ParticleSystem* particleSystem);
    };

} // namespace CoreEngine

#endif // USE_IMGUI
