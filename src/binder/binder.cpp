#include "binder/binder.h"

#include "binder/bound_statement_rewriter.h"
#include "catalog/catalog.h"
#include "catalog/catalog_entry/table_catalog_entry.h"
#include "common/copier_config/csv_reader_config.h"
#include "common/exception/binder.h"
#include "common/keyword/rdf_keyword.h"
#include "common/string_format.h"
#include "common/string_utils.h"
#include "function/built_in_function_utils.h"
#include "function/table_functions.h"
#include "processor/operator/persistent/reader/csv/parallel_csv_reader.h"
#include "processor/operator/persistent/reader/csv/serial_csv_reader.h"
#include "processor/operator/persistent/reader/npy/npy_reader.h"
#include "processor/operator/persistent/reader/parquet/parquet_reader.h"

using namespace kuzu::catalog;
using namespace kuzu::common;
using namespace kuzu::parser;
using namespace kuzu::processor;

namespace kuzu {
namespace binder {

std::unique_ptr<BoundStatement> Binder::bind(const Statement& statement) {
    std::unique_ptr<BoundStatement> boundStatement;
    switch (statement.getStatementType()) {
    case StatementType::CREATE_TABLE: {
        boundStatement = bindCreateTable(statement);
    } break;
    case StatementType::CREATE_TYPE: {
        boundStatement = bindCreateType(statement);
    } break;
    case StatementType::CREATE_SEQUENCE: {
        boundStatement = bindCreateSequence(statement);
    } break;
    case StatementType::COPY_FROM: {
        boundStatement = bindCopyFromClause(statement);
    } break;
    case StatementType::COPY_TO: {
        boundStatement = bindCopyToClause(statement);
    } break;
    case StatementType::DROP: {
        boundStatement = bindDrop(statement);
    } break;
    case StatementType::ALTER: {
        boundStatement = bindAlter(statement);
    } break;
    case StatementType::QUERY: {
        boundStatement = bindQuery((const RegularQuery&)statement);
    } break;
    case StatementType::STANDALONE_CALL: {
        boundStatement = bindStandaloneCall(statement);
    } break;
    case StatementType::EXPLAIN: {
        boundStatement = bindExplain(statement);
    } break;
    case StatementType::CREATE_MACRO: {
        boundStatement = bindCreateMacro(statement);
    } break;
    case StatementType::TRANSACTION: {
        boundStatement = bindTransaction(statement);
    } break;
    case StatementType::EXTENSION: {
        boundStatement = bindExtension(statement);
    } break;
    case StatementType::EXPORT_DATABASE: {
        boundStatement = bindExportDatabaseClause(statement);
    } break;
    case StatementType::IMPORT_DATABASE: {
        boundStatement = bindImportDatabaseClause(statement);
    } break;
    case StatementType::ATTACH_DATABASE: {
        boundStatement = bindAttachDatabase(statement);
    } break;
    case StatementType::DETACH_DATABASE: {
        boundStatement = bindDetachDatabase(statement);
    } break;
    case StatementType::USE_DATABASE: {
        boundStatement = bindUseDatabase(statement);
    } break;
    default: {
        KU_UNREACHABLE;
    }
    }
    BoundStatementRewriter::rewrite(*boundStatement, *clientContext);
    return boundStatement;
}

std::shared_ptr<Expression> Binder::bindWhereExpression(const ParsedExpression& parsedExpression) {
    auto whereExpression = expressionBinder.bindExpression(parsedExpression);
    expressionBinder.implicitCastIfNecessary(whereExpression, LogicalType::BOOL());
    return whereExpression;
}

common::table_id_t Binder::bindTableID(const std::string& tableName) const {
    auto catalog = clientContext->getCatalog();
    if (!catalog->containsTable(clientContext->getTx(), tableName)) {
        throw BinderException(common::stringFormat("Table {} does not exist.", tableName));
    }
    return catalog->getTableID(clientContext->getTx(), tableName);
}

std::shared_ptr<Expression> Binder::createVariable(std::string_view name,
    common::LogicalTypeID typeID) {
    return createVariable(std::string(name), LogicalType{typeID});
}

std::shared_ptr<Expression> Binder::createVariable(const std::string& name,
    LogicalTypeID logicalTypeID) {
    return createVariable(name, LogicalType{logicalTypeID});
}

std::shared_ptr<Expression> Binder::createVariable(const std::string& name,
    const LogicalType& dataType) {
    if (scope.contains(name)) {
        throw BinderException("Variable " + name + " already exists.");
    }
    auto expression = expressionBinder.createVariableExpression(dataType.copy(), name);
    expression->setAlias(name);
    addToScope(name, expression);
    return expression;
}

void Binder::validateProjectionColumnNamesAreUnique(const expression_vector& expressions) {
    auto existColumnNames = std::unordered_set<std::string>();
    for (auto& expression : expressions) {
        auto columnName = expression->hasAlias() ? expression->getAlias() : expression->toString();
        if (existColumnNames.contains(columnName)) {
            throw BinderException(
                "Multiple result column with the same name " + columnName + " are not supported.");
        }
        existColumnNames.insert(columnName);
    }
}

void Binder::validateProjectionColumnsInWithClauseAreAliased(const expression_vector& expressions) {
    for (auto& expression : expressions) {
        if (!expression->hasAlias()) {
            throw BinderException("Expression in WITH must be aliased (use AS).");
        }
    }
}

void Binder::validateOrderByFollowedBySkipOrLimitInWithClause(
    const BoundProjectionBody& boundProjectionBody) {
    auto hasSkipOrLimit = boundProjectionBody.hasSkip() || boundProjectionBody.hasLimit();
    if (boundProjectionBody.hasOrderByExpressions() && !hasSkipOrLimit) {
        throw BinderException("In WITH clause, ORDER BY must be followed by SKIP or LIMIT.");
    }
}

void Binder::validateTableType(table_id_t tableID, TableType expectedTableType) {
    auto tableEntry =
        clientContext->getCatalog()->getTableCatalogEntry(clientContext->getTx(), tableID);
    if (tableEntry->getTableType() != expectedTableType) {
        throw BinderException("Table type mismatch.");
    }
}

void Binder::validateTableExist(const std::string& tableName) {
    if (!clientContext->getCatalog()->containsTable(clientContext->getTx(), tableName)) {
        throw BinderException("Table " + tableName + " does not exist.");
    }
}

std::string Binder::getUniqueExpressionName(const std::string& name) {
    return "_" + std::to_string(lastExpressionId++) + "_" + name;
}

struct ReservedNames {
    // Column name that might conflict with internal names.
    static std::unordered_set<std::string> getColumnNames() {
        return {
            InternalKeyword::ID,
            InternalKeyword::LABEL,
            InternalKeyword::SRC,
            InternalKeyword::DST,
            InternalKeyword::DIRECTION,
            InternalKeyword::LENGTH,
            InternalKeyword::NODES,
            InternalKeyword::RELS,
            InternalKeyword::PLACE_HOLDER,
            StringUtils::getUpper(InternalKeyword::ROW_OFFSET),
            StringUtils::getUpper(InternalKeyword::SRC_OFFSET),
            StringUtils::getUpper(InternalKeyword::DST_OFFSET),
            StringUtils::getUpper(rdf::PID),
            StringUtils::getUpper(rdf::IRI),
        };
    }

