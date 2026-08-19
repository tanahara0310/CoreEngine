#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
	/// @brief FFT 海面のシミュレーションを 1 フレーム進めるパス
	class FFTOceanPass final : public RenderPass
	{
	public:
		const char* GetName() const override { return "FFTOceanPass"; }
		void Execute(const RenderContext& context) override;

	private:
		uint64_t lastDispatchFrame_ = UINT64_MAX; ///< 最後に波面を計算したフレーム番号
	};
}
