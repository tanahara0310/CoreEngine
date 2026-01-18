#include "DebugLineDrawer.h"
#include "Engine/Graphics/Render/Line/LineRendererPipeline.h"
#include "Engine/Camera/ICamera.h"
#include "MathCore.h"
#include <numbers>

using namespace CoreEngine::MathCore;

namespace CoreEngine
{
	void DebugLineDrawer::DrawLine(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, const Line& line) {
		if (!pipeline || !camera) {
			return;
		}

		std::vector<LineRendererPipeline::LineVertex> vertices = {
			{ line.start, line.color, line.alpha },
			{ line.end, line.color, line.alpha }
		};

		pipeline->UpdateVertexBuffer(vertices);

		Matrix4x4 view = camera->GetViewMatrix();
		Matrix4x4 proj = camera->GetProjectionMatrix();
		pipeline->SetWVPMatrix(view, proj);

		pipeline->DrawLines(pipeline->GetDirectXCommon()->GetCommandList(), 2);
	}


	void DebugLineDrawer::DrawLines(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, const std::vector<Line>& lines) {
		if (!pipeline || !camera || lines.empty()) {
			return;
		}

		std::vector<LineRendererPipeline::LineVertex> vertices;
		vertices.reserve(lines.size() * 2);

		for (const auto& line : lines) {
			vertices.push_back({ line.start, line.color, line.alpha });
			vertices.push_back({ line.end, line.color, line.alpha });
		}

		pipeline->UpdateVertexBuffer(vertices);

		Matrix4x4 view = camera->GetViewMatrix();
		Matrix4x4 proj = camera->GetProjectionMatrix();
		pipeline->SetWVPMatrix(view, proj);

		pipeline->DrawLines(pipeline->GetDirectXCommon()->GetCommandList(),
			static_cast<uint32_t>(vertices.size()));
	}

	void DebugLineDrawer::DrawSphere(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera,
		const Vector3& center, float radius, const Vector3& color, float alpha, int segments) {
		auto lines = GenerateSphereLines(center, radius, color, alpha, segments);
		DrawLines(pipeline, camera, lines);
	}

	void DebugLineDrawer::DrawBox(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera,
		const Vector3& center, const Vector3& size, const Vector3& color, float alpha) {
		auto lines = GenerateBoxLines(center, size, color, alpha);
		DrawLines(pipeline, camera, lines);
	}

	void DebugLineDrawer::DrawCircle(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera,
		const Vector3& center, float radius, const Vector3& normal,
		const Vector3& color, float alpha, int segments) {
		auto lines = GenerateCircleLines(center, radius, normal, color, alpha, segments);
		DrawLines(pipeline, camera, lines);
	}

	void DebugLineDrawer::DrawCone(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera,
		const Vector3& apex, const Vector3& direction, float height, float angle,
		const Vector3& color, float alpha, int segments) {
		auto lines = GenerateConeLines(apex, direction, height, angle, color, alpha, segments);
		DrawLines(pipeline, camera, lines);
	}

	void DebugLineDrawer::DrawCylinder(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera,
		const Vector3& center, float radius, float height, const Vector3& direction,
		const Vector3& color, float alpha, int segments) {
		auto lines = GenerateCylinderLines(center, radius, height, direction, color, alpha, segments);
		DrawLines(pipeline, camera, lines);
	}

	void DebugLineDrawer::DrawAxes(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera,
		const Vector3& origin, float length, float alpha) {
		std::vector<Line> axisLines;

		// X軸（赤）
		axisLines.push_back({ origin, {origin.x + length, origin.y, origin.z}, {1.0f, 0.0f, 0.0f}, alpha });
		// Y軸（緑）
		axisLines.push_back({ origin, {origin.x, origin.y + length, origin.z}, {0.0f, 1.0f, 0.0f}, alpha });
		// Z軸（青）
		axisLines.push_back({ origin, {origin.x, origin.y, origin.z + length}, {0.0f, 0.0f, 1.0f}, alpha });

		DrawLines(pipeline, camera, axisLines);
	}

