#pragma once

#include <cstdint>
#include <string_view>

namespace kuzu {
namespace common {

extern const char* KUZU_VERSION;

constexpr uint64_t DEFAULT_VECTOR_CAPACITY_LOG_2 = 11;
constexpr uint64_t DEFAULT_VECTOR_CAPACITY = static_cast<uint64_t>(1)
                                             << DEFAULT_VECTOR_CAPACITY_LOG_2;

constexpr double DEFAULT_HT_LOAD_FACTOR = 1.5;

// This is the default thread sleep time we use when a thread,
// e.g., a worker thread is in TaskScheduler, needs to block.
constexpr uint64_t THREAD_SLEEP_TIME_WHEN_WAITING_IN_MICROS = 500;

constexpr uint64_t DEFAULT_CHECKPOINT_WAIT_TIMEOUT_IN_MICROS = 5000000;

// Note that some places use std::bit_ceil to calculate resizes,
// which won't work for values other than 2. If this is changed, those will need to be updated
constexpr uint64_t CHUNK_RESIZE_RATIO = 2;

struct InternalKeyword {
    static constexpr char ANONYMOUS[] = "";
    static constexpr char ID[] = "_ID";
    static constexpr char LABEL[] = "_LABEL";
    static constexpr char SRC[] = "_SRC";
    static constexpr char DST[] = "_DST";
    static constexpr char DIRECTION[] = "_DIRECTION";
    static constexpr char LENGTH[] = "_LENGTH";
    static constexpr char NODES[] = "_NODES";
    static constexpr char RELS[] = "_RELS";
    static constexpr char STAR[] = "*";
    static constexpr char PLACE_HOLDER[] = "_PLACE_HOLDER";
    static constexpr char MAP_KEY[] = "KEY";
    static constexpr char MAP_VALUE[] = "VALUE";

    static constexpr std::string_view ROW_OFFSET = "_row_offset";
    static constexpr std::string_view SRC_OFFSET = "_src_offset";
    static constexpr std::string_view DST_OFFSET = "_dst_offset";
};

enum PageSizeClass : uint8_t {
    PAGE_4KB = 0,
    PAGE_256KB = 1,
};

// Currently the system supports files with 2 different pages size, which we refer to as
// PAGE_4KB_SIZE and PAGE_256KB_SIZE. PAGE_4KB_SIZE is the default size of the page which is the
// unit of read/write to the database files, such as to store columns or lists. For now, this value
// cannot be changed. But technically it can change from 2^12 to 2^16. 2^12 lower bound is assuming
// the OS page size is 4K. 2^16 is because currently we leave 11 fixed number of bits for
// relOffInPage and the maximum number of bytes needed for an edge is 20 bytes so 11 + log_2(20)
// = 15.xxx, so certainly over 2^16-size pages, we cannot utilize the page for storing adjacency
// lists.
struct BufferPoolConstants {
    static constexpr uint64_t PAGE_4KB_SIZE_LOG2 = 12;
    static constexpr uint64_t PAGE_4KB_SIZE = static_cast<uint64_t>(1) << PAGE_4KB_SIZE_LOG2;
    // Page size for files with large pages, e.g., temporary files that are used by operators that
    // may require large amounts of memory.
    static constexpr uint64_t PAGE_256KB_SIZE_LOG2 = 18;
    static constexpr uint64_t PAGE_256KB_SIZE = static_cast<uint64_t>(1) << PAGE_256KB_SIZE_LOG2;
    // If a user does not specify a max size for BM, we by default set the max size of BM to
    // maxPhyMemSize * DEFAULT_PHY_MEM_SIZE_RATIO_FOR_BM.
    static constexpr double DEFAULT_PHY_MEM_SIZE_RATIO_FOR_BM = 0.8;
    // For each PURGE_EVICTION_QUEUE_INTERVAL candidates added to the eviction queue, we will call
    // `removeNonEvictableCandidates` to remove candidates that are not evictable. See
    // `EvictionQueue::removeNonEvictableCandidates()` for more details.
    static constexpr uint64_t EVICTION_QUEUE_PURGING_INTERVAL = 1024;
// The default max size for a VMRegion.
#ifdef __32BIT__
    static constexpr uint64_t DEFAULT_VM_REGION_MAX_SIZE = (uint64_t)1 << 30; // (1GB)
#else
    static constexpr uint64_t DEFAULT_VM_REGION_MAX_SIZE = static_cast<uint64_t>(1) << 43; // (8TB)
#endif

