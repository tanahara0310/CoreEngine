#pragma once

#include "GameObject/Component/Core/IComponent.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "MapChipData.h"

namespace GameComponents
{
    enum class MapGenerationMode {
        Procedural,
        RandomCsvPool,
        FixedCsv,
        // fixedCsvPath の全列を先頭に置き、その後ろからCSVプールを区画単位で継ぐ。
        FixedThenRandomCsvPool,
    };

    // 1エリア用の区画CSV集合。選択されたエリアのpathsからのみランダムに抽選する。
    struct CsvMapPoolSettings {
        std::string name;
        std::vector<std::string> paths;
    };

    struct MapGenerationSettings {
        MapGenerationMode mode = MapGenerationMode::Procedural;
        std::vector<std::string> csvPoolPaths;
        // エリアごとに複数のCSVを登録する。指定した場合、csvPoolPathsより優先する。
        std::vector<CsvMapPoolSettings> csvPools;
        // 空文字なら最初のプール。csvPoolPathsだけの従来設定はDefaultプールになる。
        std::string initialCsvPoolName;
        std::string fixedCsvPath;
        // ランダムCSVの区画幅。CSVの不足部分はVoid、超過部分は切り捨てる。
        std::size_t csvChunkSizeX = 10;
        // 指定するとランダムCSVの並びを再現できる。
        std::optional<uint32_t> randomSeed;
    };

    // マップを生成するコンポーネント
    class MapGeneratorComponent final
        : public CoreEngine::IComponent {
    public:
        explicit MapGeneratorComponent(uint32_t mapSizeZ = 10, uint32_t startGenerateX = 30,
            MapGenerationSettings settings = {});

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "MapGenerator";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

        // 最初の更新直前に一度だけ呼ばれる
        void Start() override;
        // 毎フレーム呼ばれる
        void Update() override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "マップ生成"; }
        bool DrawInspector() override;
#endif

        // 次の区画から使うプールを選択する。生成済み・生成途中の区画は変更しない。
        // 存在しない名前やCSVランダム生成以外ではfalse（現在の選択は維持）。
        bool SelectCsvPool(const std::string& name);
        std::string GetSelectedCsvPoolName() const;
        std::string GetActiveCsvPoolName() const;
        std::vector<std::string> GetCsvPoolNames() const;

        // 先頭の固定マップの幅。固定マップを使わない場合は0。
        std::size_t GetFixedMapSizeX() const;

        // X方向に指定された列数までマップを生成する
        void CreateToX(std::size_t xCount);
        // 追加でマップを生成する
        void AddMapChips(std::size_t count);

        // 指定マスの種類を取得・変更する
        MapChipType GetMapChip(std::size_t x, std::size_t z) const;
        bool SetMapChip(std::size_t x, std::size_t z, MapChipType type);

        // 駅の正面（-Z側の隣接マス）には、常設レールと地面を用意する。
        bool IsStationRailCell(std::size_t x, std::size_t z) const;
        // 駅本体などの建設不可マスを避け、隣接マスへレールを接続できるか判定する。
        bool CanConnectRail(int32_t fromX, int32_t fromZ, int32_t toX, int32_t toZ) const;

        // マップチップの2D配列を取得する
        const std::vector<std::vector<GameComponents::MapChipType>>& GetMapChips() const;

    private:
        using MapData = std::vector<std::vector<MapChipType>>;
        struct LoadedCsvPool {
            std::string name;
            std::vector<MapData> maps;
            // 全チャンクを一度ずつ使うためのシャッフル済みインデックス。
            std::vector<std::size_t> shuffledIndices;
            std::size_t nextShuffledIndex = 0;
        };

        // CSVは列=X・行=Z。内部のmap[x][z]形式へ変換して一度だけ読み込む。
        MapData LoadCsv(const std::string& path, std::size_t width = 0) const;
        void LoadCsvPools();
        void RefillCsvShuffleBag(LoadedCsvPool& pool);
        void AddProceduralMapChips(std::size_t count);
        void AddCsvMapChips(std::size_t count);

        uint32_t mapSizeZ_ = 10;
        uint32_t initialGenerateSizeX_ = 30;
        MapGenerationSettings settings_;
        std::mt19937 csvRandom_;
        MapData fixedCsv_;
        std::vector<LoadedCsvPool> csvPools_;
        std::optional<std::size_t> selectedCsvPoolIndex_;
        std::optional<std::size_t> activeCsvPoolIndex_;
        std::size_t activeCsvIndex_ = 0;
        std::size_t activeCsvColumn_ = 0;

        uint32_t stationBuildInterval_ = 5; // 駅を建設する間隔（X方向のマス数）

        std::vector<std::vector<GameComponents::MapChipType>> mapChips_;
    };
}
