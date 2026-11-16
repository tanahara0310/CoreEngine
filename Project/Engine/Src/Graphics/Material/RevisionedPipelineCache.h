#pragma once

#include "Graphics/RHI/Command/DeferredReleaseQueue.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace CoreEngine
{
    /// @brief パイプラインを組み直す条件
    enum class PipelineRebuild : uint8_t
    {
        OnRevision, ///< シェーダの版が変わるたびに組み直す
        Once,       ///< 一度組もうとしたら、版が変わっても組み直さない
    };

    /// @brief 名前ごとにパイプラインを 1 つずつ持ち、シェーダの版が変わったら組み直す
    /// @tparam T パイプラインの型
    /// @details 組み直しに失敗したら、次に組み直せるまで nullptr を返す。
    ///          組み直した古いパイプラインは、記録中のフレームの GPU 作業が終わってから捨てる
    ///          （返したポインタは、組み直したフレームの終わりまで使える）。
    template <class T>
    class RevisionedPipelineCache
    {
    public:
        /// @brief パイプラインを引く（版が違えば組み直す）
        /// @param releaseQueue 古いパイプラインを預ける遅延解放のキュー
        /// @param key パイプラインを分ける名前
        /// @param revision 今のシェーダの版（0 は使わない）
        /// @param build 新しいパイプラインを組む `std::unique_ptr<T>(const T* previous)`。
        ///              previous は今まで使っていたパイプライン（無ければ nullptr）。組めなければ nullptr を返す
        /// @param rebuild 組み直す条件
        /// @return 組めなければ nullptr
        template <class Build>
        T* Get(DeferredReleaseQueue& releaseQueue, const std::string& key, uint64_t revision, Build&& build,
            PipelineRebuild rebuild = PipelineRebuild::OnRevision)
        {
            Entry& entry = entries_[key];
            if (entry.revision == revision || (rebuild == PipelineRebuild::Once && entry.revision != 0)) {
                return entry.pipeline.get();
            }
            entry.revision = revision;

            std::unique_ptr<T> built = build(static_cast<const T*>(entry.pipeline.get()));
            releaseQueue.PushForCurrentFrame(std::move(entry.pipeline));
            if (built) {
                entry.pipeline = std::move(built);
            }
            return entry.pipeline.get();
        }

    private:
        struct Entry
        {
            std::unique_ptr<T> pipeline;
            uint64_t revision = 0; ///< 最後に組もうとしたときの版（0 はまだ組もうとしていない）
        };

        std::unordered_map<std::string, Entry> entries_;
    };
}