    static constexpr uint64_t DEFAULT_BUFFER_POOL_SIZE_FOR_TESTING = 1ull << 26; // (64MB)
};

struct StorageConstants {
    static constexpr char OVERFLOW_FILE_SUFFIX[] = ".ovf";
    static constexpr char WAL_FILE_SUFFIX[] = ".wal";
    static constexpr char SHADOWING_SUFFIX[] = ".shadow";
    static constexpr char INDEX_FILE_SUFFIX[] = ".hindex";
    static constexpr char CATALOG_FILE_NAME[] = "catalog.kz";
    static constexpr char CATALOG_FILE_NAME_FOR_WAL[] = "catalog.shadow";
    static constexpr char DATA_FILE_NAME[] = "data.kz";
    static constexpr char METADATA_FILE_NAME[] = "metadata.kz";
    static constexpr char METADATA_FILE_NAME_FOR_WAL[] = "metadata.shadow";
    static constexpr char LOCK_FILE_NAME[] = ".lock";

    // The number of pages that we add at one time when we need to grow a file.
    static constexpr uint64_t PAGE_GROUP_SIZE_LOG2 = 10;
    static constexpr uint64_t PAGE_GROUP_SIZE = static_cast<uint64_t>(1) << PAGE_GROUP_SIZE_LOG2;
    static constexpr uint64_t PAGE_IDX_IN_GROUP_MASK =
        (static_cast<uint64_t>(1) << PAGE_GROUP_SIZE_LOG2) - 1;

    static constexpr uint64_t NODE_GROUP_SIZE_LOG2 = 17; // 64 * 2048 nodes per group
    static constexpr uint64_t NODE_GROUP_SIZE = static_cast<uint64_t>(1) << NODE_GROUP_SIZE_LOG2;
    static constexpr uint64_t NUM_VECTORS_PER_NODE_GROUP =
        NODE_GROUP_SIZE / DEFAULT_VECTOR_CAPACITY;

    static constexpr double PACKED_CSR_DENSITY = 0.8;
    static constexpr double LEAF_LOW_CSR_DENSITY = 0.1;
    static constexpr double LEAF_HIGH_CSR_DENSITY = 1.0;
    // The number of CSR lists in a leaf region.
    static constexpr uint64_t CSR_LEAF_REGION_SIZE_LOG2 = 10;
    static constexpr uint64_t CSR_LEAF_REGION_SIZE = static_cast<uint64_t>(1)
                                                     << CSR_LEAF_REGION_SIZE_LOG2;

    static constexpr uint64_t MAX_NUM_ROWS_IN_TABLE = static_cast<uint64_t>(1) << 62;
};

// Hash Index Configurations
struct HashIndexConstants {
    static constexpr uint16_t SLOT_CAPACITY_BYTES = 256;
    static constexpr double MAX_LOAD_FACTOR = 0.8;
};

struct CopyConstants {
    // Initial size of buffer for CSV Reader.
    static constexpr uint64_t INITIAL_BUFFER_SIZE = 16384;
    // This means that we will usually read the entirety of the contents of the file we need for a
    // block in one read request. It is also very small, which means we can parallelize small files
    // efficiently.
    static constexpr uint64_t PARALLEL_BLOCK_SIZE = INITIAL_BUFFER_SIZE / 2;

    static constexpr const char* BOOL_CSV_PARSING_OPTIONS[] = {"HEADER", "PARALLEL"};
    static constexpr bool DEFAULT_CSV_HAS_HEADER = false;
    static constexpr bool DEFAULT_CSV_PARALLEL = true;

