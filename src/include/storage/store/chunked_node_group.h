#pragma once

#include <atomic>

#include "common/enums/rel_multiplicity.h"
#include "common/types/internal_id_t.h"
#include "storage/enums/residency_state.h"
#include "storage/store/column_chunk.h"
#include "storage/store/column_chunk_data.h"
#include "storage/store/version_info.h"

namespace kuzu {
namespace common {
class SelectionVector;
} // namespace common

namespace transaction {
class Transaction;
} // namespace transaction

namespace storage {

class Column;
struct TableScanState;
struct TableAddColumnState;
struct NodeGroupScanState;

enum class NodeGroupDataFormat : uint8_t { REGULAR = 0, CSR = 1 };

class ChunkedNodeGroup {
public:
    static constexpr uint64_t CHUNK_CAPACITY = 2048;

    ChunkedNodeGroup(std::vector<std::unique_ptr<ColumnChunk>> chunks,
        common::row_idx_t startRowIdx, NodeGroupDataFormat format = NodeGroupDataFormat::REGULAR);
    ChunkedNodeGroup(const std::vector<common::LogicalType>& columnTypes, bool enableCompression,
        uint64_t capacity, common::row_idx_t startRowIdx, ResidencyState residencyState,
        NodeGroupDataFormat format = NodeGroupDataFormat::REGULAR);
    virtual ~ChunkedNodeGroup() = default;

    common::idx_t getNumColumns() const { return chunks.size(); }
    common::row_idx_t getStartRowIdx() const { return startRowIdx; }
    common::row_idx_t getNumRows() const { return numRows; }
    const ColumnChunk& getColumnChunk(const common::column_id_t columnID) const {
        KU_ASSERT(columnID < chunks.size());
        return *chunks[columnID];
    }
    ColumnChunk& getColumnChunk(const common::column_id_t columnID) {
        KU_ASSERT(columnID < chunks.size());
        return *chunks[columnID];
    }
    std::unique_ptr<ColumnChunk> moveColumnChunk(const common::column_id_t columnID) {
        KU_ASSERT(columnID < chunks.size());
        return std::move(chunks[columnID]);
    }
    bool isFullOrOnDisk() const {
        return numRows == capacity || residencyState == ResidencyState::ON_DISK;
    }
    ResidencyState getResidencyState() const { return residencyState; }
    NodeGroupDataFormat getFormat() const { return format; }

    void resetToEmpty();
    void resetToAllNull() const;
    void resetNumRowsFromChunks();
    void setNumRows(common::offset_t numRows_);
    void resizeChunks(uint64_t newSize) const;
    void setVersionInfo(std::unique_ptr<VersionInfo> versionInfo) {
        this->versionInfo = std::move(versionInfo);
    }
    void resetVersionAndUpdateInfo();

    uint64_t append(const transaction::Transaction* transaction,
        const std::vector<common::ValueVector*>& columnVectors, common::row_idx_t startRowInVectors,
        uint64_t numValuesToAppend);
    // Appends up to numValuesToAppend from the other chunked node group, returning the actual
    // number of values appended.
    common::offset_t append(const transaction::Transaction* transaction,
        const ChunkedNodeGroup& other, common::offset_t offsetInOtherNodeGroup,
        common::offset_t numRowsToAppend);
    common::offset_t append(const transaction::Transaction* transaction,
        const std::vector<ColumnChunk*>& other, common::offset_t offsetInOtherNodeGroup,
        common::offset_t numRowsToAppend);
    void write(const ChunkedNodeGroup& data, common::column_id_t offsetColumnID);

    void scan(const transaction::Transaction* transaction, const TableScanState& scanState,
        const NodeGroupScanState& nodeGroupScanState, common::offset_t rowIdxInGroup,
        common::length_t numRowsToScan) const;

    template<ResidencyState SCAN_RESIDENCY_STATE>
    void scanCommitted(transaction::Transaction* transaction, TableScanState& scanState,
        NodeGroupScanState& nodeGroupScanState, ChunkedNodeGroup& output) const;

    bool hasUpdates() const;
    common::row_idx_t getNumDeletedRows(const transaction::Transaction* transaction) const;
    common::row_idx_t getNumUpdatedRows(const transaction::Transaction* transaction,
        common::column_id_t columnID);

    std::pair<std::unique_ptr<ColumnChunk>, std::unique_ptr<ColumnChunk>> scanUpdates(
        transaction::Transaction* transaction, common::column_id_t columnID);

    bool lookup(transaction::Transaction* transaction, const TableScanState& state,
        NodeGroupScanState& nodeGroupScanState, common::offset_t rowIdxInChunk,
        common::sel_t posInOutput) const;

    void update(transaction::Transaction* transaction, common::row_idx_t rowIdxInChunk,
        common::column_id_t columnID, const common::ValueVector& propertyVector);

    bool delete_(const transaction::Transaction* transaction, common::row_idx_t rowIdxInChunk);

    void addColumn(transaction::Transaction* transaction, const TableAddColumnState& addColumnState,
        bool enableCompression, BMFileHandle* dataFH);

    bool isDeleted(const transaction::Transaction* transaction, common::row_idx_t rowInChunk) const;
    bool isInserted(const transaction::Transaction* transaction,
        common::row_idx_t rowInChunk) const;
    bool hasAnyUpdates(const transaction::Transaction* transaction, common::column_id_t columnID,
        common::row_idx_t startRow, common::length_t numRows) const;
    common::row_idx_t getNumDeletions(const transaction::Transaction* transaction,
        common::row_idx_t startRow, common::length_t numRows) const;
    bool hasVersionInfo() const { return versionInfo != nullptr; }

    void finalize() const;

    virtual void writeToColumnChunk(common::idx_t chunkIdx, common::idx_t vectorIdx,
        const std::vector<std::unique_ptr<ColumnChunk>>& data, ColumnChunk& offsetChunk) {
        KU_ASSERT(residencyState != ResidencyState::ON_DISK);
        chunks[chunkIdx]->getData().write(&data[vectorIdx]->getData(), &offsetChunk.getData(),
            common::RelMultiplicity::ONE);
    }

    virtual std::unique_ptr<ChunkedNodeGroup> flushAsNewChunkedNodeGroup(
        transaction::Transaction* transaction, BMFileHandle& dataFH) const;
    virtual void flush(BMFileHandle& dataFH);

    uint64_t getEstimatedMemoryUsage() const;

    virtual void serialize(common::Serializer& serializer) const;
    static std::unique_ptr<ChunkedNodeGroup> deserialize(common::Deserializer& deSer);

    template<class TARGET>
    TARGET& cast() {
        return common::ku_dynamic_cast<ChunkedNodeGroup&, TARGET&>(*this);
    }
    template<class TARGETT>
    const TARGETT& cast() const {
        return common::ku_dynamic_cast<const ChunkedNodeGroup&, const TARGETT&>(*this);
    }

protected:
    NodeGroupDataFormat format;
    ResidencyState residencyState;
    common::row_idx_t startRowIdx;
    uint64_t capacity;
    std::atomic<common::row_idx_t> numRows;
    std::vector<std::unique_ptr<ColumnChunk>> chunks;
    std::unique_ptr<VersionInfo> versionInfo;
};

} // namespace storage
} // namespace kuzu
