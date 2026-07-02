#pragma once

#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"

namespace CoreEngine
{
	/// @brief シリンダーメッシュを生成するジェネレーター
	class CylinderMeshGenerator : public IPrimitiveMeshGenerator {
	public:
		/// @param topRadius 上面半径
		/// @param bottomRadius 下面半径
		/// @param height 高さ
		/// @param divisions 円周分割数
		CylinderMeshGenerator(float topRadius = 0.5f, float bottomRadius = 0.5f,
							  float height = 1.0f, uint32_t divisions = 32);

		ModelData Generate() const override;
		std::string GetCacheKey() const override;

	private:
		float topRadius_;
		float bottomRadius_;
		float height_;
		uint32_t divisions_;
	};
}
