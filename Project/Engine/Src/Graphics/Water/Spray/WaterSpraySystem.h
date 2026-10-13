#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RHI/Descriptor/UniqueDescriptor.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    struct ViewInfo;

    /// @brief 岸の波が砕ける所から出るしぶきを GPU で動かして描くシステム
    /// @details 霧はぼかした板で、しぶきは深度をならして一つながりの水の面（画面空間の流体描画）で描く
    class WaterSpraySystem
    {
    public:
        /// @brief 粒の置き場所の数
        static constexpr uint32_t kCapacity = 65536;
        /// @brief しぶきを出す範囲の一辺のマス数
        static constexpr uint32_t kEmitResolution = 256;

        /// @brief しぶきの設定（WaterCVars の r.Water.Spray.*）
        struct Settings {
            bool enabled = true;
            float amount = 1.0f;          ///< 出す量の倍率
            bool fluidRendering = true;   ///< しぶきを流体描画で描く
            float dropletRadius = 0.08f;  ///< しぶきの粒の半径 [m]
            float mistRadius = 0.25f;     ///< 霧の粒の半径 [m]
        };

        /// @brief 描くときの光と水の色（水面と同じものを WaterRenderFeature が渡す）
        struct Lighting {
            D3D12_GPU_DESCRIPTOR_HANDLE skyIrradianceSRV{}; ///< 空の放射照度の SH9
            float skyAmbientScale = 0.0f;
            bool skyAmbientEnabled = false;
            D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSRV{}; ///< 空のキューブマップ
            bool skyEnvironmentEnabled = false;
            float waterAbsorption[3] = { 0.45f, 0.06f, 0.03f }; ///< 水の吸収係数 [1/m]
        };

        /// @brief 粒を動かす 1 フレーム分の入力
        struct SimulateInput {
            D3D12_GPU_VIRTUAL_ADDRESS surfaceConstants = 0;   ///< 描かれる水面の定数（RTWaterSurfaceCommon の b1）
            D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV{}; ///< FFT の波の変位
            D3D12_GPU_DESCRIPTOR_HANDLE shoreWaveFieldSRV{};  ///< 岸の波の場
            D3D12_GPU_DESCRIPTOR_HANDLE seabedHeightSRV{};    ///< 海底の高さ
            float windowOriginXZ[2] = { 0.0f, 0.0f };         ///< 岸の波の場の範囲の XZ の最小の角 [m]
            float windowSize = 0.0f;                          ///< 岸の波の場の範囲の一辺 [m]
            float deltaTime = 0.0f;                           ///< 波の時計で進んだ時間 [s]（0 なら止める）
            uint64_t frameNumber = 0;
        };

        /// @brief 描く 1 フレーム分の入力
        struct RenderInput {
            const ViewInfo* view = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV{};
            GpuResource* sceneColor = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE sceneColorRTV{};
            GpuResource* motionVector = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE motionVectorRTV{};
            DXGI_FORMAT motionVectorFormat = DXGI_FORMAT_UNKNOWN;
            D3D12_GPU_VIRTUAL_ADDRESS lightCountsCBV = 0;
            D3D12_GPU_DESCRIPTOR_HANDLE directionalLightsSRV{};
            uint64_t frameNumber = 0;
        };

        /// @brief 粒（WaterSpray.hlsli の WaterSprayParticle と同じ並び）
        struct Particle {
            float position[3];
            float age;
            float velocity[3];
            float lifetime;
            float prevPosition[3];
            float radius;
            uint32_t kind;
            float seed;
            float pad[2];
        };
        static_assert(sizeof(Particle) == 64, "WaterSpray.hlsli の WaterSprayParticle と大きさを揃える");

        bool Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator);
        bool IsInitialized() const { return isInitialized_; }

        void SetSettings(const Settings& settings);
        const Settings& GetSettings() const { return settings_; }

        /// @brief 描くときの光と水の色を渡す
        void SetLighting(const Lighting& lighting) { lighting_ = lighting; }

        /// @brief 動かして描くか（初期化済みで、設定で有効）
        bool IsActive() const { return isInitialized_ && settings_.enabled; }

        /// @brief 粒を出して 1 フレーム分動かす
        void Simulate(ID3D12GraphicsCommandList* cmdList, const SimulateInput& input);

        /// @brief 粒を一度でも動かしたか
        bool HasParticles() const { return simulated_; }

        /// @brief 粒をシーンの色へ描く（流体描画のしぶき → 霧の順）
        void Render(ID3D12GraphicsCommandList* cmdList, const RenderInput& input);

        /// @brief 粒のバッファ（ステート追跡つき）
        GpuResource& GetParticleResource() { return particles_; }
        /// @brief 粒のバッファの SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetParticleSRVHandle() const { return particleSRV_.Get().gpuHandle; }

    private:
        /// @brief しぶきを出すパスの定数（WaterSprayEmit.CS.hlsl）
        struct EmitConstants {
            float originXZ[2] = { 0.0f, 0.0f };
            float cellSize = 1.0f;
            uint32_t resolution = kEmitResolution;
            float deltaTime = 0.0f;
            float amount = 1.0f;
            uint32_t frameIndex = 0;
            uint32_t capacity = kCapacity;
            float dropletRadius = 0.08f;
            float mistRadius = 0.25f;
            float pad[2] = { 0.0f, 0.0f };
        };
        static constexpr Cb::Field kEmitConstantsFields[] = {
            CB_FIELD(EmitConstants, originXZ), CB_FIELD(EmitConstants, cellSize),
            CB_FIELD(EmitConstants, resolution), CB_FIELD(EmitConstants, deltaTime),
            CB_FIELD(EmitConstants, amount), CB_FIELD(EmitConstants, frameIndex),
            CB_FIELD(EmitConstants, capacity), CB_FIELD(EmitConstants, dropletRadius),
            CB_FIELD(EmitConstants, mistRadius), CB_FIELD(EmitConstants, pad),
        };
        CB_VERIFY_LAYOUT(EmitConstants, kEmitConstantsFields);
        CB_BIND_HLSL(EmitConstants, kEmitConstantsFields, "WaterSprayEmitConstants");

        /// @brief しぶきを動かすパスの定数（WaterSprayUpdate.CS.hlsl）
        struct UpdateConstants {
            float deltaTime = 0.0f;
            uint32_t capacity = kCapacity;
            float seabedOriginXZ[2] = { 0.0f, 0.0f };
            float seabedSize = 0.0f;
            float pad[3] = { 0.0f, 0.0f, 0.0f };
        };
        static constexpr Cb::Field kUpdateConstantsFields[] = {
            CB_FIELD(UpdateConstants, deltaTime), CB_FIELD(UpdateConstants, capacity),
            CB_FIELD(UpdateConstants, seabedOriginXZ), CB_FIELD(UpdateConstants, seabedSize),
            CB_FIELD(UpdateConstants, pad),
        };
        CB_VERIFY_LAYOUT(UpdateConstants, kUpdateConstantsFields);
        CB_BIND_HLSL(UpdateConstants, kUpdateConstantsFields, "WaterSprayUpdateConstants");

        /// @brief 深度をならすパスの定数（WaterSprayDepthSmooth.CS.hlsl）
        struct SmoothConstants {
            uint32_t screenSize[2] = { 1, 1 };
            float pixelsPerMeter = 1.0f;
            float worldRadius = 0.2f;
            float depthFalloff = 0.15f;
            float pad[3] = { 0.0f, 0.0f, 0.0f };
        };
        static constexpr Cb::Field kSmoothConstantsFields[] = {
            CB_FIELD(SmoothConstants, screenSize), CB_FIELD(SmoothConstants, pixelsPerMeter),
            CB_FIELD(SmoothConstants, worldRadius), CB_FIELD(SmoothConstants, depthFalloff),
            CB_FIELD(SmoothConstants, pad),
        };
        CB_VERIFY_LAYOUT(SmoothConstants, kSmoothConstantsFields);
        CB_BIND_HLSL(SmoothConstants, kSmoothConstantsFields, "WaterSpraySmoothConstants");

        /// @brief 描くパスの共通の定数（WaterSprayView.hlsli）
        struct ViewConstants {
            Matrix4x4 viewProjection{};
            Matrix4x4 prevViewProjection{};
            Vector3 cameraPosition{ 0.0f, 0.0f, 0.0f };
            float projScaleX = 1.0f;
            Vector3 cameraRight{ 1.0f, 0.0f, 0.0f };
            float projScaleY = 1.0f;
            Vector3 cameraUp{ 0.0f, 1.0f, 0.0f };
            uint32_t fluidRendering = 0;
            Vector3 cameraForward{ 0.0f, 0.0f, 1.0f };
            float skyAmbientScale = 0.0f;
            Vector2 screenSize{ 1.0f, 1.0f };
            Vector2 projZW{ 1.0f, 0.0f };
            Vector3 waterAbsorption{ 0.45f, 0.06f, 0.03f };
            uint32_t skyAmbientEnabled = 0;
            uint32_t skyEnvironmentEnabled = 0;
            Vector3 pad{ 0.0f, 0.0f, 0.0f };
        };
        static constexpr Cb::Field kViewConstantsFields[] = {
            CB_FIELD(ViewConstants, viewProjection), CB_FIELD(ViewConstants, prevViewProjection),
            CB_FIELD(ViewConstants, cameraPosition), CB_FIELD(ViewConstants, projScaleX),
            CB_FIELD(ViewConstants, cameraRight), CB_FIELD(ViewConstants, projScaleY),
            CB_FIELD(ViewConstants, cameraUp), CB_FIELD(ViewConstants, fluidRendering),
            CB_FIELD(ViewConstants, cameraForward), CB_FIELD(ViewConstants, skyAmbientScale),
            CB_FIELD(ViewConstants, screenSize), CB_FIELD(ViewConstants, projZW),
            CB_FIELD(ViewConstants, waterAbsorption), CB_FIELD(ViewConstants, skyAmbientEnabled),
            CB_FIELD(ViewConstants, skyEnvironmentEnabled), CB_FIELD(ViewConstants, pad),
        };
        CB_VERIFY_LAYOUT(ViewConstants, kViewConstantsFields);
        CB_BIND_HLSL(ViewConstants, kViewConstantsFields, "WaterSprayViewConstants");

        struct EmitShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterSprayEmit.CS.hlsl"; }
        };
        struct UpdateShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterSprayUpdate.CS.hlsl"; }
        };
        struct SmoothShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"WaterSprayDepthSmooth.CS.hlsl"; }
        };

        /// @brief 頂点・ピクセルシェーダーの組と、そのルートシグネチャ・PSO
        struct GraphicsProgram {
            std::unique_ptr<RootSignatureManager> rootSignature;
            PipelineStateManager pipelineStates;
            BlendMode blendMode = BlendMode::kBlendModeNone;

            ID3D12PipelineState* GetPipelineState() { return pipelineStates.GetPipelineState(blendMode); }
            ID3D12RootSignature* GetRootSignature() { return rootSignature ? rootSignature->GetRootSignature() : nullptr; }
            RootSlot GetSlot(const std::string& name) const { return rootSignature ? rootSignature->GetRootSlot(name) : RootSlot{}; }
        };

        /// @brief 描画先の形（GraphicsProgram を作るときに渡す）
        struct GraphicsTargetDesc {
            const DXGI_FORMAT* renderTargetFormats = nullptr;
            uint32_t renderTargetCount = 0;
            DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
            bool depthTest = false;
            BlendMode blendMode = BlendMode::kBlendModeNone;
            bool writeAlpha = true;
        };

        /// @brief 画面の大きさのテクスチャと、そのビュー
        struct ScreenTexture : GpuResource {
            UniqueDescriptor srv;
            UniqueDescriptor uav;
            UniqueDescriptor rtv;
            UniqueDescriptor dsv;
        };

        bool CreatePipelines();
        bool BuildGraphicsProgram(
            ShaderCompiler& shaderCompiler, ShaderReflectionBuilder& reflectionBuilder,
            GraphicsProgram& program, const wchar_t* vertexShader, const wchar_t* pixelShader,
            const GraphicsTargetDesc& target, const char* name);
        bool CreateParticleBuffers();

        /// @brief 画面の大きさのテクスチャを用意する（大きさが変わったら作り直す）
        bool EnsureScreenTextures(uint32_t width, uint32_t height, DXGI_FORMAT sceneColorFormat);
        bool CreateScreenTexture(
            ScreenTexture& texture, uint32_t width, uint32_t height, DXGI_FORMAT format,
            D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue,
            const char* name);
        void ReleaseScreenTexture(ScreenTexture& texture);

        /// @brief 描くパスの共通の定数を組み立てる
        ViewConstants BuildViewConstants(const ViewInfo& view, uint32_t width, uint32_t height) const;

        /// @brief 描くパスの共通の資源（定数・光・空・シーンの深度）を差す
        void BindViewResources(
            ID3D12GraphicsCommandList* cmdList, GraphicsProgram& program,
            D3D12_GPU_VIRTUAL_ADDRESS viewConstants, const RenderInput& input);

        /// @brief しぶきを流体描画で描く（深度と厚み → 深度をならす → 合成）
        void RenderFluid(
            ID3D12GraphicsCommandList* cmdList, const RenderInput& input,
            D3D12_GPU_VIRTUAL_ADDRESS viewConstants, uint32_t width, uint32_t height);

        /// @brief 霧（と、流体描画を使わないときのしぶき）を板で描く
        void RenderParticles(
            ID3D12GraphicsCommandList* cmdList, const RenderInput& input,
            D3D12_GPU_VIRTUAL_ADDRESS viewConstants, uint32_t width, uint32_t height, bool fluid);

        /// @brief 一覧の粒を、描く引数の場所 argsIndex（kSprayArgs*）の数だけ描く
        void DrawParticleList(
            ID3D12GraphicsCommandList* cmdList, GraphicsProgram& program,
            const UniqueDescriptor& listSRV, uint32_t argsIndex);

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        bool isInitialized_ = false;
        Settings settings_{};
        Lighting lighting_{};

        CustomShaderPipeline emitPipeline_{};
        CustomShaderPipeline updatePipeline_{};
        CustomShaderPipeline smoothPipeline_{};
        EmitShaderProvider emitShaderProvider_{};
        UpdateShaderProvider updateShaderProvider_{};
        SmoothShaderProvider smoothShaderProvider_{};

        GraphicsProgram particleProgram_{};
        GraphicsProgram splatDepthProgram_{};
        GraphicsProgram splatThicknessProgram_{};
        GraphicsProgram compositeProgram_{};
        DXGI_FORMAT compositeMotionVectorFormat_ = DXGI_FORMAT_UNKNOWN;

        GpuResource particles_{};
        UniqueDescriptor particleSRV_{};
        UniqueDescriptor particleUAV_{};
        GpuResource counter_{};
        UniqueDescriptor counterUAV_{};

        /// @brief 描く粒の引数（WaterSpray.hlsli の kSprayArgs*。しぶき・霧の DRAW 引数と、生きているしぶきの数）
        GpuResource drawArgs_{};
        UniqueDescriptor drawArgsUAV_{};
        /// @brief 描く引数の初期値（毎フレーム drawArgs_ へ写す）
        Microsoft::WRL::ComPtr<ID3D12Resource> drawArgsInitial_;
        /// @brief 生きているしぶき・霧の粒の番号の一覧
        GpuResource dropletList_{};
        UniqueDescriptor dropletListSRV_{};
        UniqueDescriptor dropletListUAV_{};
        GpuResource mistList_{};
        UniqueDescriptor mistListSRV_{};
        UniqueDescriptor mistListUAV_{};
        /// @brief 引数 1 つで DRAW する間接描画のシグネチャ
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawSignature_;

        ScreenTexture splatDepth_{};      ///< 一番手前の球の、カメラの前方向の距離（R32F）
        ScreenTexture splatDepthBuffer_{};///< 一番手前の球を残す深度バッファ（D32F）
        ScreenTexture smoothDepth_[2]{};  ///< ならした深度（1 回目 → 2 回目）
        ScreenTexture thickness_{};       ///< (厚み, 厚み × 動き x, 厚み × 動き y)
        ScreenTexture background_{};      ///< 合成前のシーンの色の写し
        uint32_t screenWidth_ = 0;
        uint32_t screenHeight_ = 0;
        DXGI_FORMAT backgroundFormat_ = DXGI_FORMAT_UNKNOWN;

        bool simulated_ = false;
        uint32_t emitFrameIndex_ = 0;
        uint64_t lastSimulateFrame_ = UINT64_MAX;
        Matrix4x4 prevViewProjection_{};
        bool hasPrevViewProjection_ = false;
    };
}
