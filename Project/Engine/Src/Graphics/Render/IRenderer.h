#pragma once
#include <d3d12.h>
#include "RenderPassType.h"
#include "Graphics/Pipeline/PipelineStateManager.h"

// 前方宣言
namespace CoreEngine {
    class Camera;
}

/// @brief 描画オブジェクト種別ごとのレンダラー基底インターフェース
///
/// @details
///  ## 役割
///  IRenderer は「描画オブジェクトの種類」ごとに PSO・RootSignature を
///  保持し、描画コマンドのバインドを担う **単一描画ユニット** を表現する。
///  （例: ModelRenderer / SpriteRenderer / UIRenderer / ParticleRenderer ...）
///
///  ## RenderPass との違い
///  - IRenderer  : 「何を描くか」を担当する低レベル描画器
///                 （PSO/RootSignature/DrawCall の所有・発行）
///  - RenderPass : 「いつ・どこに描くか」を担当する高レベルパイプラインノード
///                 （RenderTarget 切替・バリア・複数 IRenderer の呼び分け）
///
///  RenderPass は内部で複数の IRenderer を呼び出してフレーム全体を構築する。
///  IRenderer は RenderPass に依存しない（疎結合）。
///
///  ## ライフサイクル
///   1. Initialize(device)        - 起動時 1 回
///   2. SetCamera(camera)         - フレーム開始時
///   3. BeginPass(cmdList, blend) - パス開始時に PSO/RootSignature を設定
///   4. (描画コマンドの発行)
///   5. EndPass()                 - パス終了時
///
///  ## オプション機能
///  - GBuffer 描画をサポートする場合は IGBufferRenderer を併せて実装する。
namespace CoreEngine
{
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