	void DebugLineDrawer::DrawGrid(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera,
		float size, int divisions, const Vector3& color, float alpha) {
		std::vector<Line> gridLines;

		float halfSize = size * 0.5f;
		float step = size / divisions;

		// X方向のライン
		for (int i = 0; i <= divisions; ++i) {
			float z = -halfSize + i * step;
			gridLines.push_back({
				{-halfSize, 0.0f, z},
				{halfSize, 0.0f, z},
				color, alpha
				});
		}

		// Z方向のライン
		for (int i = 0; i <= divisions; ++i) {
			float x = -halfSize + i * step;
			gridLines.push_back({
				{x, 0.0f, -halfSize},
				{x, 0.0f, halfSize},
				color, alpha
				});
		}

		DrawLines(pipeline, camera, gridLines);
	}

	std::vector<Line> DebugLineDrawer::GenerateSphereLines(const Vector3& center, float radius,
		const Vector3& color, float alpha, int segments) {
		std::vector<Line> lines;

		// 緯度線を描画（複数の水平円）
		for (int lat = 0; lat <= segments; ++lat) {
			float theta = (static_cast<float>(lat) / segments) * std::numbers::pi_v<float>;
			float sinTheta = std::sin(theta);
			float cosTheta = std::cos(theta);

			for (int lon = 0; lon < segments; ++lon) {
				float phi1 = (static_cast<float>(lon) / segments) * 2.0f * std::numbers::pi_v<float>;
				float phi2 = (static_cast<float>(lon + 1) / segments) * 2.0f * std::numbers::pi_v<float>;

				float sinPhi1 = std::sin(phi1);
				float cosPhi1 = std::cos(phi1);
				float sinPhi2 = std::sin(phi2);
				float cosPhi2 = std::cos(phi2);

				Vector3 p1 = {
					center.x + radius * sinTheta * cosPhi1,
					center.y + radius * cosTheta,
					center.z + radius * sinTheta * sinPhi1
				};

				Vector3 p2 = {
					center.x + radius * sinTheta * cosPhi2,
					center.y + radius * cosTheta,
					center.z + radius * sinTheta * sinPhi2
				};

				lines.push_back({ p1, p2, color, alpha });
			}
		}

		// 経度線を描画（縦の線）
		for (int lon = 0; lon < segments; ++lon) {
			float phi = (static_cast<float>(lon) / segments) * 2.0f * std::numbers::pi_v<float>;
			float sinPhi = std::sin(phi);
			float cosPhi = std::cos(phi);

			for (int lat = 0; lat < segments; ++lat) {
				float theta1 = (static_cast<float>(lat) / segments) * std::numbers::pi_v<float>;
				float theta2 = (static_cast<float>(lat + 1) / segments) * std::numbers::pi_v<float>;

				float sinTheta1 = std::sin(theta1);
				float cosTheta1 = std::cos(theta1);
				float sinTheta2 = std::sin(theta2);
				float cosTheta2 = std::cos(theta2);

				Vector3 p1 = {
					center.x + radius * sinTheta1 * cosPhi,
					center.y + radius * cosTheta1,
					center.z + radius * sinTheta1 * sinPhi
				};

				Vector3 p2 = {
					center.x + radius * sinTheta2 * cosPhi,
					center.y + radius * cosTheta2,
					center.z + radius * sinTheta2 * sinPhi
				};

				lines.push_back({ p1, p2, color, alpha });
			}
		}

		return lines;
	}

