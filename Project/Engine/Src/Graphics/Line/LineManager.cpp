#include "pch.h"
#include "LineManager.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Math/MathCore.h"
#include <cassert>
#include <numbers>


namespace CoreEngine
{

LineManager& LineManager::GetInstance() {
    static LineManager instance;
    return instance;
}

void LineManager::Initialize(LineRendererPipeline* lineRenderer) {
    assert(lineRenderer != nullptr);
    lineRenderer_ = lineRenderer;
}

void LineManager::DrawLine(const Vector3& start, const Vector3& end, const Vector3& color, float alpha,
                           bool depthTest) {
    if (!lineRenderer_) {
        return;
    }

    Line line(start, end, color, alpha, depthTest);
    lineRenderer_->AddLine(line);
}

void LineManager::DrawLine(const Line& line) {
    if (!lineRenderer_) {
        return;
    }

    lineRenderer_->AddLine(line);
}

void LineManager::DrawLines(const std::vector<Line>& lines) {
    if (!lineRenderer_) {
        return;
    }

    lineRenderer_->AddLines(lines);
}

void LineManager::DrawGrid(float size, int divisions, const Vector3& center, const Vector3& color, float alpha) {
if (!lineRenderer_) {
    return;
}

float halfSize = size * 0.5f;
    float step = size / static_cast<float>(divisions);

    // X方向のライン
    for (int i = 0; i <= divisions; ++i) {
        float z = -halfSize + step * i;
        Vector3 start = center + Vector3(-halfSize, 0.0f, z);
        Vector3 end = center + Vector3(halfSize, 0.0f, z);
        DrawLine(start, end, color, alpha);
    }

    // Z方向のライン
    for (int i = 0; i <= divisions; ++i) {
        float x = -halfSize + step * i;
        Vector3 start = center + Vector3(x, 0.0f, -halfSize);
        Vector3 end = center + Vector3(x, 0.0f, halfSize);
        DrawLine(start, end, color, alpha);
    }
}

void LineManager::DrawWireBox(const Vector3& center, const Vector3& size, const Vector3& color, float alpha) {
if (!lineRenderer_) {
    return;
}

Vector3 halfSize = size * 0.5f;

    // 8つの頂点を定義
    Vector3 vertices[8] = {
        center + Vector3(-halfSize.x, -halfSize.y, -halfSize.z),
        center + Vector3( halfSize.x, -halfSize.y, -halfSize.z),
        center + Vector3( halfSize.x,  halfSize.y, -halfSize.z),
        center + Vector3(-halfSize.x,  halfSize.y, -halfSize.z),
        center + Vector3(-halfSize.x, -halfSize.y,  halfSize.z),
        center + Vector3( halfSize.x, -halfSize.y,  halfSize.z),
        center + Vector3( halfSize.x,  halfSize.y,  halfSize.z),
        center + Vector3(-halfSize.x,  halfSize.y,  halfSize.z),
    };

    // 12本のエッジを描画
    // 下面
    DrawLine(vertices[0], vertices[1], color, alpha);
    DrawLine(vertices[1], vertices[2], color, alpha);
    DrawLine(vertices[2], vertices[3], color, alpha);
    DrawLine(vertices[3], vertices[0], color, alpha);

    // 上面
    DrawLine(vertices[4], vertices[5], color, alpha);
    DrawLine(vertices[5], vertices[6], color, alpha);
    DrawLine(vertices[6], vertices[7], color, alpha);
    DrawLine(vertices[7], vertices[4], color, alpha);

    // 縦のエッジ
    DrawLine(vertices[0], vertices[4], color, alpha);
    DrawLine(vertices[1], vertices[5], color, alpha);
    DrawLine(vertices[2], vertices[6], color, alpha);
    DrawLine(vertices[3], vertices[7], color, alpha);
}

void LineManager::DrawAxis(const Vector3& origin, float length, float alpha) {
if (!lineRenderer_) {
    return;
}

// X軸（赤）
    DrawLine(origin, origin + Vector3(length, 0.0f, 0.0f), {1.0f, 0.0f, 0.0f}, alpha);

    // Y軸（緑）
    DrawLine(origin, origin + Vector3(0.0f, length, 0.0f), {0.0f, 1.0f, 0.0f}, alpha);

    // Z軸（青）
    DrawLine(origin, origin + Vector3(0.0f, 0.0f, length), {0.0f, 0.0f, 1.0f}, alpha);
}

void LineManager::DrawCircle(const Vector3& center, float radius, int segments, const Vector3& color, float alpha) {
if (!lineRenderer_ || segments < 3) {
    return;
}

float angleStep = (std::numbers::pi_v<float> * 2.0f) / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        float angle1 = angleStep * i;
        float angle2 = angleStep * (i + 1);

        Vector3 point1 = center + Vector3(
            std::cos(angle1) * radius,
            0.0f,
            std::sin(angle1) * radius
        );

        Vector3 point2 = center + Vector3(
            std::cos(angle2) * radius,
            0.0f,
            std::sin(angle2) * radius
        );

        DrawLine(point1, point2, color, alpha);
    }
}

