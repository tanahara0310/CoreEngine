#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector4.h"
#include "Math/Vector/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "MapChipData.h"

namespace CoreEngine {
    class Camera;
    class GameObject;
    class MsdfFont;
    class Text3DObject;
}

namespace GameComponents {
    class MapGeneratorComponent;
    class ModelRenderPoolComponent;
}

namespace GameComponents
{
    // マップを生成するコンポーネント
    class MapViewComponent final
        : public CoreEngine::IComponent {
    public:
        explicit MapViewComponent(
            MapGeneratorComponent* mapGenerator,
            ModelRenderPoolComponent* groundRenderPool,
            ModelRenderPoolComponent* groundSkirtRenderPool,
            ModelRenderPoolComponent* waterRenderPool,
            ModelRenderPoolComponent* stationRenderPool,
            ModelRenderPoolComponent* rockRenderPool,
            ModelRenderPoolComponent* hardRockRenderPool,
            ModelRenderPoolComponent* bananaTreeRenderPool,
            ModelRenderPoolComponent* grassRenderPool,
            CoreEngine::Camera* viewCamera,
            float gridSize = 1.0f, uint32_t viewDistanceX = 30)
            : gridSize_(gridSize), viewDistanceX_(viewDistanceX),
            mapGenerator_(mapGenerator),
            groundRenderPool_(groundRenderPool),
            groundSkirtRenderPool_(groundSkirtRenderPool),
            waterRenderPool_(waterRenderPool),
            stationRenderPool_(stationRenderPool),
            rockRenderPool_(rockRenderPool),
            hardRockRenderPool_(hardRockRenderPool),
            bananaTreeRenderPool_(bananaTreeRenderPool),
            grassRenderPool_(grassRenderPool),
            viewCamera_(viewCamera) {}

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "MapView";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "マップ描画"; }
        bool DrawInspector() override;
#endif

        // 最初の更新直前に一度だけ呼ばれる
        void Start() override;
        // 毎フレーム呼ばれる
        void Update() override;
        void OnDestroy() override;

        // ビューの中心X座標を設定する
        void SetViewCenterX(uint32_t centerX) { mapViewCenterX_ = centerX; }
        // ビューの表示距離Xを設定する
        void SetViewDistanceX(uint32_t distanceX) { viewDistanceX_ = distanceX; }

        // サルを送り出した駅を1回だけ弾ませる。引数は駅チップのマス座標。
        // ここへ来た駅は「使い終わった駅」として覚え、以降は待機の呼吸を止めて
        // 少し暗く落とす。動いている＝まだ取れる、の区別に使っている
        // （理由は ApplyStationIdle のコメント）。
        void PlayStationPop(int32_t gridX, int32_t gridZ);

        // バナナを収穫された木を、取られた向きへしならせて揺らす。
        // toward はサルが木のどちら側にいるかで、マス座標の差をそのまま渡してよい。
        // 同じ木が続けて取られたら、重ねずに頭から鳴らし直す。
        void PlayBananaTreeShake(int32_t gridX, int32_t gridZ, float towardX, float towardZ);

    private:
        // 再生中の駅の演出
        struct StationPop {
            int32_t gridX = 0;
            int32_t gridZ = 0;
            float elapsed = 0.0f;
        };

        // サルを送り出し終えた駅。止めるだけなら座標だけで足りるが、
        // 色を落とすのに掛かった時間が要るので経過時間も持つ
        struct UsedStation {
            int32_t gridX = 0;
            int32_t gridZ = 0;
            float elapsed = 0.0f;
        };

        // 再生中のバナナの木の演出
        struct BananaTreeShake {
            int32_t gridX = 0;
            int32_t gridZ = 0;
            float towardX = 0.0f; // サルがいる向き。木の +X 側にいるなら 1
            float towardZ = 0.0f;
            float elapsed = 0.0f;
        };

