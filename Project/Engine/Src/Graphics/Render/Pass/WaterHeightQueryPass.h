#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
	/// @brief 水面の高さを聞く点を GPU で評価するパス（FFT の波面を計算した後）
	class WaterHeightQueryPass final : public RenderPass
	{
	public:
		const char* GetName() const override { return "WaterHeightQueryPass"; }
		void Execute(const RenderContext& context) override;

	private:
		uint64_t lastDispatchFrame_ = UINT64_MAX; ///< 最後に評価したフレーム番号
	};
}