    // Default configuration for csv file parsing
    static constexpr const char* STRING_CSV_PARSING_OPTIONS[] = {"ESCAPE", "DELIM", "QUOTE"};
    static constexpr char DEFAULT_CSV_ESCAPE_CHAR = '"';
    static constexpr char DEFAULT_CSV_DELIMITER = ',';
    static constexpr char DEFAULT_CSV_QUOTE_CHAR = '"';
    static constexpr char DEFAULT_CSV_LIST_BEGIN_CHAR = '[';
    static constexpr char DEFAULT_CSV_LIST_END_CHAR = ']';
    static constexpr char DEFAULT_CSV_LINE_BREAK = '\n';
    static constexpr const char* ROW_IDX_COLUMN_NAME = "ROW_IDX";
    static constexpr uint64_t PANDAS_PARTITION_COUNT = 50 * DEFAULT_VECTOR_CAPACITY;

    static constexpr const char* INT_CSV_PARSING_OPTIONS[] = {"SKIP"};
    static constexpr uint64_t DEFAULT_CSV_SKIP_NUM = 0;
};

struct RdfConstants {
    static constexpr char IN_MEMORY_OPTION[] = "IN_MEMORY";
    static constexpr char STRICT_OPTION[] = "STRICT";
};

struct PlannerKnobs {
    static constexpr double NON_EQUALITY_PREDICATE_SELECTIVITY = 0.1;
    static constexpr double EQUALITY_PREDICATE_SELECTIVITY = 0.01;
    static constexpr uint64_t BUILD_PENALTY = 2;
    // Avoid doing probe to build SIP if we have to accumulate a probe side that is much bigger than
    // build side. Also avoid doing build to probe SIP if probe side is not much bigger than build.
    static constexpr uint64_t SIP_RATIO = 5;
};

struct OrderByConstants {
    static constexpr uint64_t NUM_BYTES_FOR_PAYLOAD_IDX = 8;
    static constexpr uint64_t MIN_SIZE_TO_REDUCE = common::DEFAULT_VECTOR_CAPACITY * 5;
    static constexpr uint64_t MIN_LIMIT_RATIO_TO_REDUCE = 2;
};

struct ParquetConstants {
    static constexpr uint64_t PARQUET_DEFINE_VALID = 65535;
    static constexpr const char* PARQUET_MAGIC_WORDS = "PAR1";
    // We limit the uncompressed page size to 100MB.
    // The max size in Parquet is 2GB, but we choose a more conservative limit.
    static constexpr uint64_t MAX_UNCOMPRESSED_PAGE_SIZE = 100000000;
    // Dictionary pages must be below 2GB. Unlike data pages, there's only one dictionary page.
    // For this reason we go with a much higher, but still a conservative upper bound of 1GB.
    static constexpr uint64_t MAX_UNCOMPRESSED_DICT_PAGE_SIZE = 1e9;
    // The maximum size a key entry in an RLE page takes.
    static constexpr uint64_t MAX_DICTIONARY_KEY_SIZE = sizeof(uint32_t);
    // The size of encoding the string length.
    static constexpr uint64_t STRING_LENGTH_SIZE = sizeof(uint32_t);
    static constexpr uint64_t MAX_STRING_STATISTICS_SIZE = 10000;
    static constexpr uint64_t PARQUET_INTERVAL_SIZE = 12;
};

struct ExportCSVConstants {
    static constexpr const char* DEFAULT_CSV_NEWLINE = "\n";
    static constexpr const char* DEFAULT_NULL_STR = "";
    static constexpr bool DEFAULT_FORCE_QUOTE = false;
    static constexpr uint64_t DEFAULT_CSV_FLUSH_SIZE = 4096 * 8;
};

struct ImportDBConstants {
    static constexpr char SCHEMA_NAME[] = "schema.cypher";
    static constexpr char COPY_NAME[] = "copy.cypher";
};

static constexpr char ATTACHED_KUZU_DB_TYPE[] = "KUZU";

static constexpr char LOCAL_DB_NAME[] = "local(kuzu)";

constexpr auto DECIMAL_PRECISION_LIMIT = 38;

static constexpr char SCAN_JSON_FUNC_NAME[] = "READ_JSON";

} // namespace common
} // namespace kuzu