        // 地形と同じ描画範囲で、5mごとの距離目盛りを表示・再利用する。
        void UpdateDistanceMarkers(std::size_t startX, std::size_t endX);
        // 駅の演出を進め、終わったものを捨てる
        void UpdateStationPops(float deltaTime);
        // 指定マスの駅に掛ける拡縮を求める。演出していなければ等倍
        CoreEngine::Vector3 GetStationPopScale(std::size_t x, std::size_t z) const;
        // まだ使っていない駅を呼吸させる。列車が近いほど強く・速くなる。
        // cameraGridX はカメラ（＝ほぼ列車の先頭）のマス座標。段が付くので丸めないこと
        void ApplyStationIdle(std::size_t x, std::size_t z, float cameraGridX,
            CoreEngine::Vector3& scale) const;
        // 列車がどれだけ近いか。0 で遠く、1 で真横
        float GetStationApproach(std::size_t x, float cameraGridX) const;
        // 駅の演出が共通で使う -1..1 の波。位相をずらせば別の拍になる
        float GetStationWave(std::size_t x, std::size_t z,
            float approach, float phaseOffset) const;
        // 駅の屋根で待つサルを1匹描く。roofHeight は呼吸で上下する屋根の高さ、
        // modelScale はマップ共通のモデル拡縮
        void DrawStationMonkey(std::size_t x, std::size_t z, float cameraGridX,
            float roofHeight, float modelScale);
        // 使い終わった駅へ掛ける色。まだ使っていなければ std::nullopt
        std::optional<CoreEngine::Vector4> GetStationTint(
            std::size_t x, std::size_t z) const;
        // 使い終わった駅として覚える。すでに覚えていれば何もしない
        // （同じ駅で2回鳴っても色の落ち方が頭から鳴り直さないように）
        void MarkStationUsed(int32_t gridX, int32_t gridZ);
        // 使い終わっていれば記録を返す。まだなら nullptr
        const UsedStation* FindUsedStation(std::size_t x, std::size_t z) const;
        bool IsStationUsed(std::size_t x, std::size_t z) const;
        // 使い終わった駅の経過時間を進める
        void UpdateUsedStations(float deltaTime);
        // 描画範囲から出た使い終わった駅を捨てる。マップは前へ無限に伸びるので、
        // 覚えっぱなしにすると走った距離ぶん増え続ける
        void ForgetUsedStationsBefore(std::size_t startX);
        // バナナの木の演出を進め、終わったものを捨てる
        void UpdateBananaTreeShakes(float deltaTime);
        // 指定マスのバナナの木のしなりを、渡された回転と拡縮へ反映する。
        // 演出していなければ何もしない
        void ApplyBananaTreeShake(std::size_t x, std::size_t z,
            CoreEngine::Vector3& rotate, CoreEngine::Vector3& scale) const;

        // バナナの木を風で常時ゆらす。渡された回転へ傾きを足すだけなので、
        // 収穫のしなり（ApplyBananaTreeShake）とは加算でそのまま重なる。
        // 木は1メッシュなので、モデル原点＝根元を支点にまっすぐ倒れる。
        // 回転の変位は支点からの距離に比例するため、根元は動かず葉先だけが振れる。
        void ApplyBananaTreeSway(std::size_t x, std::size_t z,
            CoreEngine::Vector3& rotate, CoreEngine::Vector3& scale) const;

        // マスごとの色ムラを求める。地面が一色だとマス目が読めないので、
        // ベースカラーへ掛ける係数をマス単位でわずかに散らす。
        CoreEngine::Vector4 CalcGroundTint(
            std::size_t x, std::size_t z, float cameraDistance) const;

        float gridSize_ = 1.0f;

        // ===== 地面ブロックの伸ばし（スカート） =====
        // ブロックの底面から下へ、逆さにした ground.obj を吊るして柱を伸ばす長さ[m]。
        // 上面は動かさないので、この値を変えても他のオブジェクトの高さは変わらない。
        // 0 にすると従来どおりの1マス角のブロックに戻る。
        // 雲（Game.Fog.*）とセットで決める値。詳しくは BlockModelLayout.h と
        // SkyFogFeature.cpp のコメントを見ること。
        float groundSkirtHeight_ = 4.5f;

        // ===== 地面の色ムラ =====
        // 明度のふり幅（ベースカラーへの乗算）。0 で従来どおりの一色。
        float groundTintStrength_ = 0.20f;
        // 明るいマスは青寄り、暗いマスは黄寄りへずらす量。明度だけだと白黒のムラに見える。
        float groundTintHueSwing_ = 0.12f;
        // この距離[m]を超えたらムラを弱め始める。
        float groundTintFadeStart_ = 16.0f;
        // フェード開始からムラが 0 になるまでの距離[m]。遠景のちらつき対策。
        float groundTintFadeRange_ = 22.0f;

        uint32_t mapViewCenterX_ = 0;
        uint32_t viewDistanceX_ = 30;

