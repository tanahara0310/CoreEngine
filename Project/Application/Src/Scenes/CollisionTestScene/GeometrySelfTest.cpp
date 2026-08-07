#include "pch.h"
#include "GeometrySelfTest.h"
#include "CollisionTestReport.h"

#include "Math/Geometry/Intersect.h"
#include "Math/Geometry/RayCast.h"
#include "Math/MathCore.h"

#include "Collision/BroadPhase/BruteForceBroadPhase.h"
#include "Collision/BroadPhase/UniformGridBroadPhase.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace CollisionTest
{
    namespace {
        using namespace CoreEngine;
        using CoreEngine::Geometry::AABB;
        using CoreEngine::Geometry::Capsule;
        using CoreEngine::Geometry::Plane;
        using CoreEngine::Geometry::Ray;
        using CoreEngine::Geometry::RayHit;
        using CoreEngine::Geometry::Sphere;

        /// @brief 数値を短い文字列へ（NaN / inf も判別できるように）
        std::string ToStr(float v)
        {
            if (std::isnan(v)) { return "NaN"; }
            if (std::isinf(v)) { return v > 0.0f ? "+inf" : "-inf"; }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f", v);
            return buf;
        }

        std::string ToStr(const Vector3& v)
        {
            return "(" + ToStr(v.x) + ", " + ToStr(v.y) + ", " + ToStr(v.z) + ")";
        }

        std::string ToStr(bool b) { return b ? "true" : "false"; }

        /// @brief 1 件のテスト結果を Report へ登録
        void Record(const char* id, const char* title,
                    const std::string& expected, const std::string& actual,
                    bool passed, const char* knownBug = "", const char* note = "")
        {
            CaseResult result;
            result.id       = id;
            result.title    = title;
            result.expected = expected;
            result.actual   = actual;
            result.knownBug = knownBug;
            result.note     = note;
            result.status   = passed ? Status::Pass : Status::Fail;
            Report::Get().Upsert(result);
        }

        /// @brief bool を期待するテスト
        void CheckBool(const char* id, const char* title, bool expected, bool actual,
                       const char* knownBug = "", const char* note = "")
        {
            Record(id, title, ToStr(expected), ToStr(actual), expected == actual, knownBug, note);
        }
    }

    void RunGeometrySelfTest()
    {
        namespace G = CoreEngine::Geometry;

        // ── 球 × 球 ────────────────────────────────────────────────
        CheckBool("U1", "Sphere x Sphere: 離れている",
            false,
            G::Intersect(Sphere({ 0.0f, 0.0f, 0.0f }, 1.0f),
                         Sphere({ 5.0f, 0.0f, 0.0f }, 1.0f)));

        // 中心距離 == 半径和。<= 比較なので接触扱い（この境界仕様を固定する）
        CheckBool("U2", "Sphere x Sphere: 境界でちょうど接触",
            true,
            G::Intersect(Sphere({ 0.0f, 0.0f, 0.0f }, 1.0f),
                         Sphere({ 2.0f, 0.0f, 0.0f }, 1.0f)),
            "", "中心距離 == 半径和 のとき衝突とみなす仕様。実装を差し替えても変えないこと。");

        // ── AABB × AABB ────────────────────────────────────────────
        CheckBool("U3", "AABB x AABB: 面でちょうど接触",
            true,
            G::Intersect(AABB({ -1.0f, -1.0f, -1.0f }, { 0.0f, 1.0f, 1.0f }),
                         AABB({ 0.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f })));

        CheckBool("U4", "AABB x AABB: わずかに隙間あり",
            false,
            G::Intersect(AABB({ -1.0f, -1.0f, -1.0f }, { -0.01f, 1.0f, 1.0f }),
                         AABB({ 0.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f })));

        // ── 球 × AABB ──────────────────────────────────────────────
        CheckBool("U5", "Sphere x AABB: 面でちょうど接触",
            true,
            G::Intersect(Sphere({ 2.0f, 0.0f, 0.0f }, 1.0f),
                         AABB({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f })));

        CheckBool("U6", "Point x AABB: 境界上の点",
            true,
            G::Contains(AABB({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }),
                        Vector3{ 1.0f, 0.0f, 0.0f }));

        // ── カプセル ────────────────────────────────────────────────
        // 長さ 0 のカプセルは球と同じ答えになるべき（縮退分岐の確認）
        {
            const Vector3 p{ 0.0f, 0.0f, 0.0f };
            const Capsule degenerate({ 3.0f, 0.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, 1.0f);
            const bool capsuleHit = G::Contains(degenerate, p);
            const bool sphereHit  = G::Contains(Sphere({ 3.0f, 0.0f, 0.0f }, 1.0f), p);
            Record("U7", "Capsule(長さ0) は球と同じ答えを返す",
                "capsule == sphere",
                "capsule=" + ToStr(capsuleHit) + " / sphere=" + ToStr(sphereHit),
                capsuleHit == sphereHit);
        }

        CheckBool("U8", "Capsule x Capsule: 平行で半径内",
            true,
            G::Intersect(Capsule({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 4.0f }, 0.5f),
                         Capsule({ 0.8f, 0.0f, 0.0f }, { 0.8f, 0.0f, 4.0f }, 0.5f)));

        // ── レイ ────────────────────────────────────────────────────
        // 球の内部から撃つと前方の交点（t > 0）が返るべき
        {
            RayHit hit{};
            const bool found = G::Raycast(Ray({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }),
                                          Sphere({ 0.0f, 0.0f, 0.0f }, 2.0f), &hit);
            Record("U9", "Ray x Sphere: 球の内部から前方へ",
                "交点あり かつ distance > 0",
                found ? ("交点=" + ToStr(hit.point) + " distance=" + ToStr(hit.distance))
                      : std::string("交点なし"),
                found && hit.distance > 0.0f);
        }

        // 平面と平行なレイは交点なし
        {
            RayHit hit{};
            const bool found = G::Raycast(Ray({ 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }),
                                          Plane({ 0.0f, 1.0f, 0.0f }, 0.0f), &hit);
            Record("U10", "Ray x Plane: 平面と平行",
                "交点なし",
                found ? ("交点=" + ToStr(hit.point)) : std::string("交点なし"),
                !found);
        }

        // 斜めレイは入口点を返すべき
        {
            RayHit hit{};
            const bool found = G::Raycast(Ray({ -5.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }),
                                          AABB({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }), &hit);
            Record("U11", "Ray x AABB: 軸に沿った通常ヒット（入口 x=-1）",
                "交点 x = -1.000",
                found ? ("交点=" + ToStr(hit.point)) : std::string("交点なし"),
                found && std::abs(hit.point.x - (-1.0f)) < 1e-3f);
        }

        // 軸平行レイ + 原点がスラブ境界上（かつては 1/0 = inf → 0 * inf = NaN の温床だった）
        {
            RayHit hit{};
            const bool found = G::Raycast(Ray({ -1.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }),
                                          AABB({ -1.0f, 0.0f, -1.0f }, { 1.0f, 2.0f, 1.0f }), &hit);
            Record("U12", "Ray x AABB: 軸平行レイが原点でスラブ境界に一致",
                "交点 y = 2.000（入口=上面）",
                found ? ("交点=" + ToStr(hit.point)) : std::string("交点なし"),
                found && std::abs(hit.point.y - 2.0f) < 1e-3f,
                "A-6",
                "軸ごとに平行判定して逆数を作らない実装であることの確認。"
                "無条件に 1/dir を取ると 0 * inf = NaN となり、棄却判定がすり抜ける。");
        }

        // 反平行ベクトルの Slerp は単位長を保つべき
        {
            const Vector3 result = MathCore::Vector::Slerp({ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, 0.5f);
            const float length = Length(result);
            Record("U13", "Slerp: 反平行ベクトルの中間",
                "結果の長さ = 1.000",
                "結果=" + ToStr(result) + " 長さ=" + ToStr(length),
                std::abs(length - 1.0f) < 1e-2f,
                "A-7",
                "end - dot*start がゼロベクトルになり回転面が決まらないケース。"
                "Normalize(0) に任せると結果が単位ベクトルでなくなる。");
        }

        // ── 接触情報（Phase 2 で追加） ──────────────────────────────
        // 球×AABB と AABB×球は同じ答えで、法線だけが反転すること。
        // 球×AABB の判定はかつて SphereCollider と AABBCollider に二重実装されていた。
        {
            // 角にまたがる配置。法線が軸平行にならないので転送側の反転漏れを拾いやすい。
            // 中心 (1.4, 1.2, 0) の最近接点は箱の角 (1, 1, 0)、距離 ≒ 0.447 < 半径 1.0。
            const Sphere sphere({ 1.4f, 1.2f, 0.0f }, 1.0f);
            const AABB   box({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });

            G::Contact forward{}, reversed{};
            const bool hitForward  = G::Intersect(sphere, box, &forward);
            const bool hitReversed = G::Intersect(box, sphere, &reversed);

            const bool sameHit = (hitForward == hitReversed);
            const bool sameDepth = std::abs(forward.depth - reversed.depth) < 1e-4f;
            const bool flipped =
                std::abs(forward.normal.x + reversed.normal.x) < 1e-4f &&
                std::abs(forward.normal.y + reversed.normal.y) < 1e-4f &&
                std::abs(forward.normal.z + reversed.normal.z) < 1e-4f;

            Record("U14", "Sphere x AABB と AABB x Sphere が対称",
                "接触あり・同じ depth・法線は反転",
                "hit=" + ToStr(hitForward) + "/" + ToStr(hitReversed)
                    + " depth=" + ToStr(forward.depth) + "/" + ToStr(reversed.depth)
                    + " normal=" + ToStr(forward.normal) + "/" + ToStr(reversed.normal),
                // hitForward を条件に入れないと「両方とも非接触」で空振り PASS する
                hitForward && sameHit && sameDepth && flipped,
                "B-1",
                "実装は Geometry 側の 1 箇所だけ。転送側が法線を反転していることの確認。");
        }

        // 貫通深度が幾何的に正しいこと（球が箱に 0.5 めり込む配置）
        {
            const Sphere sphere({ 1.5f, 0.0f, 0.0f }, 1.0f);
            const AABB   box({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });

            G::Contact contact{};
            const bool hit = G::Intersect(sphere, box, &contact);
            // 中心から面までの距離 0.5、半径 1.0 → depth = 0.5
            // 法線は「球 → 箱」なので -X 方向
            const bool depthOk  = hit && std::abs(contact.depth - 0.5f) < 1e-4f;
            const bool normalOk = hit && std::abs(contact.normal.x - (-1.0f)) < 1e-4f;

            Record("U15", "Sphere x AABB: 貫通深度と法線",
                "depth=0.500 normal=(-1, 0, 0)",
                hit ? ("depth=" + ToStr(contact.depth) + " normal=" + ToStr(contact.normal))
                    : std::string("非接触"),
                depthOk && normalOk,
                "",
                "normal は A から B へ押し出す向き（ここでは球 → 箱）。"
                "Phase 4 の押し出し実装がこの規約に依存する。");
        }

        // ── ブロードフェーズ（Phase 5 で追加） ──────────────────────
        // 実装を差し替えても同じ候補ペアが出ることを機械的に確認する。
        // BroadPhase は Collider を知らない純データ設計なので、GameObject を
        // 作らずにここで直接テストできる。
        {
            namespace BP = CoreEngine::BroadPhase;

            // 決定的な配置（乱数を使わない）で 60 個の箱を撒く
            std::vector<BP::Proxy> proxies;
            for (int i = 0; i < 60; ++i) {
                const float x = static_cast<float>((i * 7) % 23) - 11.0f;
                const float y = static_cast<float>((i * 5) % 11) - 5.0f;
                const float z = static_cast<float>((i * 3) % 17) - 8.0f;

                BP::Proxy proxy;
                proxy.bounds     = AABB({ x - 1.0f, y - 1.0f, z - 1.0f }, { x + 1.0f, y + 1.0f, z + 1.0f });
                proxy.layerIndex = static_cast<uint32_t>(i % 3);
                proxy.layerMask  = 0b111;             // 3 レイヤーすべてと当たる
                proxy.ownerId    = static_cast<uint64_t>(i / 2 + 1);  // 2 個ずつ同じオーナー
                proxy.isStatic   = (i % 5 == 0);
                proxies.push_back(proxy);
            }

            auto collect = [&proxies](BP::IBroadPhase& phase) {
                phase.Clear();
                for (const auto& p : proxies) { phase.Insert(p); }
                std::vector<BP::PairIndices> pairs;
                phase.CollectPairs(pairs);
                std::sort(pairs.begin(), pairs.end());
                return pairs;
                };

            BP::BruteForceBroadPhase bruteForce;
            BP::UniformGridBroadPhase grid(4.0f);   // 物体サイズ 2 に対してセル 4

            const auto bruteForcePairs = collect(bruteForce);
            const auto gridPairs = collect(grid);

            const bool same = (bruteForcePairs == gridPairs);
            // 空振り PASS 防止: そもそもペアが出ていることを確認する
            const bool nonTrivial = !bruteForcePairs.empty();

            Record("U16", "BroadPhase: BruteForce と UniformGrid が同じ候補を返す",
                "同じペア集合（かつ 1 組以上）",
                "BruteForce=" + std::to_string(bruteForcePairs.size())
                    + " 組 / UniformGrid=" + std::to_string(gridPairs.size()) + " 組",
                same && nonTrivial,
                "B-5",
                "セル境界をまたぐ物体は複数セルに入るため重複が出る。除去漏れがあると\n"
                "件数が食い違う。レイヤーマスク・同一オーナー・静止同士の足切り条件も\n"
                "IBroadPhase.h の ShouldTestPair に集約して実装間で共有している。");
        }

        LogEvent("Geometry セルフテスト完了");
    }
}
