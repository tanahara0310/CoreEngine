#pragma once

#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include "Graphics/RayTracing/Effect/RTEffect.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"

namespace CoreEngine
{
    class RenderPipeline;

    /// @brief 効果が描画の並びへ入れるパス 1 本
    struct RTEffectPassDesc {
        RenderPassPhase phase = RenderPassPhase::PreLighting;
        int priority = 0;                                  ///< フェーズの中の順番（小さいほど先）
        std::optional<GpuTimingCategory> timingCategory;   ///< GPU 時間の分類（無ければフェーズから決まる）
        std::unique_ptr<RenderPass> (*create)() = nullptr;

        /// @brief パスの型から作る
        template <class TPass>
        static RTEffectPassDesc Of(
            RenderPassPhase phase,
            int priority,
            std::optional<GpuTimingCategory> timingCategory = std::nullopt)
        {
            return { phase, priority, timingCategory,
                []() -> std::unique_ptr<RenderPass> { return std::make_unique<TPass>(); } };
        }
    };

    /// @brief RT_EFFECT_REGISTER で申告する効果 1 つ
    struct RTEffectDesc {
        const char* id = nullptr;              ///< 効果の名前（ログと、作る順・パスを入れる順に使う）
        const char* group = nullptr;           ///< 作る時機（nullptr = 起動時。名前があれば RTEffectSet::EnsureGroup で作る）
        std::vector<RTEffectPassDesc> passes;  ///< 描画の並びへ入れるパス

        // 以下は RT_EFFECT_REGISTER が埋める
        const void* typeKey = nullptr;
        std::unique_ptr<RTEffect> (*create)() = nullptr;
    };

    /// @brief 申告された効果の一覧
    class RTEffectRegistry {
    public:
        static RTEffectRegistry& Get();

        /// @brief 効果を足す（RT_EFFECT_REGISTER が静的初期化のときに呼ぶ）
        bool Register(RTEffectDesc desc);

        /// @brief 申告された効果（名前の順）
        const std::vector<RTEffectDesc>& GetEffects();

        /// @brief 全部の効果のパスを描画の並びへ入れる
        void AddPasses(RenderPipeline& pipeline);

    private:
        std::vector<RTEffectDesc> effects_;
        bool sorted_ = false;
    };

    /// @brief 申告に型の作り方と印を足す（RT_EFFECT_REGISTER が使う）
    template <class T>
    RTEffectDesc MakeRTEffectDesc(RTEffectDesc desc)
    {
        static_assert(std::is_base_of_v<RTEffect, T>, "RT_EFFECT_REGISTER の型は RTEffect を継承する");
        desc.typeKey = RTEffectTypeKey<T>();
        desc.create = []() -> std::unique_ptr<RTEffect> { return std::make_unique<T>(); };
        return desc;
    }
}

#define RT_EFFECT_DETAIL_CONCAT_INNER(a, b) a##b
#define RT_EFFECT_DETAIL_CONCAT(a, b) RT_EFFECT_DETAIL_CONCAT_INNER(a, b)

/// @brief レイトレーシングの効果を申告する（効果の .cpp のファイルスコープに書く）
/// @param Type RTEffect を継承した効果の型（RTEffectSet が 1 つ作って持つ）
/// @details 残りは RTEffectDesc の指示付き初期化子（.id・.group・.passes の順）
#define RT_EFFECT_REGISTER(Type, ...)                                                             \
    namespace {                                                                                   \
        const bool RT_EFFECT_DETAIL_CONCAT(rtEffect_, __LINE__) =                                 \
            ::CoreEngine::RTEffectRegistry::Get().Register(                                       \
                ::CoreEngine::MakeRTEffectDesc<Type>(::CoreEngine::RTEffectDesc{ __VA_ARGS__ }));  \
    }
