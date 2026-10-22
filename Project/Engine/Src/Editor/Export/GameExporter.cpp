#include "pch.h"
#include "Editor/Export/GameExporter.h"

#ifdef CORE_EDITOR

#include "Editor/Export/MeshCooker.h"
#include "Editor/Export/ShaderCooker.h"
#include "Editor/Export/TextureCooker.h"
#include "EngineSystem/EngineVersion.h"
#include "EngineSystem/Startup/SplashScreen.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Model/Import/MeshImporter.h"
#include "Graphics/Texture/Import/TextureImporter.h"
#include "Threading/ThreadBudget.h"
#include "Threading/ThreadPool.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/CookedPath.h"
#include "Utility/Path/ProjectPaths.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <set>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief 写すファイル 1 つ
        struct CopyItem
        {
            std::filesystem::path from;
            std::filesystem::path to;
        };

        /// @brief 丸ごと写すフォルダ 1 つ
        struct FolderItem
        {
            std::filesystem::path from;
            std::filesystem::path to;
        };

        /// @brief path を表示用の UTF-8 にする
        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief 動いている exe のパス
        std::filesystem::path RunningExe()
        {
            wchar_t path[MAX_PATH] = {};
            ::GetModuleFileNameW(nullptr, path, MAX_PATH);
            return std::filesystem::path(path);
        }

        /// @brief ファイルを最後に書いた日時を "2026-09-26 13:25" にする（取れなければ空）
        std::string FormatWriteTime(const std::filesystem::path& path)
        {
            std::error_code ec;
            const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, ec);
            if (ec) {
                return {};
            }
            using namespace std::chrono;
            const zoned_time local{ current_zone(), floor<minutes>(clock_cast<system_clock>(written)) };
            return std::format("{:%Y-%m-%d %H:%M}", local);
        }

        /// @brief 書き出しの仕事（変換か写し）1 つの結果
        struct JobOutcome
        {
            std::string error;
            int files = 0;
            std::uintmax_t bytes = 0;
            std::vector<CookRecord> records;
        };

        using ExportJob = std::function<void(JobOutcome&)>;

        /// @brief 仕事をスレッドプールで並べて行い、結果を仕事の順に返す（1 つ終わるたびに done を進める）
        std::vector<JobOutcome> RunJobs(ThreadPool& pool, const std::vector<ExportJob>& jobs, std::atomic<int>& done)
        {
            std::vector<JobOutcome> outcomes(jobs.size());
            std::vector<std::future<void>> futures;
            futures.reserve(jobs.size());
            for (size_t i = 0; i < jobs.size(); ++i) {
                futures.push_back(pool.Submit("Export", [&jobs, &outcomes, &done, i] {
                    try {
                        jobs[i](outcomes[i]);
                    }
                    catch (const std::exception& e) {
                        outcomes[i].error = e.what();
                    }
                    ++done;
                }));
            }
            for (std::future<void>& future : futures) {
                pool.Wait(future);
            }
            return outcomes;
        }

        /// @brief パスを照合用のキーにする（正規化して小文字）
        std::wstring MakeFileKey(const std::filesystem::path& path)
        {
            std::wstring key = path.lexically_normal().generic_wstring();
            for (wchar_t& c : key) {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            return key;
        }

        /// @brief .cmesh にするので写さないファイル（モデルのファイルと、OBJ の mtllib・glTF の外部バッファ）のキー
        std::set<std::wstring> CollectModelFiles(const std::vector<std::filesystem::path>& meshes)
        {
            Logger& log = Logger::GetInstance();
            std::set<std::wstring> files;
            for (const std::filesystem::path& mesh : meshes) {
                files.insert(MakeFileKey(mesh));
                const std::filesystem::path folder = mesh.parent_path();
                const std::wstring extension = MakeFileKey(mesh.extension());
                if (extension == L".obj") {
                    std::ifstream obj(mesh);
                    std::string line;
                    while (std::getline(obj, line)) {
                        if (line.rfind("mtllib ", 0) != 0) {
                            continue;
                        }
                        std::string name = line.substr(7);
                        while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) {
                            name.pop_back();
                        }
                        if (name.empty()) {
                            continue;
                        }
                        // 指定の .mtl が無ければ、Assimp と同じく OBJ と同じ名前の .mtl を読んだものとする
                        std::filesystem::path material = folder / log.Utf8ToPath(name);
                        std::error_code ec;
                        if (!std::filesystem::is_regular_file(material, ec)) {
                            material = std::filesystem::path(mesh).replace_extension(".mtl");
                        }
                        files.insert(MakeFileKey(material));
                    }
                } else if (extension == L".gltf") {
                    const json gltf = JsonManager::GetInstance().LoadJson(ToUtf8(mesh));
                    if (gltf.is_object() && gltf.contains("buffers") && gltf["buffers"].is_array()) {
                        for (const json& buffer : gltf["buffers"]) {
                            if (buffer.is_object() && buffer.contains("uri") && buffer["uri"].is_string()) {
                                const std::string& uri = buffer["uri"].get_ref<const std::string&>();
                                if (!uri.starts_with("data:")) {
                                    files.insert(MakeFileKey(folder / log.Utf8ToPath(uri)));
                                }
                            }
                        }
                    }
                }
            }
            return files;
        }

        /// @brief モデルのファイルか（.cmesh にして写さない拡張子か）
        bool IsModelFile(const std::filesystem::path& path)
        {
            const std::wstring extension = MakeFileKey(path.extension());
            return extension == L".gltf" || extension == L".glb" || extension == L".obj" || extension == L".fbx";
        }

        /// @brief 中身が JSON のファイルか（書き出しで MessagePack にする拡張子か）
        bool IsJsonData(const std::filesystem::path& path)
        {
            const std::wstring extension = MakeFileKey(path.extension());
            return extension == L".json" || extension == L".prefab" || extension == L".mat" ||
                extension == L".meta" || extension == L".physmat";
        }

        /// @brief 書き出した物を確かめる（変換した物が全部あるか、写さないはずの元のファイルが残っていないか）
        /// @param keptImage 元のまま写す画像（ローディング画面の画像。無ければ空）
        /// @return 見つかったこと（無ければ空）
        std::vector<std::string> VerifyExport(const std::filesystem::path& destination,
            const std::vector<CookRecord>& records, const std::filesystem::path& keptImage)
        {
            Logger& log = Logger::GetInstance();
            std::vector<std::string> warnings;
            std::error_code ec;
            for (const CookRecord& record : records) {
                if (!std::filesystem::is_regular_file(destination / log.Utf8ToPath(record.derived), ec)) {
                    warnings.push_back(record.derived + " がありません");
                }
            }
            const std::wstring keptKey = keptImage.empty() ? std::wstring() : MakeFileKey(keptImage);
            std::string bytes;
            for (const char* folder : { "Engine/Assets", "Application/Assets", "Application/Config" }) {
                std::filesystem::recursive_directory_iterator it(destination / folder, ec);
                for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                    const std::filesystem::path& file = it->path();
                    if (!it->is_regular_file(ec)) {
                        continue;
                    }
                    if ((CookedTexture::IsCookable(file) && MakeFileKey(file) != keptKey) || IsModelFile(file)) {
                        warnings.push_back("元のファイルが残っています: " + ToUtf8(file.lexically_relative(destination)));
                    } else if (IsJsonData(file) && (!JsonManager::ReadBytes(file, bytes) || !JsonManager::IsBinary(bytes))) {
                        warnings.push_back("テキストの JSON が残っています: " + ToUtf8(file.lexically_relative(destination)));
                    }
                }
            }
            return warnings;
        }

        /// 消せない・書けないときに添える一言
        constexpr const char* kLockedHint = "（書き出したゲームが動いていないか確かめてください）";

        /// @brief JSON のファイルを MessagePack に変換して、同じ名前で書く
        /// @return 書けなかった訳（書けたら空）
        std::string ConvertJson(const CopyItem& item, std::uintmax_t& writtenBytes)
        {
            const std::filesystem::path relative = ProjectPaths::MakeRelative(item.from).generic_wstring();
            const std::string name = ToUtf8(relative.empty() ? item.from : relative);
            std::string text;
            if (!JsonManager::ReadBytes(item.from, text)) {
                return name + " を読めませんでした";
            }
            const json data = JsonManager::Parse(text);
            if (data.is_discarded()) {
                return name + " を JSON として読めません";
            }

            std::error_code ec;
            std::filesystem::create_directories(item.to.parent_path(), ec);
            const std::vector<std::uint8_t> binary = JsonManager::ToBinary(data);
            std::ofstream file(item.to, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
            if (!file) {
                return ToUtf8(item.to) + " を書けませんでした" + kLockedHint;
            }
            writtenBytes = binary.size();
            return {};
        }

        /// @brief クックの一覧（書き出したゲームの根の CookManifest.json）を書く
        json ToJson(const std::vector<CookRecord>& records)
        {
            json entries = json::array();
            for (const CookRecord& record : records) {
                entries.push_back({
                    { "source", record.source },
                    { "guid", record.guid },
                    { "derived", record.derived },
                    { "settings", record.settings },
                    { "key", record.key },
                });
            }
            return entries;
        }

        bool WriteCookManifest(const std::filesystem::path& destination, const std::vector<CookRecord>& textures,
            const std::vector<CookRecord>& meshes, int shaderCount, int jsonCount)
        {
            using namespace std::chrono;
            const zoned_time now{ current_zone(), floor<seconds>(system_clock::now()) };
            const json manifest = {
                { "engineVersion", kEngineVersion },
                { "cookedAt", std::format("{:%Y-%m-%d %H:%M:%S}", now) },
                { "importers", { { "Texture", TextureImporter::kVersion }, { "Mesh", MeshImporter::kVersion } } },
                { "textures", ToJson(textures) },
                { "meshes", ToJson(meshes) },
                { "shaders", shaderCount },
                { "json", jsonCount },
            };
            return JsonManager::GetInstance().SaveJson(ToUtf8(destination / CookedPath::kManifestFileName), manifest);
        }
    }

    ExportPlan GameExporter::Plan()
    {
        ExportPlan plan;
        plan.destination = ProjectPaths::ProjectRoot() / "Build" / "Windows";

        // Release の exe は、動いている exe の出力先と並んだ Release のフォルダにある
        const std::filesystem::path running = RunningExe();
        plan.releaseExe = running.parent_path().parent_path() / "Release" / running.filename();
        std::error_code ec;
        if (!std::filesystem::is_regular_file(plan.releaseExe, ec)) {
            plan.error = "先に Release でビルドしてください（" + ToUtf8(plan.releaseExe) + " がありません）";
            return plan;
        }
        plan.releaseBuiltAt = FormatWriteTime(plan.releaseExe);
        return plan;
    }

    ExportResult GameExporter::Export(const ExportPlan& plan, ExportProgress& progress)
    {
        ExportResult result;
        if (!plan.error.empty()) {
            result.error = plan.error;
            return result;
        }

        const std::filesystem::path& destination = plan.destination;
        const FolderItem folders[] = {
            { ProjectPaths::EngineRoot() / "Engine" / "Assets", destination / "Engine" / "Assets" },
            { ProjectPaths::ProjectRoot() / "Application" / "Assets", destination / "Application" / "Assets" },
            { ProjectPaths::ProjectRoot() / "Application" / "Config", destination / "Application" / "Config" },
        };
        const std::filesystem::path cookedFolders[] = {
            destination / "Engine" / "Cooked",
            destination / "Application" / "Cooked",
        };

        // 写すファイルを先に集める（Release のフォルダの exe と DLL、各フォルダの中身）。
        // 画像のテクスチャは写さずに DDS へ変換する
        std::vector<CopyItem> items;
        std::vector<std::filesystem::path> textures;
        std::error_code ec;
        const std::filesystem::path releaseDirectory = plan.releaseExe.parent_path();
        for (const auto& entry : std::filesystem::directory_iterator(releaseDirectory, ec)) {
            const std::filesystem::path extension = entry.path().extension();
            if (entry.is_regular_file(ec) && (extension == ".exe" || extension == ".dll")) {
                items.push_back({ entry.path(), destination / entry.path().filename() });
            }
        }
        if (ec) {
            result.error = ToUtf8(releaseDirectory) + " を読めませんでした";
            return result;
        }
        // エディタだけで読むスクリプトのフォルダ
        const std::filesystem::path editorScripts = ProjectPaths::ProjectRoot() / "Application" / "Assets" / "Scripts" / "Editor";
        for (const FolderItem& folder : folders) {
            std::filesystem::recursive_directory_iterator it(folder.from, ec);
            for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (it->is_directory(ec) && it->path() == editorScripts) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file(ec)) {
                    continue;
                }
                if (CookedTexture::IsCookable(it->path())) {
                    textures.push_back(it->path());
                } else {
                    items.push_back({ it->path(), folder.to / it->path().lexically_relative(folder.from) });
                }
            }
            if (ec) {
                result.error = ToUtf8(folder.from) + " を読めませんでした";
                return result;
            }
        }
        // ローディング画面の画像は D3D12 より前に WIC で読むので、DDS にした物とは別に元のまま写す
        std::filesystem::path splashCopy;
        if (const std::filesystem::path splashImage = SplashScreen::ResolveImagePath(); !splashImage.empty()) {
            if (const std::filesystem::path relative = ProjectPaths::MakeRelative(splashImage); !relative.empty()) {
                splashCopy = destination / relative;
                items.push_back({ splashImage, splashCopy });
            }
        }
        const std::vector<std::filesystem::path> meshes = MeshCooker::PlanTargets();
        const std::vector<ShaderManifest::Entry> shaders = ShaderCooker::PlanTargets();

        // .cmesh にするモデルのファイル（と、それが使う .mtl・外部バッファ）は写さない。
        // 写しは並べて行うので、同じ先へ写すものは 1 つにする
        const std::set<std::wstring> modelFiles = CollectModelFiles(meshes);
        std::set<std::wstring> destinations;
        std::erase_if(items, [&modelFiles, &destinations](const CopyItem& item) {
            return modelFiles.contains(MakeFileKey(item.from)) || !destinations.insert(MakeFileKey(item.to)).second;
        });

        // 中身が JSON のファイル（シーン・プレハブ・マテリアル・.meta・設定など）は、写さずに MessagePack へ変換する
        const auto jsonBegin = std::stable_partition(items.begin(), items.end(),
            [](const CopyItem& item) { return !IsJsonData(item.from); });
        const std::vector<CopyItem> jsonItems(std::make_move_iterator(jsonBegin), std::make_move_iterator(items.end()));
        items.erase(jsonBegin, items.end());

        progress.cookTotal = static_cast<int>(meshes.size() + textures.size() + shaders.size() + jsonItems.size());
        progress.total = static_cast<int>(items.size());

        // 丸ごと写すフォルダとクック済みのデータのフォルダは、書き出す先の側を消しておく
        std::vector<std::filesystem::path> clearFolders(std::begin(cookedFolders), std::end(cookedFolders));
        for (const FolderItem& folder : folders) {
            clearFolders.push_back(folder.to);
        }
        for (const std::filesystem::path& folder : clearFolders) {
            std::filesystem::remove_all(folder, ec);
            if (ec) {
                result.error = ToUtf8(folder) + " を消せませんでした" + kLockedHint;
                return result;
            }
        }
        const std::filesystem::path manifestPath = destination / CookedPath::kManifestFileName;
        std::filesystem::remove(manifestPath, ec);
        if (ec) {
            result.error = ToUtf8(manifestPath) + " を消せませんでした" + kLockedHint;
            return result;
        }

        // 画像は WIC で読むので、書き出しの間は COM のマルチスレッドの区画を保つ（プールのワーカーもこの区画で COM を使う）
        struct ComScope
        {
            HRESULT hr;
            ~ComScope() { if (SUCCEEDED(hr)) { ::CoUninitialize(); } }
        } com{ ::CoInitializeEx(nullptr, COINIT_MULTITHREADED) };

        // 変換と写しは一時的なスレッドプールで並べて行う（ほかのプールの枠を借りる）
        ThreadPoolDesc poolDesc;
        poolDesc.name = "Export";
        poolDesc.threadCount = ThreadBudget::GetCapacity();
        poolDesc.borrowBudget = true;
        ThreadPool pool(poolDesc);

        // モデルを先に .cmesh にし、テクスチャの使い道はその .cmesh のマテリアルから決める
        std::vector<ExportJob> meshJobs;
        meshJobs.reserve(meshes.size());
        for (const std::filesystem::path& mesh : meshes) {
            meshJobs.push_back([&mesh, &destination](JobOutcome& outcome) {
                outcome.error = MeshCooker::Cook(mesh, destination, outcome.files, outcome.bytes, outcome.records);
            });
        }
        std::vector<JobOutcome> meshOutcomes = RunJobs(pool, meshJobs, progress.cooked);

        const std::vector<CookTarget> targets = TextureCooker::PlanTargets(textures);
        progress.cookTotal = static_cast<int>(meshes.size() + targets.size() + shaders.size() + jsonItems.size());

        // テクスチャ・シェーダ・JSON の変換をまとめて並べる（結果を数えるために仕事の種類を控える）
        enum class CookKind { Texture, Shader, Json };
        std::vector<ExportJob> otherJobs;
        std::vector<CookKind> otherKinds;
        otherJobs.reserve(targets.size() + shaders.size() + jsonItems.size());
        otherKinds.reserve(otherJobs.capacity());
        for (const CookTarget& target : targets) {
            otherJobs.push_back([&target, &destination](JobOutcome& outcome) {
                outcome.error = TextureCooker::Cook(target, destination, outcome.files, outcome.bytes, outcome.records);
            });
            otherKinds.push_back(CookKind::Texture);
        }
        for (const ShaderManifest::Entry& shader : shaders) {
            otherJobs.push_back([&shader, &destination](JobOutcome& outcome) {
                outcome.error = ShaderCooker::Cook(shader, destination, outcome.files, outcome.bytes);
            });
            otherKinds.push_back(CookKind::Shader);
        }
        for (const CopyItem& item : jsonItems) {
            otherJobs.push_back([&item](JobOutcome& outcome) {
                outcome.error = ConvertJson(item, outcome.bytes);
                outcome.files = outcome.error.empty() ? 1 : 0;
            });
            otherKinds.push_back(CookKind::Json);
        }
        std::vector<JobOutcome> otherOutcomes = RunJobs(pool, otherJobs, progress.cooked);

        // 結果を仕事の順にまとめる。失敗は止めずに全部集める
        const auto accept = [&result](JobOutcome& outcome) {
            if (!outcome.error.empty()) {
                result.failures.push_back(std::move(outcome.error));
                return false;
            }
            result.fileCount += outcome.files;
            result.bytes += outcome.bytes;
            return true;
        };
        std::vector<CookRecord> meshRecords;
        for (JobOutcome& outcome : meshOutcomes) {
            if (accept(outcome)) {
                ++result.cookedModels;
                meshRecords.insert(meshRecords.end(), outcome.records.begin(), outcome.records.end());
            }
        }
        std::vector<CookRecord> cookRecords;
        for (size_t i = 0; i < otherOutcomes.size(); ++i) {
            JobOutcome& outcome = otherOutcomes[i];
            const int files = outcome.files;
            if (!accept(outcome)) {
                continue;
            }
            switch (otherKinds[i]) {
            case CookKind::Texture:
                ++result.cookedTextures;
                cookRecords.insert(cookRecords.end(), outcome.records.begin(), outcome.records.end());
                break;
            case CookKind::Shader:
                result.cookedShaders += (files > 0) ? 1 : 0;
                break;
            case CookKind::Json:
                ++result.convertedJson;
                break;
            }
        }
        if (!result.failures.empty()) {
            result.error = std::format("{} 件を変換できなかったので、ファイルを写す前に書き出しを止めました。"
                "直してから書き出し直してください", result.failures.size());
            return result;
        }

        // 写す先のフォルダを先に作ってから、ファイルを並べて写す
        std::set<std::filesystem::path> directories;
        for (const CopyItem& item : items) {
            directories.insert(item.to.parent_path());
        }
        for (const std::filesystem::path& directory : directories) {
            std::filesystem::create_directories(directory, ec);
            if (ec) {
                result.error = ToUtf8(directory) + " を作れませんでした" + kLockedHint;
                return result;
            }
        }
        std::vector<ExportJob> copyJobs;
        copyJobs.reserve(items.size());
        for (const CopyItem& item : items) {
            copyJobs.push_back([&item](JobOutcome& outcome) {
                std::error_code copyError;
                std::filesystem::copy_file(item.from, item.to, std::filesystem::copy_options::overwrite_existing, copyError);
                if (copyError) {
                    outcome.error = ToUtf8(item.to) + " を書けませんでした" + kLockedHint;
                    return;
                }
                const std::uintmax_t size = std::filesystem::file_size(item.to, copyError);
                outcome.bytes = copyError ? 0 : size;
                outcome.files = 1;
            });
        }
        for (JobOutcome& outcome : RunJobs(pool, copyJobs, progress.copied)) {
            if (!outcome.error.empty()) {
                result.error = std::move(outcome.error);
                return result;
            }
            result.fileCount += outcome.files;
            result.bytes += outcome.bytes;
        }
        pool.Shutdown();

        // 一覧があると、書き出したゲームは元のファイルから派生データを作らない
        if (!WriteCookManifest(destination, cookRecords, meshRecords, result.cookedShaders, result.convertedJson)) {
            result.error = ToUtf8(manifestPath) + " を書けませんでした" + kLockedHint;
            return result;
        }
        ++result.fileCount;

        std::vector<CookRecord> records = std::move(cookRecords);
        records.insert(records.end(), meshRecords.begin(), meshRecords.end());
        result.warnings = VerifyExport(destination, records, splashCopy);
        for (const std::string& warning : result.warnings) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::System, "書き出しの確認: {}", warning);
        }
        return result;
    }
}

#endif // CORE_EDITOR
