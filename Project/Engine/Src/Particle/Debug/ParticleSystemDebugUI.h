#pragma once

#ifdef USE_IMGUI

#include <cstdint>

namespace CoreEngine
{
    // 前方宣言
    class ParticleSystemComponent;
    class GpuParticleSystemComponent;
    class IParticleSystem;
    class ParticleModule;

    /// @brief パーティクルのコンポーネントのインスペクタ（CPU/GPU共通）
    /// @details Unity の Particle System インスペクターに近い構成:
    ///          ステータス → 再生コントロール → プリセット → モジュール一覧
    ///          モジュールの有効/無効はヘッダー右端のトグルで切り替える。
    ///          テクスチャ・ブレンド・ビルボードは記述子の欄が描く。
    class ParticleSystemDebugUI {
    public:
        ParticleSystemDebugUI() = default;
        ~ParticleSystemDebugUI() = default;

        /// @brief CPU パーティクルのインスペクタ
        /// @return モジュールの値を変えたら true
        static bool ShowImGui(ParticleSystemComponent& particleSystem);

        /// @brief GPU パーティクルのインスペクタ
        /// @return モジュールの値を変えたら true
        static bool ShowImGui(GpuParticleSystemComponent& particleSystem);

    private:
        /// @brief ステータスヘッダー（バックエンド・再生状態・粒子数バー）
        static void ShowStatusHeader(IParticleSystem& system, bool isGpu,
                                     uint32_t aliveCount, uint32_t capacity);

        /// @brief 再生コントロール
        /// @param cpuSystem CPU版のみ渡す（クリアボタン用。GPU版はnullptr）
        static void ShowTransport(IParticleSystem& system, ParticleSystemComponent* cpuSystem);

        /// @brief モジュール一覧（有効トグル付きヘッダー）
        static bool ShowModules(IParticleSystem& system);

        /// @brief 有効トグル付き折りたたみヘッダーでモジュールUIを表示する
        static bool DrawModuleSection(const char* label, ParticleModule& module,
                                      bool defaultOpen = false);

        /// @brief 統計情報（CPU版のみ）
        static void ShowStatistics(ParticleSystemComponent& particleSystem);
    };

} // namespace CoreEngine

#endif // USE_IMGUI
