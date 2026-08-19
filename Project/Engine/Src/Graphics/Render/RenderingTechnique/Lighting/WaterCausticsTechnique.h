#pragma once

#include "../RenderingTechniqueBase.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include <wrl.h>
#include <d3d12.h>

namespace CoreEngine
{
	class LightManager;

	/// @brief 水面のコースティクス（集光模様）をスクリーンスペースで生成するレンダリング技術
	class WaterCausticsTechnique : public RenderingTechniqueBase
	{
	public:
		/// @brief シェーダー側 cbuffer WaterCausticsParams と一致させること
		struct Params {
			float intensity = 0.35f;
			float depthAttenuation = 1.5f;
			float curvatureScale = 1.5f;
			float surfaceSampleRadius = 0.35f;
			float refractiveIndex = 1.333f;
			float receiverNormalStrength = 1.0f;
			float alignmentPower = 6.0f;
			float debugDisplayScale = 1.0f;
			uint32_t debugViewMode = 0;
			uint32_t debugLogEnabled = 0;
			float padding[2] = {};
			float invViewProjMatrix[16] = {}; // WorldPosition ターゲット廃止に伴う深度復元用
		};

		static constexpr Cb::Field kParamsFields[] = {
		    CB_FIELD(Params, intensity), CB_FIELD(Params, depthAttenuation), CB_FIELD(Params, curvatureScale),
		    CB_FIELD(Params, surfaceSampleRadius), CB_FIELD(Params, refractiveIndex),
		    CB_FIELD(Params, receiverNormalStrength), CB_FIELD(Params, alignmentPower),
		    CB_FIELD(Params, debugDisplayScale), CB_FIELD(Params, debugViewMode), CB_FIELD(Params, debugLogEnabled),
		    CB_FIELD(Params, padding), CB_FIELD_AS(Params, invViewProjMatrix, Cb::Float4x4),
		};
		CB_VERIFY_LAYOUT(Params, kParamsFields);
		CB_BIND_HLSL(Params, kParamsFields, "WaterCausticsParams");

		/// @brief 直近ディスパッチの診断情報（デバッグ UI 表示用）
		struct Diagnostics {
			uint32_t activeWaveCount = 0;
			uint32_t mainLightEnabled = 0;
			uint64_t outputHandle = 0;
			uint64_t normalHandle = 0;
		};

		/// @brief DeferredLighting へ合成するコースティクスの生成方式
		/// @details 両方式は同時に走らせず、ここで選んだ側だけが実行・合成される。
		///          以前は RT 出力が存在する限り無条件で優先されていたため、
		///          スクリーンスペース版と本テクニックのパラメータが死んでいた。
		enum class Backend : uint32_t {
			RayTracing = 0,  ///< RTWaterCausticsPass（DXR。既定）
			ScreenSpace = 1, ///< WaterCausticsPass（本テクニックのピクセルシェーダー）
		};

		WaterCausticsTechnique() = default;
		~WaterCausticsTechnique() override = default;

		void Initialize(DirectXCommon* dxCommon) override;
		void Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle) override;
		void DrawImGui() override;

		void SetRenderTargetName(const std::string& name) { targetName_ = name; }
		const Params& GetParams() const { return params_; }
		const Diagnostics& GetDiagnostics() const { return diagnostics_; }
		void SetParams(const Params& params);

		Backend GetBackend() const { return backend_; }
		void SetBackend(Backend backend) { backend_ = backend; }

	protected:
        /// @brief 有効/無効は CVar "r.WaterCaustics.Enabled" が保持する
        CVar<bool>* GetEnabledCVar() const override;

		std::string GetTechniqueName() const override { return "WaterCaustics"; }
		const std::wstring& GetPixelShaderPath() const override;

	private:
		struct alignas(16) MainLightConstants {
			float color[3] = { 1.0f, 1.0f, 1.0f };
			float intensity = 0.0f;
			float direction[3] = { 0.0f, -1.0f, 0.0f };
			uint32_t enabled = 0;
		};

		struct alignas(16) WaterSurfaceConstants {
			float waterHeight = 0.0f;
			uint32_t activeWaveCount = 0;
			float time = 0.0f;
			float padding = 0.0f;
			WaterWaveParam waves[kMaxWaterSurfaceWaveCount]{};
			// 水面メッシュのワールドXZ範囲（WaterCaustics.PS.hlsl の cbuffer 末尾と一致させること）。
			// regionValid == 0 なら範囲制限なし（従来挙動）
			float regionCenterXZ[2] = { 0.0f, 0.0f };
			float regionHalfExtentXZ[2] = { 0.0f, 0.0f };
			uint32_t regionValid = 0;
			float regionPadding[3] = {};
		};

		static constexpr Cb::Field kMainLightConstantsFields[] = {
		    CB_FIELD(MainLightConstants, color), CB_FIELD(MainLightConstants, intensity),
		    CB_FIELD(MainLightConstants, direction), CB_FIELD(MainLightConstants, enabled),
		};
		CB_VERIFY_LAYOUT(MainLightConstants, kMainLightConstantsFields);
		CB_BIND_HLSL(MainLightConstants, kMainLightConstantsFields, "gMainLight");

		static constexpr Cb::Field kWaterSurfaceConstantsFields[] = {
		    CB_FIELD(WaterSurfaceConstants, waterHeight), CB_FIELD(WaterSurfaceConstants, activeWaveCount),
		    CB_FIELD(WaterSurfaceConstants, time), CB_FIELD(WaterSurfaceConstants, padding),
		    CB_FIELD(WaterSurfaceConstants, waves), CB_FIELD(WaterSurfaceConstants, regionCenterXZ),
		    CB_FIELD(WaterSurfaceConstants, regionHalfExtentXZ), CB_FIELD(WaterSurfaceConstants, regionValid),
		    CB_FIELD(WaterSurfaceConstants, regionPadding),
		};
		CB_VERIFY_LAYOUT(WaterSurfaceConstants, kWaterSurfaceConstantsFields);
		CB_BIND_HLSL(WaterSurfaceConstants, kWaterSurfaceConstantsFields, "gWaterSurfaceData");

		void CreateConstantBuffers();
		void UpdateMainLightBuffer(LightManager* lightManager);
		void UpdateWaterSurfaceBuffer(const WaterSurfaceData* surfaceData);
		void UpdateParamsBuffer();

		std::string targetName_ = RenderTargetNames::WaterCausticsBuffer;
		Backend backend_ = Backend::RayTracing;
		Params params_{};
		Diagnostics diagnostics_{};
		Microsoft::WRL::ComPtr<ID3D12Resource> paramsBuffer_;
		Microsoft::WRL::ComPtr<ID3D12Resource> mainLightBuffer_;
		Microsoft::WRL::ComPtr<ID3D12Resource> waterSurfaceBuffer_;
		Params* mappedParams_ = nullptr;
		MainLightConstants* mappedMainLight_ = nullptr;
		WaterSurfaceConstants* mappedWaterSurface_ = nullptr;
	};
}