    // Properties that should be hidden from user access.
    static std::unordered_set<std::string> getPropertyLookupName() {
        return {
            InternalKeyword::ID,
            StringUtils::getUpper(rdf::PID),
        };
    }
};

bool Binder::reservedInColumnName(const std::string& name) {
    auto normalizedName = StringUtils::getUpper(name);
    return ReservedNames::getColumnNames().contains(normalizedName);
}

bool Binder::reservedInPropertyLookup(const std::string& name) {
    auto normalizedName = StringUtils::getUpper(name);
    return ReservedNames::getPropertyLookupName().contains(normalizedName);
}

void Binder::addToScope(const std::string& name, std::shared_ptr<Expression> expr) {
    // TODO(Xiyang): assert name not in scope.
    scope.addExpression(name, std::move(expr));
}

BinderScope Binder::saveScope() {
    return scope.copy();
}

void Binder::restoreScope(BinderScope prevScope) {
    scope = std::move(prevScope);
}

function::TableFunction Binder::getScanFunction(FileType fileType, const ReaderConfig& config) {
    function::Function* func;
    std::vector<LogicalType> inputTypes;
    inputTypes.push_back(LogicalType::STRING());
    auto functions = clientContext->getCatalog()->getFunctions(clientContext->getTx());
    switch (fileType) {
    case FileType::PARQUET: {
        func = function::BuiltInFunctionsUtils::matchFunction(clientContext->getTx(),
            ParquetScanFunction::name, inputTypes, functions);
    } break;
    case FileType::NPY: {
        func = function::BuiltInFunctionsUtils::matchFunction(clientContext->getTx(),
            NpyScanFunction::name, inputTypes, functions);
    } break;
    case FileType::CSV: {
        auto csvConfig = CSVReaderConfig::construct(config.options);
        func = function::BuiltInFunctionsUtils::matchFunction(clientContext->getTx(),
            csvConfig.parallel ? ParallelCSVScan::name : SerialCSVScan::name, inputTypes,
            functions);
    } break;
    case FileType::JSON: {
        func = function::BuiltInFunctionsUtils::matchFunction(clientContext->getTx(),
            SCAN_JSON_FUNC_NAME, inputTypes, functions);
    } break;
    default:
        KU_UNREACHABLE;
    }
    return *ku_dynamic_cast<function::Function*, function::TableFunction*>(func);
}

} // namespace binder
} // namespace kuzu
