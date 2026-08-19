#pragma once
#include <d3d12.h>
#include "RenderPassType.h"
#include "Graphics/Pipeline/PipelineStateManager.h"

// 前方宣言
namespace CoreEngine {
    class Camera;
}

namespace CoreEngine
{
/// @brief 描画オブジェクト種別ごとのレンダラー基底インターフェース
/// @details PSO / RootSignature を保持し、描画コマンドのバインドまでを担う単一描画ユニット。
///          「いつ・どこに描くか」は RenderPass の担当で、IRenderer は RenderPass に依存しない。
/// @note 使用順は Initialize → SetCamera → BeginPass → 描画 → EndPass。
///       G-Buffer 描画に対応する場合は IGBufferRenderer も併せて実装する。
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// @brief 初期化（PSO・RootSignature 構築）
    /// @param device D3D12デバイス
    virtual void Initialize(ID3D12Device* device) = 0;

    /// @brief 描画パスの開始（PSO/RootSignature を CommandList にバインド）
    /// @param cmdList コマンドリスト
    /// @param blendMode ブレンドモード
    virtual void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode = BlendMode::kBlendModeNone) = 0;

    /// @brief 描画パスの終了（状態クリア）
    virtual void EndPass() = 0;

    /// @brief このレンダラーが担当する描画パスタイプを取得
    /// @return 描画パスタイプ（RenderManager のソート/ディスパッチに使用）
    virtual RenderPassType GetRenderPassType() const = 0;

    /// @brief カメラを設定（View/Projection の参照元）
    /// @param camera カメラオブジェクト
    virtual void SetCamera(const CoreEngine::Camera* camera) = 0;
};
}
