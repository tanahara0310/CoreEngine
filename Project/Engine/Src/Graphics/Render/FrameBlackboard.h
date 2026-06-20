#pragma once

#include <d3d12.h>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace CoreEngine
{
    /// @brief フレーム内で共有する論理リソースの 1 エントリ
    struct FrameBlackboardResource {
        std::string name;
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_STATES* currentState = nullptr;
        bool isValid = false;

        /// @brief Blackboard エントリをリセットする
        /// @details SRV、リソース、状態参照を無効化し、このフレームで未登録状態へ戻す
        void Reset();
    };

    /// @brief フレーム内共有リソースを論理名で管理するブラックボード
    ///          Pass 間の Read / Write を論理名ベースで整理する。
    class FrameBlackboard {
    public:
        static const char* const SceneColor;
        static const char* const SceneDepth;
        static const char* const ReflectionColor;
        static const char* const ReflectionDepth;
        static const char* const SSAO;
        static const char* const ShadowMap;
        static const char* const ShadowMask;
        static const char* const RTShadowMask;
        static const char* const BackBuffer;
        static const char* const PostEffectFinal;
        static const char* const GBufferAlbedoAO;
        static const char* const GBufferNormalRoughness;
        static const char* const GBufferEmissiveMetallic;
        static const char* const GBufferWorldPosition;
        static const char* const GBufferMotionVector;

        /// @brief PostEffect の中間論理リソース名を生成する
        /// @param index 中間段インデックス
        /// @return PostEffectIntermediateN 形式の論理名
        static std::string MakePostEffectIntermediateName(size_t index);

        /// @brief Blackboard に登録された全論理リソースをクリアする
        void Reset();

        /// @brief 論理名に対応するリソース情報を登録する
        /// @param name 論理リソース名
        /// @param srvHandle リソースを参照する SRV ハンドル
        /// @param resource 実リソース
        /// @param currentState 実リソースの現在状態を追跡する参照先
        void SetResource(
            const std::string& name,
            D3D12_GPU_DESCRIPTOR_HANDLE srvHandle,
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES* currentState = nullptr);

        /// @brief 指定した論理リソースを無効化する
        /// @param name 無効化する論理リソース名
        void InvalidateResource(const std::string& name);

        /// @brief 論理名からリソース情報を取得する
        /// @param name 取得対象の論理リソース名
        /// @return 登録済みリソース情報。未登録なら nullptr
        const FrameBlackboardResource* GetResource(const std::string& name) const;

        /// @brief 論理名から SRV ハンドルを取得する
        /// @param name 取得対象の論理リソース名
        /// @param outHandle 取得した SRV ハンドルの出力先
        /// @return 有効な SRV を取得できた場合 true
        bool TryGetSrvHandle(const std::string& name, D3D12_GPU_DESCRIPTOR_HANDLE& outHandle) const;

        /// @brief 論理名からリソース状態参照を取得する
        /// @param name 取得対象の論理リソース名
        /// @return 状態参照を保持している場合はそのポインタ。無い場合は nullptr
        D3D12_RESOURCE_STATES* GetCurrentStateRef(const std::string& name) const;

        /// @brief 論理名が有効なリソースを持つか確認する
        /// @param name 確認対象の論理リソース名
        /// @return 有効なリソースが登録済みなら true
        bool HasResource(const std::string& name) const;

        /// @brief Blackboard 登録情報を RenderGraph リソースへ反映する
        /// @param name 論理リソース名
        /// @param outResource 実リソースの出力先
        /// @param outCurrentState 現在状態参照の出力先
        /// @return 有効な登録がある場合 true
        bool TryResolveResource(
            const std::string& name,
            ID3D12Resource*& outResource,
            D3D12_RESOURCE_STATES*& outCurrentState) const;

    private:
        std::unordered_map<std::string, FrameBlackboardResource> resources_;
    };
}
