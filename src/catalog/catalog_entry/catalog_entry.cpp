#include "catalog/catalog_entry/catalog_entry.h"

#include "catalog/catalog_entry/scalar_macro_catalog_entry.h"
#include "catalog/catalog_entry/sequence_catalog_entry.h"
#include "catalog/catalog_entry/table_catalog_entry.h"
#include "catalog/catalog_entry/type_catalog_entry.h"
#include "common/serializer/deserializer.h"

namespace kuzu {
namespace catalog {

void CatalogEntry::serialize(common::Serializer& serializer) const {
    serializer.write(static_cast<uint8_t>(type));
    serializer.write(name);
    serializer.write(hasParent_);
}

std::unique_ptr<CatalogEntry> CatalogEntry::deserialize(common::Deserializer& deserializer) {
    CatalogEntryType type;
    std::string name;
    bool hasParent_;
    deserializer.deserializeValue(type);
    deserializer.deserializeValue(name);
    deserializer.deserializeValue(hasParent_);
    std::unique_ptr<CatalogEntry> entry;
    switch (type) {
    case CatalogEntryType::NODE_TABLE_ENTRY:
    case CatalogEntryType::REL_TABLE_ENTRY:
    case CatalogEntryType::REL_GROUP_ENTRY:
    case CatalogEntryType::RDF_GRAPH_ENTRY: {
        entry = TableCatalogEntry::deserialize(deserializer, type);
    } break;
    case CatalogEntryType::SCALAR_MACRO_ENTRY: {
        entry = ScalarMacroCatalogEntry::deserialize(deserializer);
    } break;
    case CatalogEntryType::SEQUENCE_ENTRY: {
        entry = SequenceCatalogEntry::deserialize(deserializer);
    } break;
    case CatalogEntryType::TYPE_ENTRY: {
        entry = TypeCatalogEntry::deserialize(deserializer);
    } break;
    default:
        KU_UNREACHABLE;
    }
    entry->type = type;
    entry->name = std::move(name);
    entry->hasParent_ = hasParent_;
    return entry;
}

void CatalogEntry::copyFrom(const CatalogEntry& other) {
    type = other.type;
    name = other.name;
    timestamp = other.timestamp;
    deleted = other.deleted;
    hasParent_ = other.hasParent_;
}

} // namespace catalog
} // namespace kuzu