        MapGeneratorComponent* mapGenerator_ = nullptr;
        ModelRenderPoolComponent* groundRenderPool_ = nullptr;
        // 地面ブロックの下へ吊るす柱。地面と同じ ground.obj を使うが、1マスにつき
        // 地面とスカートの2つを出すのでプールは分ける
        ModelRenderPoolComponent* groundSkirtRenderPool_ = nullptr;
        ModelRenderPoolComponent* waterRenderPool_ = nullptr;
        ModelRenderPoolComponent* stationRenderPool_ = nullptr;
        ModelRenderPoolComponent* rockRenderPool_ = nullptr;
        // レールを敷けない空白マスへ立てる、壊せない岩
        ModelRenderPoolComponent* hardRockRenderPool_ = nullptr;
        ModelRenderPoolComponent* bananaTreeRenderPool_ = nullptr;
        ModelRenderPoolComponent* grassRenderPool_ = nullptr;
        // 描画範囲はゲーム視点カメラの位置から決める（構図は CameraRig が握る）
        CoreEngine::Camera* viewCamera_ = nullptr;

        CoreEngine::MsdfFont* distanceMarkerFont_ = nullptr;
        std::vector<CoreEngine::Text3DObject*> distanceMarkers_;

        std::vector<StationPop> stationPops_;
        float stationPopDuration_ = 0.45f; // 沈んで跳ね返るまでの時間（秒）
        float stationPopSquash_ = 0.22f;   // 沈み込みの深さ（1.0 で高さが 0 になる）

        // ===== 駅の待機演出（常時） =====
        // 駅は「速度を捨ててサルを買う」決断点なので、何マス手前で気づけるかが
        // そのまま操作の質になる。止まったままだと背景の岩と区別が付かないので、
        // 静かに呼吸させておき、列車が近づくほど強く速くする。
        //
        // ■ 木と同じ「風で傾く」にはしていない
        //   建物が風で揺れると倒れかけて見える。駅の語彙は連結ポップ
        //   （GetStationPopScale）の伸び縮みで既に決まっているので、待機はその弱い版に
        //   して、ポップが「同じ体の大きな反応」として読めるようにしてある。
        //
        // ■ 速さは周波数ではなく2本目の振幅で上げる
        //   sin(t * freq) の freq を動かすと、変えた瞬間に位相が飛んで跳ねる。
        //   遅い波は鳴らしっぱなしにして、速い波を近づくほど混ぜる。
        //   位相が連続なので、どの距離から近づいても継ぎ目が出ない。
        //
        // ■ 近さは距離の絶対値で測る
        //   通り過ぎた駅まで鳴らし続けると「まだ取れる」と嘘をつく。前後対称に
        //   すれば、駅を通らず素通りした（＝使われないので下の使用済みにならない）駅も
        //   離れるにつれて自然に静まる。
        //
        // ■ 見え方の目安（gridSize 1.0・1080p）
        //   駅は高さ 3.0 モデル単位＝1.875m。見下ろし約48度なので縦の変位は画面上で
        //   0.67倍に潰れる。待機 0.02 で屋根が約1.2px、接近 0.07 で約4.3px 動く。
        //   縦だけでは弱いので、横は逆位相に縮めて足元の広がりでも読ませる。
        std::vector<UsedStation> usedStations_;
        float stationIdleBreath_ = 0.02f;  // 待機時の呼吸の振幅（縦の伸び縮み）
        float stationWakeBreath_ = 0.07f;  // 最接近時の呼吸の振幅
        float stationIdleSpeed_ = 0.45f;   // 鳴らしっぱなしにする遅い波の速さ（Hz）
        float stationWakeSpeed_ = 1.5f;    // 近づくほど混ぜる速い波の速さ（Hz）
        float stationWakeRange_ = 7.0f;    // 何マス手前から起き出すか
        // 使い終わった駅へ掛ける明るさ。1.0 で色を変えない。
        // 露出が掛かる画面では明るい側は白へ飽和して差が出ないので、暗い側で付ける。
        float stationUsedTint_ = 0.78f;

