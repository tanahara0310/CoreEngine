#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
	class FFTOceanPass final : public RenderPass
	{
	public:
		const char* GetName() const override { return "FFTOceanPass"; }
		void Execute(const RenderContext& context) override;
	};
}
