#pragma once

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <unordered_map>

#include "Graphics/IBL/IBLManager.h"

namespace CoreEngine
{
    class DirectXCommon;
    class IBLGenerator;
    class RenderManager;

    class IBLSystem
    {
    public:
        /// @brief IBLセットアップパラメータ（IBLManager::InitParamsをベースに、表示用環境マップ情報を追加）
        struct SetupParams : IBLManager::InitParams
        {
            D3D12_GPU_DESCRIPTOR_HANDLE environmentMapSRV = {};  ///< レンダリングに使用するSRVハンドル
            std::string environmentKey;                          ///< キャッシュ識別キー（空文字列の場合はポインタアドレスを使用）
            bool forceRegenerate = false;                        ///< キャッシュを無視して強制再生成
        };

        bool Initialize(DirectXCommon* dxCommon, IBLGenerator* iblGenerator, RenderManager* renderManager);
        bool Setup(const SetupParams& params);

    private:
        DirectXCommon* dxCommon_ = nullptr;
        IBLGenerator* iblGenerator_ = nullptr;
        RenderManager* renderManager_ = nullptr;

        std::unordered_map<std::string, std::unique_ptr<IBLManager>> iblCache_;
        std::string activeCacheKey_;
        IBLManager* iblManager_ = nullptr;
    };
}
