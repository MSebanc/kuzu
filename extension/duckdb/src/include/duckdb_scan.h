#pragma once

#include "common/types/types.h"
#include "common/vector/value_vector.h"
#include "function/table/bind_data.h"
#include "function/table/scan_functions.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
// Supress warnings from duckdb.hpp
#undef ARROW_FLAG_DICTIONARY_ORDERED
#include "duckdb.hpp"
#pragma GCC diagnostic pop

namespace kuzu {
namespace duckdb_extension {

using duckdb_conversion_func_t = std::function<void(duckdb::Vector& duckDBVector,
    common::ValueVector& result, uint64_t numValues)>;
using init_duckdb_conn_t = std::function<std::pair<duckdb::DuckDB, duckdb::Connection>()>;

struct DuckDBScanBindData : public function::TableFuncBindData {
    explicit DuckDBScanBindData(std::string query, std::vector<common::LogicalType> columnTypes,
        std::vector<std::string> columnNames, init_duckdb_conn_t initDuckDBConn);

    std::unique_ptr<TableFuncBindData> copy() const override;

    std::string query;
    std::vector<duckdb_conversion_func_t> conversionFunctions;
    init_duckdb_conn_t initDuckDBConn;
};

struct DuckDBScanSharedState : public function::BaseScanSharedStateWithNumRows {
    explicit DuckDBScanSharedState(std::unique_ptr<duckdb::MaterializedQueryResult> queryResult);

    std::unique_ptr<duckdb::MaterializedQueryResult> queryResult;
};

void getDuckDBVectorConversionFunc(common::PhysicalTypeID physicalTypeID,
    duckdb_conversion_func_t& conversion_func);

function::TableFunction getScanFunction(DuckDBScanBindData bindData);

} // namespace duckdb_extension
} // namespace kuzu
