#pragma once
#include "Graphics/PostEffect/Effect/PostEffectBase.h"

#include <d3d12.h>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine
{
    struct RenderContext;
    class PostEffectTransientPool;

    /// @brief グラフ内リソースへの参照
    /// @details 実体は Blackboard の論理名。ハンドルではなく名前で持つことで、
    ///          RenderGraph が既に持っている名前ベースの依存解決に素直に乗る。
    class PostEffectResourceRef {
    public:
        PostEffectResourceRef() = default;
        explicit PostEffectResourceRef(std::string name) : name_(std::move(name)) {}

        const std::string& Name() const { return name_; }
        bool IsValid() const { return !name_.empty(); }

    private:
        std::string name_;
    };

    /// @brief パス記録ラムダへ渡る実行時文脈
    struct PostEffectPassContext {
        ID3D12GraphicsCommandList*               cmdList = nullptr;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> reads;   ///< 宣言順に解決済み
        D3D12_GPU_DESCRIPTOR_HANDLE              output{}; ///< CS なら UAV
        uint32_t                                 width  = 0; ///< 出力先の実サイズ
        uint32_t                                 height = 0;
    };

    /// @brief 1 パス分の記録処理
    using PostEffectRecordFn = std::function<void(const PostEffectPassContext&)>;

    /// @brief エフェクトが積んだ 1 パス分の宣言
    struct PostEffectStep {
        std::string              name;
        std::vector<std::string> reads;
        std::string              write;
        PostEffectExecutionType  executionType = PostEffectExecutionType::Compute;
        PostEffectRecordFn       record;
    };

    /// @brief エフェクトが自分のパス群と一時リソースを RenderGraph へ積むためのファサード
    /// @details 「入力1・出力1・単一ディスパッチ」の制約から外れ、多段処理を表現できる。
    ///          バリアと実行順は宣言から RenderGraph が導出する。
    /// @warning AddComputePass / AddGraphicsPass は宣言のみ。GPU コマンドは record ラムダの中で積むこと。
    class PostEffectGraphBuilder {
    public:
        /// @param inputResource チェーン上でこのエフェクトへ入ってくる色
        /// @param chainOutput   このエフェクトが最終的に書くべき出力
        /// @param baseWidth     フル解像度の幅（一時ターゲットのスケール基準）
        PostEffectGraphBuilder(
            const RenderContext& context,
            PostEffectTransientPool& pool,
            std::string inputResource,
            std::string chainOutput,
            uint32_t baseWidth,
            uint32_t baseHeight);

        /// @brief チェーン上の入力色
        PostEffectResourceRef Input() const { return PostEffectResourceRef(inputResource_); }

        /// @brief このエフェクトが最終的に書くべき出力（最後のパスの write に使う）
        PostEffectResourceRef ChainOutput() const { return PostEffectResourceRef(chainOutput_); }

        /// @brief 既存の論理リソースを読む（DeclareExtraInputs で申告済みのもの）
        PostEffectResourceRef Read(const char* logicalName) const
        {
            return PostEffectResourceRef(logicalName ? std::string(logicalName) : std::string());
        }

        /// @brief 一時リソースを確保する（失敗時は無効な参照）
        /// @param resolutionScale 1.0 でフル解像度、0.5 で 1/2
        /// @param format DXGI_FORMAT_UNKNOWN のときはフル解像度と同じ既定フォーマット
        /// @note 同一フレーム内でのみ有効。持ち越すものは Blackboard の名前付きリソースにすること
        PostEffectResourceRef CreateTransient(float resolutionScale = 1.0f,
                                              DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

        /// @brief コンピュートパスを積む
        void AddComputePass(std::string_view name,
                            const std::vector<PostEffectResourceRef>& reads,
                            const PostEffectResourceRef& write,
                            PostEffectRecordFn record);

        /// @brief グラフィクスパスを積む
        void AddGraphicsPass(std::string_view name,
                             const std::vector<PostEffectResourceRef>& reads,
                             const PostEffectResourceRef& write,
                             PostEffectRecordFn record);

        /// @brief 一時リソースのスケール基準になるフル解像度
        uint32_t BaseWidth() const { return baseWidth_; }
        uint32_t BaseHeight() const { return baseHeight_; }

        /// @brief 積まれたパス列
        const std::vector<PostEffectStep>& Steps() const { return steps_; }

    private:
        void AddPass(std::string_view name,
                     const std::vector<PostEffectResourceRef>& reads,
                     const PostEffectResourceRef& write,
                     PostEffectExecutionType executionType,
                     PostEffectRecordFn record);

        const RenderContext&     context_;
        PostEffectTransientPool& pool_;
        std::string              inputResource_;
        std::string              chainOutput_;
        uint32_t                 baseWidth_  = 0;
        uint32_t                 baseHeight_ = 0;
        std::vector<PostEffectStep> steps_;
    };
}
