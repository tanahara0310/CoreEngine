#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
	/// @brief 水面の屈折をレイトレーシングで生成するパス
	class RTWaterRefractionPass : public RenderPass
	{
	public:
		const char* GetName() const override { return "RTWaterRefractionPass"; }

		/// @brief GameView のみ実行する
		/// @details 屈折結果は WaterSurfacePass（GameView 限定）だけが消費する。
		bool IsEnabledForView(const RenderViewSettings& view) const override {
			return view.viewType == RenderViewType::GameView;
		}

		void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
		void Execute(const RenderContext& context) override;
	};
}
