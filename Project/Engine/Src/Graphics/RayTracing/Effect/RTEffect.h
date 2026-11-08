#pragma once

#include <cstdint>

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class AccelerationStructureManager;
    class ShaderProgramCache;

    /// @brief 効果を初期化するときに渡す窓口
    struct RTEffectInitContext {
        GraphicsCore* dxCommon = nullptr;
        DescriptorAllocator* descriptorAllocator = nullptr;
        AccelerationStructureManager* asMgr = nullptr;
        ShaderProgramCache* shaderProgramCache = nullptr;
    };

    /// @brief レイトレーシングの効果（影・水面の屈折など）の共通の口
    /// @details RT_EFFECT_REGISTER で申告した型を RTEffectSet が作って持つ。
    ///          効果のパスは RenderContext::rtEffects から型で引いて撃つ（FindRTEffect）。
    class RTEffect {
    public:
        virtual ~RTEffect() = default;

        /// @brief パイプラインと資源を作る（DXR が使えるときだけ呼ばれる）
        /// @return 使える状態になったら true
        virtual bool Initialize(const RTEffectInitContext& init) = 0;

        /// @brief 別スレッドで作っているパイプラインの完成を待つ
        /// @return 使える状態なら true
        virtual bool WaitForPipelines() { return true; }

        /// @brief フレームの頭（加速構造を作る前）に呼ぶ
        virtual void BeginFrame() {}

        /// @brief ウィンドウの大きさが変わったときに呼ぶ（GPU は止まっている）
        virtual void OnResize([[maybe_unused]] uint32_t width, [[maybe_unused]] uint32_t height) {}
    };

    /// @brief 型ごとに 1 つの印（RTEffectSet::Find が型を見分けるのに使う）
    template <class T>
    const void* RTEffectTypeKey()
    {
        static const char key = 0;
        return &key;
    }
}
