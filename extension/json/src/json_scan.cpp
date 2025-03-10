#include "json_scan.h"

#include "common/exception/binder.h"
#include "common/exception/runtime.h"
#include "common/string_utils.h"
#include "function/built_in_function_utils.h"
#include "function/table/scan_functions.h"
#include "json_utils.h"

using namespace kuzu::function;
using namespace kuzu::common;

namespace kuzu {
namespace json_extension {

constexpr JsonScanFormat DEFAULT_JSON_FORMAT = JsonScanFormat::ARRAY;
constexpr int64_t DEFAULT_JSON_DEPTH = -1;
constexpr int64_t DEFAULT_JSON_BREADTH = -1;

struct JsonScanConfig {
    JsonScanFormat format = DEFAULT_JSON_FORMAT;
    int64_t depth = DEFAULT_JSON_DEPTH;
    int64_t breadth = DEFAULT_JSON_BREADTH;

    explicit JsonScanConfig(const std::unordered_map<std::string, Value>& options) {
        for (const auto& i : options) {
            if (i.first == "FORMAT") {
                if (i.second.getDataType().getLogicalTypeID() != LogicalTypeID::STRING) {
                    throw BinderException("Format parameter must be a string.");
                }
                auto tmp = StringUtils::getUpper(i.second.strVal);
                if (tmp == "ARRAY") {
                    format = JsonScanFormat::ARRAY;
                } else if (tmp == "UNSTRUCTURED") {
                    format = JsonScanFormat::UNSTRUCTURED;
                } else {
                    throw RuntimeException(
                        "Invalid JSON file format: Must either be 'unstructured' or 'array'");
                }
            } else if (i.first == "MAXIMUM_DEPTH") {
                if (i.second.getDataType().getLogicalTypeID() != LogicalTypeID::INT64) {
                    throw BinderException("Maximum depth parameter must be an int64.");
                }
                depth = i.second.val.int64Val;
            } else if (i.first == "SAMPLE_SIZE") {
                if (i.second.getDataType().getLogicalTypeID() != LogicalTypeID::INT64) {
                    throw BinderException("Sample size parameter must be an int64.");
                }
                breadth = i.second.val.int64Val;
            } else {
                throw BinderException(stringFormat("Unrecognized parameter: {}", i.first));
            }
        }
    }
};

struct JsonBindData : public ScanBindData {
    std::shared_ptr<JsonWrapper> json;
    bool scanFromList;
    bool scanFromStruct;

    JsonBindData(std::vector<common::LogicalType> columnTypes, std::vector<std::string> columnNames,
        std::shared_ptr<JsonWrapper> wrapper, bool scanFromList, bool scanFromStruct,
        ReaderConfig config, main::ClientContext* ctx)
        : ScanBindData(std::move(columnTypes), std::move(columnNames), std::move(config), ctx),
          json(wrapper), scanFromList{scanFromList}, scanFromStruct{scanFromStruct} {
        for (auto i = 0u; i < this->columnNames.size(); i++) {
            nameToIdxMap[StringUtils::getUpper(this->columnNames[i])] = i;
        }
    }

    std::unique_ptr<TableFuncBindData> copy() const override {
        return std::make_unique<JsonBindData>(LogicalType::copy(columnTypes), columnNames, json,
            scanFromList, scanFromStruct, config.copy(), context);
    }

    uint32_t getIdxFromName(const std::string& s) const {
        return nameToIdxMap.at(StringUtils::getUpper(s));
    }

private:
    std::map<std::string, uint32_t> nameToIdxMap;
};

struct JsonScanLocalState : public TableFuncLocalState {
    std::vector<yyjson_val*>::iterator begin, end;
    uint32_t chunkSize;

    JsonScanLocalState(std::vector<yyjson_val*>::iterator begin,
        std::vector<yyjson_val*>::iterator end, uint32_t chunkSize)
        : begin{begin}, end{end}, chunkSize{chunkSize} {}
};

struct JsonScanSharedState : public BaseScanSharedState {
    std::vector<yyjson_val*> rows;
    uint64_t curPos = 0u;

