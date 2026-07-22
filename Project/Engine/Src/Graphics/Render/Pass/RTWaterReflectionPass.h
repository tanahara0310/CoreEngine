#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
	/// @brief DXR 水面反射パス（鏡像カメラ平面反射の置き換え）
	/// @details 反射レイをトレースしヒット点を SceneColor へ再投影して
	///          水面反射カラーを生成する。GameView のみ実行する。
	///          結果は WaterSurfacePass（GameView 限定）が gReflectionTexture として消費する。
	class RTWaterReflectionPass : public RenderPass
	{
	public:
		const char* GetName() const override { return "RTWaterReflectionPass"; }

		bool IsEnabledForView(const RenderViewSettings& view) const override {
			return view.viewType == RenderViewType::GameView;
		}

		void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
		void Execute(const RenderContext& context) override;
	};
}