        // ===== 駅で待つサル =====
        // 「この駅は何をくれるのか」をモデルで言わせる。プールは GameScene から
        // 渡さず Start() で自前に生やす（距離目盛りの Text3DObject と同じ扱い）。
        //
        // ■ 屋根の上に載せる理由
        //   station.obj は 1 マスをぎっしり埋めていて、地面には正面 0.06m しか余地が無い。
        //   その先はレールのマスで、通るトロッコが 1 マス幅を丸ごと使うため、
        //   地上へ置くとどこに立たせても壁かトロッコへめり込む。屋根の頂点だけが
        //   0.5m 角の平らな面として空いていて、サルなら登っていておかしくない。
        //
        // ■ 屋根は呼吸で上下するので、乗せる高さは毎フレーム今の拡縮から出す
        //   固定の高さで置くと、駅が縮んだときにサルだけ宙に浮く。
        //
        // ■ 拍は駅とずらす
        //   同じ拍で動くと駅とサルが 1 つの塊に見える。跳ねは波の絶対値なので、
        //   屋根へ着地しては跳ね上がる弾みになる（下へは沈まない）。
        ModelRenderPoolComponent* stationMonkeyRenderPool_ = nullptr;
        CoreEngine::GameObject* stationMonkeyPoolObject_ = nullptr;
        // 列車に乗っているサルに対する大きさの比。屋根の面から食み出しすぎない値
        float stationMonkeyScale_ = 0.85f;
        // 最接近時の跳ね上がる高さ［マス］。遠いときはこの 0.35 倍まで落ちる
        float stationMonkeyHop_ = 0.16f;
        // 連結したあとに列車側（-Z）へ飛び降りる距離［マス］
        float stationMonkeyLeap_ = 0.7f;

        std::vector<BananaTreeShake> bananaTreeShakes_;
        float bananaTreeShakeDuration_ = 0.55f; // しなって戻り切るまでの時間（秒）
        float bananaTreeShakeLean_ = 0.17f;     // 取られた向きへ倒れる角度（ラジアン）
        float bananaTreeShakeSquash_ = 0.12f;   // しなりに合わせて縦へ縮む量

        // ===== バナナの木の風揺れ（常時） =====
        // 止まったままの木は死んで見えるので、ゆっくり傾け続けて風の中に立たせる。
        // 位相はマス座標から作るので木ごとにばらけ、プールの要素が別のマスへ
        // 移っても揺れは飛ばない（理由は ApplyBananaTreeSway のコメント）。
        // 角度を上げると根元の底面のフチが地面から浮くので、
        // bananaTreeSinkDepth_ とセットで決めること。
        //
        // ゲームカメラは offset [0, 20, -18] の見下ろし（水平から約48度・距離27m）で、
        // FOV45度・1080p なら画面上は約 49px/m。葉先が 10cm 振れて 5px 動く計算になる。
        // 上から見るぶん傾きは効きにくいので、真上からでも形が変わって見える
        // ヨー（Y回転）と葉の開閉を足して、傾きに頼りきらないようにしている。
        float bananaTreeSwayAngle_ = 0.07f;     // 傾きの振幅（ラジアン。0.07 ≒ 4度）
        float bananaTreeSwaySpeed_ = 0.6f;      // 主となる揺れの速さ（Hz）
        float bananaTreeSwaySubSpeed_ = 0.27f;  // 重ねる2本目の速さ（Hz）
        float bananaTreeSwaySubRate_ = 0.45f;   // 2本目の振幅の割合
        float bananaTreeSwayLean_ = 0.03f;      // 風下へ倒しておく角度（ラジアン）
        // 幹をねじる角度（ラジアン）。葉の茂りが前後左右で非対称なので、回すと
        // 真上から見てもシルエットが動く。傾きと違って根元が全く浮かないため、
        // 埋める深さを気にせず大きく取れる。見下ろし視点ではこれが一番効く。
        float bananaTreeSwayYaw_ = 0.18f;
        // 葉の開閉。横へ広げたぶん少し縦を縮める（厳密な体積保存ではなく、
        // 見た目重視の弱い連動）。見下ろしだと葉の面積の変化として読める。
        float bananaTreeSwayBreath_ = 0.045f;
        // 風向。既定は雲（r.Cloud.WindDirX / WindDirZ）と同じ +X。
        // 雲の風向を変えたらここも合わせると、雲と木が同じ風で動いて見える。
        float bananaTreeWindDirX_ = 1.0f;
        float bananaTreeWindDirZ_ = 0.0f;
        // 木を地面へ沈める深さ[m]（1マス=1m のとき）。傾けると底面のフチが持ち上がって
        // 地面との間に隙間が出るので、その分だけ埋めて隠す。
        // 既定値での浮きは 13mm ほどなので、2.5cm あれば足りる。
        // ヨーと葉の開閉は根元を浮かせないので、ここに効くのは傾きの角度だけ。
        float bananaTreeSinkDepth_ = 0.025f;

    };
}
