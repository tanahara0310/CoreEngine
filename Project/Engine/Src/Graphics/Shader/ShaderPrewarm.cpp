#include "pch.h"
#include "ShaderPrewarm.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include "ShaderCompiler.h"
#include "Cache/ShaderBlobCache.h"
#include "Cache/ShaderManifest.h"
#include "EngineSystem/Startup/StartupProgress.h"
#include "Threading/ThreadBudget.h"
#include "Threading/ThreadPool.h"
#include "Utility/Logger/Logger.h"

namespace
{
    /// @brief スレッドごとの ShaderCompiler
    /// @details IDxcCompiler3 はスレッドセーフとして文書化されておらず、
    ///          RecordingIncludeHandler も「直前に開いた include」という状態を持つため、
    ///          共有すると依存マニフェスト（.deps）が別シェーダのものと混ざる。
    CoreEngine::ShaderCompiler& GetThreadLocalCompiler()
    {
        thread_local std::unique_ptr<CoreEngine::ShaderCompiler> compiler;
        if (!compiler) {
            compiler = std::make_unique<CoreEngine::ShaderCompiler>();
            compiler->Initialize();
        }
        return *compiler;
    }
}

namespace CoreEngine::ShaderPrewarm
{
    Result Run()
    {
        Result result;

        const std::vector<ShaderManifest::Entry> entries = ShaderManifest::GetInstance().Load();
        if (entries.empty()) {
            // 初回起動、または一覧が無効。従来どおり各 PSO 生成コードが直列にコンパイルする
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Shader,
                "[ShaderPrewarm] 記録された一覧が無いため事前コンパイルを省略します"
                "（この起動の記録が次回から使われます）");
            return result;
        }

        result.executed = true;
        result.total = static_cast<uint32_t>(entries.size());

        // 事前コンパイルの結果を本来の PSO 生成コードへ渡すのはメモリキャッシュの役目。
        // ここを有効化しないと、同じ検証コストを 2 回払うだけで速くならない
        ShaderBlobCache::GetInstance().SetEnabled(true);

        // ===== パス解決はメインスレッドで済ませる =====
        // AssetDatabase::FindAssetPath は unordered_map::operator[] で挿入するため
        // 並列に呼ぶとレースする。ワーカーへ渡すのは解決済みの要求だけにする
        ShaderCompiler mainThreadCompiler;
        mainThreadCompiler.Initialize();

        std::vector<PreparedShaderCompile> prepared;
        prepared.reserve(entries.size());
        uint32_t missing = 0;
        for (const ShaderManifest::Entry& entry : entries) {
            const wchar_t* entryPoint = entry.entryPoint.empty() ? nullptr : entry.entryPoint.c_str();
            PreparedShaderCompile request =
                mainThreadCompiler.Prepare(entry.filePath, entry.profile.c_str(), entryPoint);
            if (!request.IsValid()) {
                continue;
            }

            // 一覧は前回の実行の記録なので、シェーダを消した／改名した／
            // プロジェクトを移動した後は解決できない項目が残る。
            // 事前コンパイルは最適化に過ぎないので、黙って飛ばす
            //（本当に必要なら本来の PSO 生成コードが要求した時点でエラーになる）
            std::error_code errorCode;
            if (!std::filesystem::exists(request.resolvedPath, errorCode)) {
                ++missing;
                continue;
            }
            prepared.push_back(std::move(request));
        }

        if (missing > 0) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Shader,
                "[ShaderPrewarm] 一覧のうち {} 件は現在解決できないため飛ばしました"
                "（消された／改名されたシェーダ。一覧はこの起動の内容で更新されます）",
                missing);
        }

        if (prepared.empty()) {
            return result;
        }

        // ===== 並列コンパイル =====
        // このプールは事前コンパイルの間だけ生き、終わったら枠を返す。
        // 残り枠を全部要求するのは、この工程が起動をブロックするから
        ThreadPoolDesc poolDesc;
        poolDesc.name = "ShaderCompile";
        poolDesc.threadCount = ThreadBudget::GetCapacity();
        poolDesc.priority = WorkerPriority::Normal;

        const auto startedAt = std::chrono::steady_clock::now();

        std::atomic<uint32_t> succeeded{ 0 };
        std::atomic<uint32_t> failed{ 0 };

        {
            ThreadPool pool(poolDesc);
            result.workerCount = pool.GetThreadCount();

            std::vector<std::future<void>> futures;
            futures.reserve(prepared.size());

            for (const PreparedShaderCompile& request : prepared) {
                const std::string label = "Shader: " +
                    Logger::GetInstance().PathToUtf8(
                        std::filesystem::path(request.resolvedPath).filename()) +
                    " (" + Logger::GetInstance().WideToUtf8(request.profile) + ")";

                futures.push_back(pool.Submit(label, [&request, &succeeded, &failed]() {
                    // 失敗しても事前コンパイルは最適化に過ぎない。
                    // 本来の PSO 生成コードが同じシェーダを要求したときに
                    // 通常経路でコンパイルされるので、ここでは記録して先へ進む
                    try {
                        IDxcBlob* blob = GetThreadLocalCompiler().CompilePrepared(request);
                        if (blob) {
                            // 欲しいのはキャッシュへの副作用だけなので、参照は即座に返す
                            blob->Release();
                            succeeded.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            failed.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    catch (...) {
                        failed.fetch_add(1, std::memory_order_relaxed);
                    }
                    }));
            }

            // ===== 合流 =====
            // メインスレッドは待つ代わりにタスクを引き受ける。手伝えるものが無い間だけ
            // 短く待って StartupProgress を叩く（再描画とメッセージ処理はメインスレッドしかできない）。
            size_t completed = 0;
            for (auto& future : futures) {
                while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    if (pool.TryRunOnePendingTask()) {
                        continue;
                    }
                    future.wait_for(std::chrono::milliseconds(30));
                    StartupProgress::Tick(nullptr);
                }
                ++completed;
                if ((completed % 8) == 0) {
                    StartupProgress::Tick(nullptr);
                }
            }

            const auto stats = pool.GetStats();
            result.workerBusyMs = stats.totalBusyMs;
        }   // ここでプールが破棄され、ThreadBudget へ枠が返る

        result.wallMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - startedAt).count();
        result.succeeded = succeeded.load();
        result.failed = failed.load();
        result.speedup = result.wallMs > 0.0 ? result.workerBusyMs / result.wallMs : 0.0;

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Shader,
            "[ShaderPrewarm] {} 件を {} ワーカーで事前コンパイル: "
            "成功 {} / 失敗 {}、壁時計 {:.0f}ms、ワーカー実行合計 {:.0f}ms（直列比 {:.1f}x）",
            result.total, result.workerCount, result.succeeded, result.failed,
            result.wallMs, result.workerBusyMs, result.speedup);

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Shader,
            "[ShaderPrewarm] メモリキャッシュ {} 件 / {:.2f} MB を保持（起動完了時に解放）",
            ShaderBlobCache::GetInstance().GetCount(),
            static_cast<double>(ShaderBlobCache::GetInstance().GetTotalBytes()) / (1024.0 * 1024.0));

        return result;
    }
}
