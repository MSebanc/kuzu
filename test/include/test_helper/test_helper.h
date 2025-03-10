#pragma once

#include <filesystem>
#include <memory>

#include "main/kuzu.h"

namespace kuzu {
namespace testing {

struct TestQueryConfig {
    std::string name;
    std::string query;
    uint64_t numThreads = 4;
    std::string encodedJoin;
    uint64_t expectedNumTuples = 0;
    std::vector<std::string> expectedTuples;
    bool enumerate = false;
    bool checkOutputOrder = false;
};

class TestHelper {
public:
    static constexpr char E2E_TEST_FILES_DIRECTORY[] = TEST_FILES_DIR;
    static constexpr char SCHEMA_FILE_NAME[] = "schema.cypher";
    static constexpr char COPY_FILE_NAME[] = "copy.cypher";
    static constexpr char TEST_ANSWERS_PATH[] = "test/answers";
    static constexpr char TEST_STATEMENTS_PATH[] = "test/statements";
    static constexpr char DEFAULT_CONN_NAME[] = "conn_default";

    static std::vector<std::unique_ptr<TestQueryConfig>> parseTestFile(const std::string& path,
        bool checkOutputOrder = false);

    static std::vector<std::string> convertResultToString(main::QueryResult& queryResult,
        bool checkOutputOrder = false);

    static void executeScript(const std::string& path, main::Connection& conn);

    static std::string getTestListFile() {
        return appendKuzuRootPath(std::string(E2E_TEST_FILES_DIRECTORY) + "/test_list");
    }

    static std::string appendKuzuRootPath(const std::string& path) {
        if (std::filesystem::path(path).is_relative()) {
            return KUZU_ROOT_DIRECTORY + std::string("/") + path;
        } else {
            return path;
        }
    }

    static std::unique_ptr<main::SystemConfig> getSystemConfigFromEnv() {
        auto systemConfig = std::make_unique<main::SystemConfig>();
        auto autoCheckpointEnv = std::getenv("AUTO_CHECKPOINT");
        auto bufferPoolSizeEnv = std::getenv("BUFFER_POOL_SIZE");
        auto maxNumThreadsEnv = std::getenv("MAX_NUM_THREADS");
        auto enableCompressionEnv = std::getenv("ENABLE_COMPRESSION");
        auto checkpointThresholdEnv = std::getenv("CHECKPOINT_THRESHOLD");
        auto isValid = [](const char* env) { return env != nullptr && strlen(env) > 0; };
        systemConfig->autoCheckpoint =
            isValid(autoCheckpointEnv) ? std::string(autoCheckpointEnv) == "true" : false;
        systemConfig->bufferPoolSize =
            isValid(bufferPoolSizeEnv) ?
                std::stoull(bufferPoolSizeEnv) :
                common::BufferPoolConstants::DEFAULT_BUFFER_POOL_SIZE_FOR_TESTING;
        systemConfig->maxNumThreads = isValid(maxNumThreadsEnv) ? std::stoull(maxNumThreadsEnv) : 2;
        systemConfig->enableCompression =
            isValid(enableCompressionEnv) ? std::string(enableCompressionEnv) == "true" : true;
        systemConfig->checkpointThreshold = isValid(checkpointThresholdEnv) ?
                                                std::stoull(checkpointThresholdEnv) :
                                                systemConfig->checkpointThreshold;
        return systemConfig;
    }

    static std::string getMillisecondsSuffix();

    inline static std::filesystem::path getTempDir() {
        auto tempDir = std::getenv("RUNNER_TEMP");
        if (tempDir != nullptr) {
            return std::filesystem::path(tempDir) / "kuzu";
        } else {
            return std::filesystem::temp_directory_path() / "kuzu";
        }
    }

    inline static std::string getTempDir(const std::string& name) {
        auto path = getTempDir() / (name + TestHelper::getMillisecondsSuffix());
        std::filesystem::create_directories(path);
        auto pathStr = path.string();
#ifdef _WIN32
        // kuzu still doesn't support backslashes in paths on windows
        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
#endif
        return pathStr;
    }

private:
    static void initializeConnection(TestQueryConfig* config, main::Connection& conn);
};

} // namespace testing
} // namespace kuzu
