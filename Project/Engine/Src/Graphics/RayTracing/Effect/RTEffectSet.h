#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Graphics/RayTracing/Effect/RTEffect.h"

namespace CoreEngine
{
    /// @brief 申告されたレイトレーシングの効果を作って持つ（RenderDomainContext が 1 つ持つ）
    /// @details 作る時機は効果の組ごと（起動時の組と、名前を付けた組）。
    ///          作った効果は Shutdown まで持ち、作った逆の順に壊す。
    class RTEffectSet {
    public:
        RTEffectSet() = default;
        ~RTEffectSet();

        RTEffectSet(const RTEffectSet&) = delete;
        RTEffectSet& operator=(const RTEffectSet&) = delete;

        /// @brief 効果の初期化に渡す窓口を覚える
        /// @param rayTracingSupported false なら効果は作るが Initialize を呼ばない
        void Initialize(const RTEffectInitContext& init, bool rayTracingSupported);

        /// @brief 組の効果を、まだなら作る
        /// @param group 組の名前（nullptr = 起動時の組）
        void EnsureGroup(const char* group);

        /// @brief 作った効果の全部のパイプラインの完成を待つ
        void WaitForPipelines();

        /// @brief フレームの頭に作った効果へ知らせる
        void BeginFrame();

        /// @brief ウィンドウの大きさが変わったことを作った効果へ知らせる
        void OnResize(uint32_t width, uint32_t height);

        /// @brief 作った効果を逆の順に壊す（GPU が終わってから呼ぶ）
        void Shutdown();

        /// @brief 効果を型で引く（まだ作っていなければ nullptr）
        template <class T>
        T* Find() const
        {
            RTEffect* effect = FindByKey(RTEffectTypeKey<T>());
            return effect ? static_cast<T*>(effect) : nullptr;
        }

    private:
        RTEffect* FindByKey(const void* typeKey) const;

        struct Entry {
            const void* typeKey = nullptr;
            const char* id = nullptr;
            std::unique_ptr<RTEffect> effect;
        };

        std::vector<Entry> effects_;
        std::vector<std::string> createdGroups_;
        RTEffectInitContext init_{};
        bool rayTracingSupported_ = false;
    };

    /// @brief RenderContext から効果を型で引く（無ければ nullptr）
    template <class T, class TContext>
    T* FindRTEffect(const TContext& context)
    {
        return context.rtEffects ? context.rtEffects->template Find<T>() : nullptr;
    }
}
