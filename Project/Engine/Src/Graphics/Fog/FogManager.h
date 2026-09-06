#pragma once

#include "Graphics/Fog/Settings/FogSettings.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Math/MathCore.h"

#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    class GpuResource;
    class GraphicsCore;
    struct ViewInfo;

    /// @brief フォグ定数の用途別バリアント
    /// @details 同じ数式のまま「何を差すか」で挙動を変えるための 3 本。
    ///          シェーダーには分岐を持たせない（ブレンドモードは描画側しか知らないため）。
    enum class FogVariant : uint8_t {
        Full,     ///< 内散乱 + 減衰。通常の半透明と全画面合成
        Additive, ///< 減衰のみ（内散乱色を 0 にしたもの）。加算・スクリーン合成用
        Disabled, ///< 恒等（密度 0）。全画面パスが既に掛けた不透明フォワード用
        Count
    };

    /// @brief 空色ブレンドに必要な、大気側から受け取る値一式
    /// @details FogManager に AtmosphereManager そのものを持たせないための入れ物。
    ///          組み立ては FogPass の責務（RenderContext から大気を引けるのはパスだけ）。
    struct FogSkyInfo {
        D3D12_GPU_DESCRIPTOR_HANDLE skyViewLutSrv{}; ///< Sky-View LUT の SRV
        float cameraRadiusKm = 0.0f;                 ///< 惑星中心からのカメラ距離 [km]
        float planetRadiusKm = 0.0f;                 ///< 惑星半径 [km]
        bool  valid = false;                         ///< LUT が生成済みで参照してよいか
    };

    /// @brief 高さフォグシステムの窓口
    /// @details 設定・パイプラインを所有し、フレーム状態を定数バッファへ詰めて
    ///          SceneColor へ合成する。数式の実体はシェーダー側
    ///          （Assets/Shaders/Include/Common/Fog.hlsli）にあり、前方描画も同じものを使う。
    /// @note 大気散乱（AerialPerspective）とは効く距離帯が 2 桁以上違う別の媒質なので、
    ///       両方同時に有効でよい。重ねた結果は放射伝達として正しい
    ///       （カメラに近い媒質を後に合成する＝本パスが AerialPerspective より後）。
    class FogManager {
    public:
        /// @brief フォグ合成 CS が使う定数バッファ
        /// @note Assets/Shaders/Include/Common/Fog.hlsli の struct FogParameters と 1 対 1。
        ///       行列は転置せずそのまま入れる（HLSL 側は行ベクトル規約 mul(v, M)）
        struct FogConstants {
            Matrix4x4 invViewProj;      ///< View*Projection の逆行列
            Vector3   cameraWorldPos;   ///< カメラのワールド座標 [m]
            float     density;          ///< heightRef における消散係数 [1/m]
            Vector3   fogColor;         ///< フォグ色（リニア HDR）
            float     heightFalloff;    ///< 高さ方向の減衰率 [1/m]
            float     heightRef;        ///< density を与える基準高度 [m]
            float     startDistance;    ///< フォグが効き始める距離 [m]
            float     maxOpacity;       ///< 最大濃度 [0,1]
            float     skyDistance;      ///< 背景ピクセルのレイ長 [m]
            uint32_t  applyToSky;       ///< 背景にもフォグを掛けるか（HLSL の bool は 4 バイト）
            Vector3   sunDirection;     ///< 太陽光の進行方向（太陽→地表、正規化済み）
            float     sunExponent;      ///< 内散乱ローブの鋭さ
            Vector3   sunTint;          ///< 太陽方向でのフォグの色味（基準色への倍率）
            float     sunGain;          ///< 太陽方向でのフォグの明るさ倍率。太陽が無いフレームは 1
            float     skyColorBlend;    ///< 空色へ寄せる量 [0,1]。LUT が無いフレームは 0
            float     cameraRadiusKm;   ///< Sky-View LUT サンプル用
            float     planetRadiusKm;   ///< Sky-View LUT サンプル用
        };

        static constexpr Cb::Field kFogConstantsFields[] = {
            CB_FIELD(FogConstants, invViewProj),   CB_FIELD(FogConstants, cameraWorldPos),
            CB_FIELD(FogConstants, density),       CB_FIELD(FogConstants, fogColor),
            CB_FIELD(FogConstants, heightFalloff), CB_FIELD(FogConstants, heightRef),
            CB_FIELD(FogConstants, startDistance), CB_FIELD(FogConstants, maxOpacity),
            CB_FIELD(FogConstants, skyDistance),   CB_FIELD(FogConstants, applyToSky),
            CB_FIELD(FogConstants, sunDirection),  CB_FIELD(FogConstants, sunExponent),
            CB_FIELD(FogConstants, sunTint),       CB_FIELD(FogConstants, sunGain),
            CB_FIELD(FogConstants, skyColorBlend), CB_FIELD(FogConstants, cameraRadiusKm),
            CB_FIELD(FogConstants, planetRadiusKm),
        };
        CB_VERIFY_LAYOUT(FogConstants, kFogConstantsFields);
        CB_BIND_HLSL(FogConstants, kFogConstantsFields, "gFog");

    public:
        /// @brief 初期化（合成 CS のコンパイルと PSO 構築）
        /// @param graphicsCore デバイスと UploadRing の取得元
        /// @return 構築に成功したら true（失敗しても IsFogActive() が false になるだけで描画は続く）
        bool Initialize(GraphicsCore* graphicsCore);

        /// @brief フレーム更新（EnvironmentFeature::UpdateFog から毎フレーム呼ばれる）
        /// @details fogActive_ を立て、CVar の現在値を設定へ取り込む。
        ///          Update() を呼ばないシーンではフォグ合成そのものが走らない。
        /// @param sunDirection 太陽光の進行方向（太陽→地表）。正規化されていなくてよい
        /// @param hasSun 太陽ライトが存在し有効か。false なら内散乱を切る
        void Update(const Vector3& sunDirection, bool hasSun);

        /// @brief フレーム終端の後始末（RenderDomainContext が全 View 描画後に呼ぶ）
        void EndFrame()
        {
            fogActive_ = false;
            // 次フレームで PrepareConstants が走るまで、前方描画には恒等バッファを返す
            for (auto& address : frameConstants_) { address = 0; }
        }

        /// @brief 今フレームの定数バッファ 3 本を確保する
        /// @details 前方描画（半透明・水面）が読むので、フォグが無効なフレームでも必ず呼ぶこと。
        ///          無効なフレームは 3 本とも恒等（密度 0）になる。
        void PrepareConstants(const ViewInfo& view, const FogSkyInfo& sky);

        /// @brief このフレームでフォグが要求されているか（Update() が呼ばれ、かつ有効かつ構築済み）
        bool IsFogActive() const { return fogActive_ && settings_.enabled && pipelineReady_; }

        /// @brief 合成パイプラインの構築に成功しているか
        /// @details false のときは r.Fog.Enabled を立ててもフォグは出ない（シェーダーのコンパイル失敗）。
        ///          エディタが原因を切り分けるために公開している
        bool IsPipelineReady() const { return pipelineReady_; }

        /// @brief 現在の設定（実体は FogCVars。Update が毎フレーム取り込む）
        /// @note 変更するときは FogCVars 側を Set すること（ここへ書いても次の Update で戻る）
        const FogSettings& GetSettings() const { return settings_; }

        /// @brief SceneColor へフォグを in-place 合成する
        /// @param cmdList         記録先コマンドリスト
        /// @param sceneColor      SceneColor（ステート追跡込み）
        /// @param sceneColorUav   SceneColor の UAV ハンドル
        /// @param sceneDepthSrv   SceneDepth の SRV ハンドル
        /// @param view            描画に使われたビュー（深度復元の行列はここから取る）
        /// @param sky             空色ブレンド用の大気情報（valid == false ならブレンドしない）
        void ApplyFog(
            ID3D12GraphicsCommandList* cmdList,
            GpuResource& sceneColor,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneColorUav,
            D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
            const ViewInfo& view,
            const FogSkyInfo& sky);

        /// @brief 今フレームのフォグ定数バッファの GPU 仮想アドレス
        /// @details 半透明・水面・パーティクルが Fog.hlsli の ApplyFog を呼ぶときに差す。
        ///          全画面合成と同じ実体を読むので数式がずれない。
        /// @note 今フレームの PrepareConstants() が未実行なら、常設の恒等バッファを返す。
        ///       戻り値は必ず有効なので、呼び出し側は 0 チェックなしで差してよい
        D3D12_GPU_VIRTUAL_ADDRESS GetConstantsGpuAddress(
            FogVariant variant = FogVariant::Full) const
        {
            const D3D12_GPU_VIRTUAL_ADDRESS address = frameConstants_[static_cast<size_t>(variant)];
            return address != 0 ? address : disabledConstantsAddress_;
        }

    private:
        /// @brief 設定・ビュー・大気情報から今フレームの定数バッファ内容を作る
        FogConstants BuildConstants(const ViewInfo& view, const FogSkyInfo& sky) const;

        GraphicsCore* graphicsCore_ = nullptr;

        CustomShaderPipeline pipeline_{};
        BindingTable bindings_{};
        bool pipelineReady_ = false;

        FogSettings settings_{};

        /// @brief 今フレームの太陽（Update が毎フレーム外から受け取る値）
        Vector3 sunDirection_{ 0.0f, -1.0f, 0.0f };
        bool hasSun_ = false;

        /// @brief このフレームで Update() が呼ばれフォグが要求されたか
        bool fogActive_ = false;

        /// @brief 今フレームぶんのフォグ定数（UploadRing 上）。フレームを跨いで持ち越さない
        D3D12_GPU_VIRTUAL_ADDRESS frameConstants_[static_cast<size_t>(FogVariant::Count)]{};

        /// @brief 常設の「何もしない」フォグ定数。PrepareConstants 未実行のビュー・フレームで差す
        /// @details ここが無いと、補助ビューの前方描画が未バインドの CBV を読むことになる
        Microsoft::WRL::ComPtr<ID3D12Resource> disabledConstantBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS disabledConstantsAddress_ = 0;
    };
}
