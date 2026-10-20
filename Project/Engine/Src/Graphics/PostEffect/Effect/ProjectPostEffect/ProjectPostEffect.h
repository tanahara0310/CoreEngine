#pragma once

#include "../PostEffectComputeBase.h"
#include "EngineSystem/Settings/ProjectSettings.h"
#include "Graphics/Material/ShaderMaterialPipeline.h"
#include "Graphics/RootSignature/RootSlot.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/MathCore.h"

#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

namespace CoreEngine
{
    class ShaderMaterial;
    class ShaderReflectionData;

    /// @brief プロジェクトのポストエフェクト 1 つ（Project.json の `postEffects` の 1 要素）
    /// @details シェーダと値はマテリアル（.mat）が持つ。シェーダはコンピュートシェーダで、
    ///          `PostEffectMaterial.hlsli` の決まりで書く（入口は main・8×8）。
    ///          値はシェーダの b0 の定数バッファをリフレクションで読み、名前で渡す。
    ///          有効かどうかとマテリアルの値は、シーンを開くたびに Project.json と .mat の中身へ戻す。
    ///          シェーダのファイルを書き換えたら組み直し、組めなければ前のパイプラインを使い続ける。
    class ProjectPostEffect : public PostEffectComputeBase {
    public:
        /// @brief 描くたびにシェーダへ渡す値（`PostEffectMaterial.hlsli` の `gPostEffect`）
        struct FrameData {
            uint32_t screenWidth = 1;
            uint32_t screenHeight = 1;
            Vector2 invScreenSize = { 1.0f, 1.0f };
            float time = 0.0f;
            float unscaledTime = 0.0f;
            float deltaTime = 0.0f;
            float padding = 0.0f;
        };
        static constexpr Cb::Field kFrameDataFields[] = {
            CB_FIELD(FrameData, screenWidth), CB_FIELD(FrameData, screenHeight),
            CB_FIELD(FrameData, invScreenSize), CB_FIELD(FrameData, time),
            CB_FIELD(FrameData, unscaledTime), CB_FIELD(FrameData, deltaTime),
            CB_FIELD(FrameData, padding),
        };
        CB_VERIFY_LAYOUT(FrameData, kFrameDataFields);
        CB_BIND_HLSL(FrameData, kFrameDataFields, "gPostEffect");

        /// @brief 段の名前（Project.json の `stage`）
        static constexpr const char* kStageSceneHDR = "SceneHDR";
        static constexpr const char* kStagePostTonemap = "PostTonemap";
        static constexpr const char* kStageOverlay = "Overlay";

        explicit ProjectPostEffect(ProjectPostEffectSettings settings);

        /// @brief マテリアルを読み、シェーダを組む
        /// @note 組めなければ例外を投げる（PostEffectManager は登録しない）
        void Initialize(GraphicsCore* dxCommon) override;

        void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
            uint32_t width,
            uint32_t height) override;

        /// @brief シェーダが書き換わっていたら組み直す
        void PrepareFrame(const PostEffectFrameContext& ctx) override;

        /// @brief Overlay も PostTonemap の段に入る（UI の後へ置くのは並びの位置で決める）
        PostEffectStage GetStage() const override;

        void DrawImGui() override;

        /// @brief 有効かどうかを Project.json の値へ、マテリアルを .mat の中身へ戻す
        /// @note シーンの遷移が使っている間は戻さない（覆っている絵が消えるため）
        void ResetToDefault();

        /// @brief シーンの遷移が使っているかを決める（SceneTransition が呼ぶ）
        void SetDrivenByTransition(bool driven) { drivenByTransition_ = driven; }

        /// @brief Project.json に書いた設定
        const ProjectPostEffectSettings& GetSettings() const { return settings_; }

        /// @brief 値を持つマテリアル（共有）
        const std::shared_ptr<ShaderMaterial>& GetMaterial() const { return material_; }

        /// @brief シェーダの値の並び
        const MaterialParamLayout& GetLayout() const { return layout_; }

        /// @brief Project.json の `stage` が Overlay（UI の後）か
        bool IsOverlay() const;

        /// @brief Project.json の `stage` が使える名前か
        static bool IsValidStage(const std::string& stage);

    protected:
        std::wstring GetComputeShaderPath() const override;
        std::string GetEffectName() const override { return settings_.name; }

    private:
        /// @brief マテリアルのシェーダでルートシグネチャとパイプラインを組む
        /// @return 組めなければ false（前のパイプラインはそのまま）
        bool BuildPipeline();

        ProjectPostEffectSettings settings_;
        std::shared_ptr<ShaderMaterial> material_;
        std::unique_ptr<ShaderReflectionData> reflection_;
        MaterialParamLayout layout_;

        RootSlot inputSlot_;
        RootSlot outputSlot_;
        RootSlot frameSlot_;
        RootSlot paramsSlot_;
        std::vector<RootSlot> textureSlots_;

        /// 組んだときのシェーダのパスと、シェーダの更新の番号
        std::string builtShaderPath_;
        uint64_t builtShaderRevision_ = 0;

        /// 組み直した古いパイプライン（描画中のフレームが使っているかもしれないので捨てずに持つ）
        std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> retiredPipelines_;
        std::vector<std::unique_ptr<RootSignatureManager>> retiredRootSignatures_;

        std::vector<uint8_t> packed_;
        TextureManager::LoadedTexture white_{};
        bool drivenByTransition_ = false;
    };
}
