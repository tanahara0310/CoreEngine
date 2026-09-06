#include "pch.h"
#include "DefaultSceneFeatures.h"

#include "CameraFeature.h"
#include "CameraRigFeature.h"
#include "CameraSequenceFeature.h"
#include "CameraShakeFeature.h"
#include "CollisionFeature.h"
#include "DebugEditorFeature.h"
#include "EnvironmentFeature.h"
#include "EventDispatchFeature.h"
#include "GridFeature.h"
#include "GroundFeature.h"
#include "LightingFeature.h"
#include "TweenFeature.h"

namespace CoreEngine
{
    namespace {
        /// @brief 既定 priority（0）で末尾へ積む
        template<typename T>
        void Add(std::vector<DefaultSceneFeature>& out)
        {
            out.push_back({ std::make_unique<T>(), 0 });
        }

        /// @brief フェーズ内で最初に回す Feature として末尾へ積む
        template<typename T>
        void AddEarly(std::vector<DefaultSceneFeature>& out)
        {
            out.push_back({ std::make_unique<T>(), kEarlyFeaturePriority });
        }

        /// @brief フェーズ内で最後に回す Feature として末尾へ積む
        template<typename T>
        void AddLate(std::vector<DefaultSceneFeature>& out)
        {
            out.push_back({ std::make_unique<T>(), kLateFeaturePriority });
        }
    }

    std::vector<DefaultSceneFeature> CreateDefaultSceneFeatures()
    {
        std::vector<DefaultSceneFeature> features;

        // 並び順 = 同 priority 内の実行順。従来 BaseScene::Update に
        // 暗黙の順序として埋まっていた並びをそのまま再現している。

        // カメラは最優先。以降の Feature（ライト/影・床の追従・大気散乱）はいずれも
        // 「今フレームのカメラ姿勢が確定済み」であることを前提にしている。
        // 先頭に置くことで、解放時（逆順）には最後に回り、他の Feature が
        // 解放中も SceneContext::cameraManager を参照できる。
        AddEarly<CameraFeature>(features);

        Add<LightingFeature>(features);

#ifdef USE_IMGUI
        Add<GridFeature>(features);
        Add<DebugEditorFeature>(features);
#endif

        Add<CollisionFeature>(features);
        Add<EnvironmentFeature>(features);

        // 既定の床。空（Environment）と対になる「必ずある地面」で、
        // 生成はシーンの OnInitialize 完了後（PostSceneInitialize）に行われる
        Add<GroundFeature>(features);

        // ここから下は「そのフェーズの全 Feature が終わってから 1 回だけ」動く。
        // 破棄側（PostSceneFinalize）は登録の逆順で呼ばれるため、
        // EventDispatch → Tween の順に畳まれる。
        AddLate<TweenFeature>(features);          // PreObjectUpdate の最後
        AddLate<EventDispatchFeature>(features);  // PostObjectUpdate の最後

        // ここから下は 1 つのカメラを「ゲーム側の追従 → リグ → シーケンス → 揺れ」の
        // 順に重ね書きする。いずれも PostLogic の最後（＝追従が構図を決め切った後）に
        // 回り、この 3 行の並びがそのまま重ね順になる。入れ替えてはいけない。

        // カメラリグが一番下。追従・注視・視野角をシーンの状態から決め直す層で、
        // 動かしている間はゲーム側の追従コンポーネントを置き換える。
        AddLate<CameraRigFeature>(features);      // PostLogic の最後（シーケンスより先）

        // カメラシーケンスはリグの上。エディタで作ったカット割りで構図を差し替える。
        AddLate<CameraSequenceFeature>(features); // PostLogic の最後（シェイクより先）

        // カメラシェイクは最後。シーケンスが決めた構図の上に揺れを乗せる。
        // 同フェーズの大気・雲より後に回るので、それらは揺れる前の姿勢を見る。
        AddLate<CameraShakeFeature>(features);    // PostLogic の最後

        return features;
    }
}
