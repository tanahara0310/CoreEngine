#pragma once
#include <d3d12.h>
#include "RenderPassType.h"
#include "Graphics/Pipeline/PipelineStateManager.h"

// 前方宣言
namespace CoreEngine {
    class ICamera;
}

/// @brief レンダラーの基底インターフェース

namespace CoreEngine
{
class IRenderer {
public:
    virtual ~IRenderer() = default;
    
    /// @brief 初期化
    /// @param device D3D12デバイス
    virtual void Initialize(ID3D12Device* device) = 0;
    
    /// @brief 描画パスの開始（パイプライン設定）
    /// @param cmdList コマンドリスト
    /// @param blendMode ブレンドモード
    virtual void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode = BlendMode::kBlendModeNone) = 0;

    /// @brief GBuffer パスの開始（GBuffer 用 RootSignature・PSO をバインド）
    /// @note GBuffer に対応しないレンダラーはデフォルト実装（何もしない）のままでよい
    virtual void BeginGBufferPass(ID3D12GraphicsCommandList* cmdList) { (void)cmdList; }

    /// @brief 描画パスの終了
    virtual void EndPass() = 0;
    
    /// @brief このレンダラーがサポートする描画タイプを取得
    /// @return 描画パスタイプ
    virtual RenderPassType GetRenderPassType() const = 0;
    
    /// @brief カメラを設定
    /// @param camera カメラオブジェクト
    virtual void SetCamera(const CoreEngine::ICamera* camera) = 0;
};
}
