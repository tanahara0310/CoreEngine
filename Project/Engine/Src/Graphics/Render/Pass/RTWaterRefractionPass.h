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
		///          また入力の SceneColorSnapshot / G-Buffer WorldPosition は
		///          反射ビュー（半解像度 G-Buffer）では整合しない。
		bool IsEnabledForView(const RenderViewSettings& view) const override {
			return view.viewType == RenderViewType::GameView;
		}

		void DeclareResources(RenderGraphBuilder& builder, const RenderContext& context) override;
		void Execute(const RenderContext& context) override;
	};
}