    explicit JsonScanSharedState(std::vector<yyjson_val*> rows)
        : BaseScanSharedState{}, rows{std::move(rows)} {}

    uint64_t getNumRows() const override { return rows.size(); }

    // returns success
    bool tryGetNextLocalState(std::vector<yyjson_val*>::iterator& begin,
        std::vector<yyjson_val*>::iterator& end, uint32_t& chunkSize) {
        std::scoped_lock scopedLock(lock);
        if (curPos >= rows.size()) {
            chunkSize = 0;
            begin = end = rows.end();
            return false;
        }
        chunkSize = std::min(DEFAULT_VECTOR_CAPACITY, rows.size() - curPos);
        begin = rows.begin() + curPos;
        curPos += chunkSize;
        end = rows.begin() + curPos;
        return true;
    }
};

static bool jsonContextCanCast(const LogicalType& in, const LogicalType& out) {
    if (in.getLogicalTypeID() == LogicalTypeID::STRING ||
        (LogicalTypeUtils::isNumerical(in) && LogicalTypeUtils::isNumerical(out))) {
        return true;
    }
    return BuiltInFunctionsUtils::getCastCost(in.getLogicalTypeID(), out.getLogicalTypeID()) !=
           UNDEFINED_CAST_COST;
}

static void verifyExpectedSchema(const std::vector<LogicalType>& expectedTypes,
    const std::vector<LogicalType>& actualTypes, const std::vector<std::string>& expectedNames,
    const std::vector<std::string>&
        actualNames) { // NOLINT : this function will be used in the future
    if (expectedNames.size() != actualNames.size()) {
        throw BinderException(stringFormat("Expected {} columns. Found {}", expectedNames.size(),
            actualNames.size()));
    }
    for (auto i = 0u; i < expectedNames.size(); i++) {
        bool matchFound = false;
        for (auto j = 0u; j < actualNames.size() && !matchFound; j++) {
            if (StringUtils::getUpper(expectedNames[i]) == StringUtils::getUpper(actualNames[j])) {
                if (!jsonContextCanCast(actualTypes[j], expectedTypes[j])) {
                    throw BinderException(stringFormat(
                        "Column '{}' cannot be cast to type {}; actual type is {}",
                        expectedNames[i], expectedTypes[i].toString(), actualTypes[j].toString()));
                } else {
                    matchFound = true;
                }
            }
        }
        if (!matchFound) {
            throw BinderException(
                stringFormat("Column '{}' was not found in JSON file", expectedNames[i]));
        }
    }
}

static std::unique_ptr<TableFuncBindData> bindFunc(main::ClientContext* ctx,
    TableFuncBindInput* input) {
    auto scanInput = input->constPtrCast<ScanTableFuncBindInput>();
    // get parameters
    JsonScanConfig scanConfig(scanInput->config.options);
    auto parsedJson = fileToJson(ctx, scanInput->inputs[0].strVal, scanConfig.format);
    std::vector<LogicalType> columnTypes;
    std::vector<std::string> columnNames;
    auto scanFromList = false;
    auto scanFromStruct = false;
    if (scanInput->expectedColumnNames.size() > 0) {
        columnTypes = LogicalType::copy(scanInput->expectedColumnTypes);
        columnNames = scanInput->expectedColumnNames;
        // still need determine scanning mode
        if (scanConfig.depth == -1 || scanConfig.depth > 3) {
            scanConfig.depth = 3;
        }
        auto schema = jsonSchema(parsedJson, scanConfig.depth, scanConfig.breadth);
        if (schema.getLogicalTypeID() == LogicalTypeID::LIST) {
            schema = ListType::getChildType(schema).copy();
            scanFromList = true;
        }
        if (schema.getLogicalTypeID() == LogicalTypeID::STRUCT) {
            scanFromStruct = true;
        }
    } else {
        auto schema = jsonSchema(parsedJson, scanConfig.depth, scanConfig.breadth);
        if (schema.getLogicalTypeID() == LogicalTypeID::LIST) {
            schema = ListType::getChildType(schema).copy();
            scanFromList = true;
        }
        if (schema.getLogicalTypeID() == LogicalTypeID::STRUCT) {
            for (const auto& i : StructType::getFields(schema)) {
                columnTypes.push_back(i.getType().copy());
                columnNames.push_back(i.getName());
            }
            scanFromStruct = true;
        } else {
            columnTypes.push_back(std::move(schema));
            columnNames.push_back("json");
        }
    }
    auto parsedJsonPtr = parsedJson.ptr;
    parsedJson.ptr = nullptr;
    return std::make_unique<JsonBindData>(std::move(columnTypes), std::move(columnNames),
        std::make_shared<JsonWrapper>(std::move(parsedJsonPtr)), scanFromList, scanFromStruct,
        scanInput->config.copy(), ctx);
}

static std::unique_ptr<TableFuncSharedState> initSharedState(TableFunctionInitInput& input) {
    auto jsonBindData = input.bindData->constPtrCast<JsonBindData>();
    std::vector<yyjson_val*> rows;
    if (jsonBindData->scanFromList) {
        auto it = yyjson_arr_iter_with(yyjson_doc_get_root(jsonBindData->json->ptr));
        yyjson_val* val;
        while ((val = yyjson_arr_iter_next(&it))) {
            rows.push_back(val);
        }
    } else {
        rows.push_back(yyjson_doc_get_root(jsonBindData->json->ptr));
    }
    return std::make_unique<JsonScanSharedState>(std::move(rows));
}

static std::unique_ptr<TableFuncLocalState> initLocalState(TableFunctionInitInput&,
    TableFuncSharedState* shared, storage::MemoryManager* /*mm*/) {
    auto jsonShared = shared->ptrCast<JsonScanSharedState>();
    std::vector<yyjson_val*>::iterator begin, end;
    uint32_t chunkSize;
    jsonShared->tryGetNextLocalState(begin, end, chunkSize);
    return std::make_unique<JsonScanLocalState>(begin, end, chunkSize);
}

static offset_t tableFunc(TableFuncInput& input, TableFuncOutput& output) {
    auto bindData = input.bindData->constPtrCast<JsonBindData>();
    auto localState = ku_dynamic_cast<TableFuncLocalState*, JsonScanLocalState*>(input.localState);
    auto sharedState = input.sharedState->ptrCast<JsonScanSharedState>();
    for (auto& i : output.dataChunk.valueVectors) {
        i->setAllNull();
    }
    for (auto i = localState->begin; i != localState->end; i++) {
        if (bindData->scanFromStruct) {
            auto objIter = yyjson_obj_iter_with(*i);
            yyjson_val *key, *ele;
            while ((key = yyjson_obj_iter_next(&objIter))) {
                ele = yyjson_obj_iter_get_val(key);
                auto columnIdx = bindData->getIdxFromName(yyjson_get_str(key));
                readJsonToValueVector(ele, *output.dataChunk.valueVectors[columnIdx],
                    i - localState->begin);
            }
        } else {
            readJsonToValueVector(*i, *output.dataChunk.valueVectors[0], i - localState->begin);
        }
    }
    auto chunkSize = localState->chunkSize;
    sharedState->tryGetNextLocalState(localState->begin, localState->end, localState->chunkSize);
    return chunkSize;
}

static double progressFunc(TableFuncSharedState* state) {
    auto jsonShared = state->ptrCast<JsonScanSharedState>();
    return (double)jsonShared->curPos / jsonShared->rows.size();
}

std::unique_ptr<TableFunction> JsonScan::getFunction() {
    return std::make_unique<TableFunction>(name, tableFunc, bindFunc, initSharedState,
        initLocalState, progressFunc, std::vector<LogicalTypeID>{LogicalTypeID::STRING});
}

} // namespace json_extension
} // namespace kuzu