	std::vector<Line> DebugLineDrawer::GenerateBoxLines(const Vector3& center, const Vector3& size,
		const Vector3& color, float alpha) {
		std::vector<Line> lines;

		Vector3 halfSize = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
		Vector3 vertices[8] = {
			{ center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z }, // 0: 左下前
			{ center.x + halfSize.x, center.y - halfSize.y, center.z - halfSize.z }, // 1: 右下前
			{ center.x + halfSize.x, center.y + halfSize.y, center.z - halfSize.z }, // 2: 右上前
			{ center.x - halfSize.x, center.y + halfSize.y, center.z - halfSize.z }, // 3: 左上前
			{ center.x - halfSize.x, center.y - halfSize.y, center.z + halfSize.z }, // 4: 左下後
			{ center.x + halfSize.x, center.y - halfSize.y, center.z + halfSize.z }, // 5: 右下後
			{ center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z }, // 6: 右上後
			{ center.x - halfSize.x, center.y + halfSize.y, center.z + halfSize.z }  // 7: 左上後
		};

		// 前面の4辺
		lines.push_back({ vertices[0], vertices[1], color, alpha });
		lines.push_back({ vertices[1], vertices[2], color, alpha });
		lines.push_back({ vertices[2], vertices[3], color, alpha });
		lines.push_back({ vertices[3], vertices[0], color, alpha });

		// 後面の4辺
		lines.push_back({ vertices[4], vertices[5], color, alpha });
		lines.push_back({ vertices[5], vertices[6], color, alpha });
		lines.push_back({ vertices[6], vertices[7], color, alpha });
		lines.push_back({ vertices[7], vertices[4], color, alpha });

		// 前面と後面を結ぶ4辺
		lines.push_back({ vertices[0], vertices[4], color, alpha });
		lines.push_back({ vertices[1], vertices[5], color, alpha });
		lines.push_back({ vertices[2], vertices[6], color, alpha });
		lines.push_back({ vertices[3], vertices[7], color, alpha });

		return lines;
	}

	std::vector<Line> DebugLineDrawer::GenerateCircleLines(const Vector3& center, float radius,
		const Vector3& normal, const Vector3& color, float alpha, int segments) {
		std::vector<Line> lines;

		Vector3 up = { 0.0f, 1.0f, 0.0f };
		Vector3 right;

		if (std::abs(normal.y) > 0.999f) {
			right = { 1.0f, 0.0f, 0.0f };
		} else {
			right = CoreEngine::MathCore::Vector::Normalize(CoreEngine::MathCore::Vector::Cross(up, normal));
		}

		up = CoreEngine::MathCore::Vector::Normalize(CoreEngine::MathCore::Vector::Cross(normal, right));

		for (int i = 0; i < segments; ++i) {
			float angle1 = (static_cast<float>(i) / segments) * 2.0f * std::numbers::pi_v<float>;
			float angle2 = (static_cast<float>(i + 1) / segments) * 2.0f * std::numbers::pi_v<float>;

			Vector3 p1 = {
				center.x + radius * (std::cos(angle1) * right.x + std::sin(angle1) * up.x),
				center.y + radius * (std::cos(angle1) * right.y + std::sin(angle1) * up.y),
				center.z + radius * (std::cos(angle1) * right.z + std::sin(angle1) * up.z)
			};

			Vector3 p2 = {
				center.x + radius * (std::cos(angle2) * right.x + std::sin(angle2) * up.x),
				center.y + radius * (std::cos(angle2) * right.y + std::sin(angle2) * up.y),
				center.z + radius * (std::cos(angle2) * right.z + std::sin(angle2) * up.z)
			};

			lines.push_back({ p1, p2, color, alpha });
		}

		return lines;
	}

	std::vector<Line> DebugLineDrawer::GenerateConeLines(const Vector3& apex, const Vector3& direction,
		float height, float angle, const Vector3& color, float alpha, int segments) {
		std::vector<Line> lines;

		float angleRad = angle * std::numbers::pi_v<float> / 180.0f;
		float baseRadius = height * std::tan(angleRad);

		Vector3 normalizedDir = CoreEngine::MathCore::Vector::Normalize(direction);
		Vector3 baseCenter = {
			apex.x + normalizedDir.x * height,
			apex.y + normalizedDir.y * height,
			apex.z + normalizedDir.z * height
		};

		Vector3 up = { 0.0f, 1.0f, 0.0f };
		Vector3 right;

		if (std::abs(normalizedDir.y) > 0.999f) {
			right = { 1.0f, 0.0f, 0.0f };
		} else {
			right = Vector::Normalize(Vector::Cross(up, normalizedDir));
		}
		up = Vector::Normalize(Vector::Cross(normalizedDir, right));

		for (int i = 0; i < segments; ++i) {
			float angle1 = (static_cast<float>(i) / segments) * 2.0f * std::numbers::pi_v<float>;
			float angle2 = (static_cast<float>(i + 1) / segments) * 2.0f * std::numbers::pi_v<float>;

			Vector3 p1 = {
				baseCenter.x + baseRadius * (std::cos(angle1) * right.x + std::sin(angle1) * up.x),
				baseCenter.y + baseRadius * (std::cos(angle1) * right.y + std::sin(angle1) * up.y),
				baseCenter.z + baseRadius * (std::cos(angle1) * right.z + std::sin(angle1) * up.z)
			};

			Vector3 p2 = {
				baseCenter.x + baseRadius * (std::cos(angle2) * right.x + std::sin(angle2) * up.x),
				baseCenter.y + baseRadius * (std::cos(angle2) * right.y + std::sin(angle2) * up.y),
				baseCenter.z + baseRadius * (std::cos(angle2) * right.z + std::sin(angle2) * up.z)
			};

			lines.push_back({ p1, p2, color, alpha });

			if (i % (segments / 4) == 0) {
				lines.push_back({ apex, p1, color, alpha });
			}
		}

		return lines;
	}

