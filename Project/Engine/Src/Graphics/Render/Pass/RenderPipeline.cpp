#include "RenderPipeline.h"

namespace CoreEngine
{
    void RenderPipeline::AddPass(std::unique_ptr<RenderPass> pass)
    {
        if (pass) {
            passes_.push_back(std::move(pass));
        }
    }

    RenderPass* RenderPipeline::GetPass(const std::string& name)
    {
        for (auto& pass : passes_) {
            if (pass->GetName() == name) {
                return pass.get();
            }
        }
        return nullptr;
    }

    void RenderPipeline::Execute(const RenderContext& context)
    {
        for (auto& pass : passes_) {
            if (!pass->IsEnabled()) {
                continue;
            }

            // パスのセットアップ
            pass->Setup(context);

            // パスの実行
            pass->Execute(context);

            // パスのクリーンアップ
            pass->Cleanup(context);
        }
    }

    void RenderPipeline::Clear()
    {
        passes_.clear();
    }
}
