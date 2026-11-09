#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/RayTracing/Effect/RTEffect.h"
#include "Graphics/RayTracing/RayTracingOutputViewSet.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Math/Matrix/Matrix4x4.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class ShaderProgramCache;
    struct RenderContext;

    /// @brief RTDenoiser の調整値
    struct RTDenoiseSettings {
        int   maxHistoryFrames = 64;         ///< 蓄積フレーム数の上限（混ぜる割合 α = 1/N の下限を決める）
        float historyClampSigma = 3.0f;      ///< 履歴のクランプ帯の広さ（推定標準偏差の倍数）
        float historyClampMargin = 0.02f;    ///< 履歴のクランプ帯に足す固定の幅
        float historyDepthTolerance = 0.05f; ///< 再投影した履歴を採用する相対深度しきい値
        float stillnessSigma = 1.25f;        ///< 現フレームと履歴の差がこの倍数 × σ 未満なら履歴をそのまま出す（0 で無効）
        int   atrousPassCount = 3;           ///< A-Trous のパス数（0〜RTDenoiser::kMaxAtrousPassCount。0 で空間のフィルタを掛けない）
        float atrousPhiValue = 6.0f;         ///< A-Trous のエッジ停止関数の σ 倍率（大きいほど広くぼける）
        float atrousPhiNormal = 16.0f;       ///< A-Trous の法線重みの指数
        float atrousPhiDepth = 1.0f;         ///< A-Trous の深度重みの厳しさ
        float upsamplePhiDepth = 8.0f;       ///< フル解像度へ引き伸ばすときの深度重みの厳しさ
        bool  useHistory = true;             ///< false で履歴を読まない
    };

    /// @brief デノイズの手がかりにする画面の情報（どれもフル解像度）
    struct RTDenoiseGuides {
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV{};
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV{};
        D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV{}; ///< Accumulate だけが読む
        Matrix4x4 projection{};                        ///< 深度の線形化に使う投影行列
    };

    /// @brief 実行中のビューの深度・法線・モーションベクター・投影行列を集める
    /// @return 実行中のビューが無いか G-Buffer が無ければ false
    bool BuildRTDenoiseGuides(const RenderContext& context, RTDenoiseGuides& out);

    /// @brief レイで求めた 0〜1 の割合（光が届いた割合・遮られなかった割合など）を時間と空間の両方でデノイズする部品
    /// @details 信号 1 つを「チャンネル」と呼び、チャンネルごとに入力・履歴・作業用・出力のテクスチャを持つ。
    ///          毎フレーム Prepare → BeginInputWrite → 入力へ書く → EndInputWrite → Accumulate → Filter の順に呼ぶ。
    ///          入力は x = 値・y = サンプル数（0 は信号の無い画素）。背景と信号の無い画素の出力は 1。
    ///          Filter の後の出力（フル解像度・R16_FLOAT）はピクセルシェーダの読み取りの状態にある。
    class RTDenoiser {
    public:
        /// @brief チャンネル 1 つが持つテクスチャの数
        static constexpr uint32_t kTexturesPerChannel = 6;
        /// @brief チャンネルの数の上限
        static constexpr uint32_t kMaxChannels = RayTracingOutputViewSet::kMaxSlotCount / kTexturesPerChannel;
        /// @brief A-Trous のパス数の上限（ステップ幅は 1・2・4・8）
        static constexpr int kMaxAtrousPassCount = 4;

        /// @brief 入力の形式（R = 値、G = サンプル数）
        static constexpr DXGI_FORMAT kInputFormat = DXGI_FORMAT_R16G16_FLOAT;
        /// @brief 履歴と A-Trous の作業用の形式（R = 値、G = 推定分散、B = 蓄積フレーム数、A = 線形ビュー深度）
        static constexpr DXGI_FORMAT kSignalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        /// @brief 出力の形式
        static constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R16_FLOAT;

        /// @brief 時間の蓄積・A-Trous・仕上げのパイプラインを作る
        /// @param ownerName    ログとテクスチャの名前に付ける持ち主の名前
        /// @param channelCount チャンネルの数（kMaxChannels 以下）
        /// @return 3 つのパイプラインが全部作れたら true
        bool Initialize(const RTEffectInitContext& init, const char* ownerName, uint32_t channelCount);

        /// @brief チャンネルの今フレームの準備（テクスチャの確保・履歴の書き込み先の入れ替え）
        /// @param width,height フル解像度
        /// @param traceScale   入力を書く解像度の縮小率（1 = フル解像度 / 2 = ハーフ解像度）
        /// @param signalKey    信号の出どころを見分ける値（前の準備と違うか、前のフレームに準備していなければ履歴を捨てる）
        /// @return テクスチャがそろったら true
        bool Prepare(uint32_t channel, UINT width, UINT height, UINT traceScale, uint64_t signalKey);

        /// @brief 入力へ書く前の状態遷移（入力を UAV、前フレームの履歴を読み取りへ）
        void BeginInputWrite(ID3D12GraphicsCommandList* cmdList, uint32_t channel);

        /// @brief 入力を書き終えた後の状態遷移（書き込みの完了を待ってから読み取りへ）
        void EndInputWrite(ID3D12GraphicsCommandList* cmdList, uint32_t channel);

        /// @brief 時間方向の蓄積（入力と前フレームの履歴から今フレームの履歴を作る）
        void Accumulate(ID3D12GraphicsCommandList* cmdList, uint32_t channel,
            const RTDenoiseGuides& guides, const RTDenoiseSettings& settings);

        /// @brief A-Trous と、フル解像度の出力への仕上げ
        void Filter(ID3D12GraphicsCommandList* cmdList, uint32_t channel,
            const RTDenoiseGuides& guides, const RTDenoiseSettings& settings);

        /// @brief テクスチャを持つ全チャンネルを新しい大きさで作り直す（GPU は止まっている）
        void OnResize(UINT width, UINT height, UINT traceScale);

        /// @brief 実際に掛ける A-Trous のパス数（0〜kMaxAtrousPassCount）
        static int ClampAtrousPassCount(int passCount);

        UINT GetTraceWidth(uint32_t channel) const { return channels_[channel].traceWidth; }
        UINT GetTraceHeight(uint32_t channel) const { return channels_[channel].traceHeight; }

        /// @brief 前フレームの履歴を読めるか（Prepare の後に見る）
        bool IsHistoryValid(uint32_t channel) const { return channels_[channel].historyValid; }

        D3D12_GPU_DESCRIPTOR_HANDLE GetInputUAV(uint32_t channel) const { return TextureUAV(channel, Texture::Input); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetInputSRV(uint32_t channel) const { return TextureSRV(channel, Texture::Input); }
        GpuResource& GetInput(uint32_t channel) { return TextureResource(channel, Texture::Input); }

        /// @brief 前フレームの履歴（入力を書くシェーダが読む）の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetPreviousHistorySRV(uint32_t channel) const;

        /// @brief 今フレームの履歴の SRV と実体
        D3D12_GPU_DESCRIPTOR_HANDLE GetHistorySRV(uint32_t channel) const;
        GpuResource& GetHistory(uint32_t channel);

        /// @brief フル解像度の出力の SRV と実体（未確保なら SRV は 0・実体は空）
        D3D12_GPU_DESCRIPTOR_HANDLE GetOutputSRV(uint32_t channel) const { return TextureSRV(channel, Texture::Output); }
        GpuResource& GetOutput(uint32_t channel) { return TextureResource(channel, Texture::Output); }

    private:
        /// @brief チャンネル 1 つのテクスチャの用途（Output 以外はトレース解像度）
        enum class Texture : uint32_t {
            Output = 0,
            Input,
            FilterA,
            FilterB,
            HistoryA,
            HistoryB,
            Count
        };
        static_assert(static_cast<uint32_t>(Texture::Count) == kTexturesPerChannel);

        /// @brief チャンネル 1 つの、テクスチャ以外の状態
        struct Channel {
            UINT width = 0;            ///< フル解像度（出力の大きさ）
            UINT height = 0;
            UINT traceWidth = 0;       ///< トレース解像度（出力以外の大きさ）
            UINT traceHeight = 0;
            UINT traceScale = 1;
            bool historyValid = false; ///< 前フレームの履歴を読めるか
            uint32_t historyParity = 0; ///< 今フレームの履歴の書き込み先（0 = HistoryA / 1 = HistoryB）
            uint64_t signalKey = 0;
            uint64_t lastPrepareFrame = 0;

            Texture CurrentHistory() const { return (historyParity == 0) ? Texture::HistoryA : Texture::HistoryB; }
            Texture PreviousHistory() const { return (historyParity == 0) ? Texture::HistoryB : Texture::HistoryA; }
        };

        /// @brief コンピュートのパス 1 本（ルートシグネチャとバインド表はシェーダのリフレクションから作る）
        struct ComputePass {
            RootSignatureManager rootSigMgr;
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
            BindingTable bindings;
            bool initialized = false;

            ID3D12RootSignature* GetRootSignature() { return rootSigMgr.GetRootSignature(); }
        };

        static constexpr uint32_t SlotIndex(uint32_t channel, Texture texture)
        {
            return channel * kTexturesPerChannel + static_cast<uint32_t>(texture);
        }
        D3D12_GPU_DESCRIPTOR_HANDLE TextureSRV(uint32_t channel, Texture texture) const {
            return textures_.GetSRVHandle(SlotIndex(channel, texture));
        }
        D3D12_GPU_DESCRIPTOR_HANDLE TextureUAV(uint32_t channel, Texture texture) const {
            return textures_.GetUAVHandle(SlotIndex(channel, texture));
        }
        GpuResource& TextureResource(uint32_t channel, Texture texture) {
            return textures_.Resource(SlotIndex(channel, texture));
        }

        /// @brief チャンネルのテクスチャを指定の大きさでそろえる（大きさが変われば履歴を捨てる）
        bool EnsureTextures(uint32_t channel, UINT width, UINT height, UINT traceScale);

        /// @brief CS のコンパイルとリフレクションからパス 1 本を作る
        /// @param rootConstantName ルート定数にする cbuffer 名
        bool CreateComputePass(const wchar_t* shaderPath, const ShaderBindingDecl* decls, size_t declCount,
            const char* rootConstantName, const char* passName, ComputePass& outPass);

        /// @brief トレース解像度の値をフル解像度の出力へ書く
        void Resolve(ID3D12GraphicsCommandList* cmdList, uint32_t channel, Texture source,
            const RTDenoiseGuides& guides, const RTDenoiseSettings& settings);

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        ShaderProgramCache* shaderProgramCache_ = nullptr;
        const char* ownerName_ = "RTDenoiser";
        uint32_t channelCount_ = 0;

        ComputePass temporalPass_;
        ComputePass atrousPass_;
        ComputePass resolvePass_;

        RayTracingOutputViewSet textures_;
        Channel channels_[kMaxChannels]{};
    };
}
