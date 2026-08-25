#pragma once

#include <d3d12.h>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include <externals/DirectXTex/DirectXTex.h>
#include <wrl.h>
#include <string>
#include <mutex>

namespace CoreEngine
{
    class GraphicsCore;

    /// @brief GPUリソースへのアップロードとSRV作成を担当するクラス
    class TextureGpuUploader
    {
    public:
        /// @brief GPUアップロード後に呼び出し側へ返す結果データ
        /// @note 中間バッファは UploadContext がフェンス完了まで生存させるため返さない。
        struct UploadResult
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> texture;
            DescriptorHandle descriptor{}; ///< ???? SRV ????(???????????????)
        };

        /// @brief ミップ済み画像をGPUへ転送し、SRVを作成する
        /// @param dxCommon DirectX共通管理クラス
        /// @param mipImages ミップチェーン作成済みの画像
        /// @param resolvedPath ログ・SRV識別用のパス
        /// @return 作成されたGPUリソースとSRVハンドル
        static UploadResult UploadAndCreateSrv(
            CoreEngine::GraphicsCore* dxCommon,
            const DirectX::ScratchImage& mipImages,
            const std::string& resolvedPath);

    private:
        // ディスクリプタヒープ確保とリソース生成への同時アクセスを防ぐ排他ロック。
        // コマンドリストへの記録は UploadContext が自前のロックで直列化するため、
        // ここで守っているのはそれ以外（DescriptorAllocator::CreateSRV 等）。
        static std::mutex gpuUploadMutex_;
    };
}
