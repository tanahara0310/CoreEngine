#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
	class RTWaterRefractionPass : public RenderPass
	{
	public:
		const char* GetName() const override { return "RTWaterRefractionPass"; }
		void Execute(const RenderContext& context) override;
	};
}