	std::vector<Line> DebugLineDrawer::GenerateCylinderLines(const Vector3& center, float radius,
		float height, const Vector3& direction, const Vector3& color, float alpha, int segments) {
		std::vector<Line> lines;

		Vector3 normalizedDir = CoreEngine::MathCore::Vector::Normalize(direction);
		float halfHeight = height * 0.5f;

		Vector3 topCenter = {
			center.x + normalizedDir.x * halfHeight,
			center.y + normalizedDir.y * halfHeight,
			center.z + normalizedDir.z * halfHeight
		};

		Vector3 bottomCenter = {
			center.x - normalizedDir.x * halfHeight,
			center.y - normalizedDir.y * halfHeight,
			center.z - normalizedDir.z * halfHeight
		};

		Vector3 up = { 0.0f, 1.0f, 0.0f };
		Vector3 right;

		if (std::abs(normalizedDir.y) > 0.999f) {
			right = { 1.0f, 0.0f, 0.0f };
		} else {
			right = Vector::Normalize(Vector::Cross(up, normalizedDir));
		}
		up = Vector::Normalize(Vector::Cross(normalizedDir, right));

		for (int i = 0; i < segments; ++i) {
			float angle1 = (static_cast<float>(i) / segments) * 2.0f * std::numbers::pi_v<float>;
			float angle2 = (static_cast<float>(i + 1) / segments) * 2.0f * std::numbers::pi_v<float>;

			Vector3 topP1 = {
				topCenter.x + radius * (std::cos(angle1) * right.x + std::sin(angle1) * up.x),
				topCenter.y + radius * (std::cos(angle1) * right.y + std::sin(angle1) * up.y),
				topCenter.z + radius * (std::cos(angle1) * right.z + std::sin(angle1) * up.z)
			};

			Vector3 topP2 = {
				topCenter.x + radius * (std::cos(angle2) * right.x + std::sin(angle2) * up.x),
				topCenter.y + radius * (std::cos(angle2) * right.y + std::sin(angle2) * up.y),
				topCenter.z + radius * (std::cos(angle2) * right.z + std::sin(angle2) * up.z)
			};

			Vector3 bottomP1 = {
				bottomCenter.x + radius * (std::cos(angle1) * right.x + std::sin(angle1) * up.x),
				bottomCenter.y + radius * (std::cos(angle1) * right.y + std::sin(angle1) * up.y),
				bottomCenter.z + radius * (std::cos(angle1) * right.z + std::sin(angle1) * up.z)
			};

			Vector3 bottomP2 = {
				bottomCenter.x + radius * (std::cos(angle2) * right.x + std::sin(angle2) * up.x),
				bottomCenter.y + radius * (std::cos(angle2) * right.y + std::sin(angle2) * up.y),
				bottomCenter.z + radius * (std::cos(angle2) * right.z + std::sin(angle2) * up.z)
			};

			lines.push_back({ topP1, topP2, color, alpha });
			lines.push_back({ bottomP1, bottomP2, color, alpha });

			if (i % (segments / 4) == 0) {
				lines.push_back({ topP1, bottomP1, color, alpha });
			}
		}

		return lines;
	}
}