void LineManager::DrawWireSphere(const Vector3& center, float radius, int segments, const Vector3& color, float alpha) {
if (!lineRenderer_ || segments < 3) {
    return;
}

// XY平面の円
float angleStep = (std::numbers::pi_v<float> * 2.0f) / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float angle1 = angleStep * i;
        float angle2 = angleStep * (i + 1);

        Vector3 point1 = center + Vector3(
            std::cos(angle1) * radius,
            std::sin(angle1) * radius,
            0.0f
        );

        Vector3 point2 = center + Vector3(
            std::cos(angle2) * radius,
            std::sin(angle2) * radius,
            0.0f
        );

        DrawLine(point1, point2, color, alpha);
    }

    // YZ平面の円
    for (int i = 0; i < segments; ++i) {
        float angle1 = angleStep * i;
        float angle2 = angleStep * (i + 1);

        Vector3 point1 = center + Vector3(
            0.0f,
            std::cos(angle1) * radius,
            std::sin(angle1) * radius
        );

        Vector3 point2 = center + Vector3(
            0.0f,
            std::cos(angle2) * radius,
            std::sin(angle2) * radius
        );

        DrawLine(point1, point2, color, alpha);
    }

    // XZ平面の円
    for (int i = 0; i < segments; ++i) {
        float angle1 = angleStep * i;
        float angle2 = angleStep * (i + 1);

        Vector3 point1 = center + Vector3(
            std::cos(angle1) * radius,
            0.0f,
            std::sin(angle1) * radius
        );

        Vector3 point2 = center + Vector3(
            std::cos(angle2) * radius,
            0.0f,
            std::sin(angle2) * radius
        );

        DrawLine(point1, point2, color, alpha);
    }
}

void LineManager::DrawCross(const Vector3& position, float size, const Vector3& color, float alpha,
                            bool depthTest) {
if (!lineRenderer_) {
    return;
}

float halfSize = size * 0.5f;

    // X軸方向
    DrawLine(
        position + Vector3(-halfSize, 0.0f, 0.0f),
        position + Vector3( halfSize, 0.0f, 0.0f),
        color, alpha, depthTest
    );

    // Y軸方向
    DrawLine(
        position + Vector3(0.0f, -halfSize, 0.0f),
        position + Vector3(0.0f,  halfSize, 0.0f),
        color, alpha, depthTest
    );

    // Z軸方向
    DrawLine(
        position + Vector3(0.0f, 0.0f, -halfSize),
        position + Vector3(0.0f, 0.0f,  halfSize),
        color, alpha, depthTest
    );
}

void LineManager::ClearAll() {
    if (!lineRenderer_) {
        return;
    }

    lineRenderer_->ClearBatch();
}

// ===== ライン配列生成ヘルパー関数群 =====

std::vector<Line> LineManager::GenerateSphereLines(const Vector3& center, float radius,
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

std::vector<Line> LineManager::GenerateBoxLines(const Vector3& center, const Vector3& size,
    const Vector3& color, float alpha) {
    return GenerateBoxLines(center, size,
        { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, color, alpha);
}

std::vector<Line> LineManager::GenerateBoxLines(const Vector3& center, const Vector3& size,
    const Vector3& axisX, const Vector3& axisY, const Vector3& axisZ,
    const Vector3& color, float alpha) {
    std::vector<Line> lines;

    const Vector3 extentX = axisX * (size.x * 0.5f);
    const Vector3 extentY = axisY * (size.y * 0.5f);
    const Vector3 extentZ = axisZ * (size.z * 0.5f);

    Vector3 vertices[8] = {
        center - extentX - extentY - extentZ,
        center + extentX - extentY - extentZ,
        center + extentX + extentY - extentZ,
        center - extentX + extentY - extentZ,
        center - extentX - extentY + extentZ,
        center + extentX - extentY + extentZ,
        center + extentX + extentY + extentZ,
        center - extentX + extentY + extentZ
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

std::vector<Line> LineManager::GenerateCircleLines(const Vector3& center, float radius,
    const Vector3& normal, const Vector3& color, float alpha, int segments) {
    std::vector<Line> lines;

    Vector3 up = { 0.0f, 1.0f, 0.0f };
    Vector3 right;

    if (std::abs(normal.y) > 0.999f) {
        right = { 1.0f, 0.0f, 0.0f };
    } else {
        right = CoreEngine::Normalize(CoreEngine::Cross(up, normal));
    }

    up = CoreEngine::Normalize(CoreEngine::Cross(normal, right));

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

std::vector<Line> LineManager::GenerateConeLines(const Vector3& apex, const Vector3& direction,
    float height, float angle, const Vector3& color, float alpha, int segments) {
    // 頂点から底面へ向かう円錐のワイヤー。底面半径は height * tan(半頂角) で決まる
    std::vector<Line> lines;

    float angleRad = angle * std::numbers::pi_v<float> / 180.0f;
    float baseRadius = height * std::tan(angleRad);

    Vector3 normalizedDir = CoreEngine::Normalize(direction);
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
        right = CoreEngine::Normalize(CoreEngine::Cross(up, normalizedDir));
    }
    up = CoreEngine::Normalize(CoreEngine::Cross(normalizedDir, right));

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

std::vector<Line> LineManager::GenerateCylinderLines(const Vector3& center, float radius,
    float height, const Vector3& direction, const Vector3& color, float alpha, int segments) {
    std::vector<Line> lines;
    // 上下の円と、それを結ぶ側面の線で円柱のワイヤーを作る

    Vector3 normalizedDir = CoreEngine::Normalize(direction);
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

    // 軸が真上（または真下）に近いと up と平行になり外積がゼロになるので、
    // その場合だけ基準軸を X へ差し替える
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    Vector3 right;

    if (std::abs(normalizedDir.y) > 0.999f) {
        right = { 1.0f, 0.0f, 0.0f };
    } else {
        right = CoreEngine::Normalize(CoreEngine::Cross(up, normalizedDir));
    }
    up = CoreEngine::Normalize(CoreEngine::Cross(normalizedDir, right));

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
