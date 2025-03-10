#include "common/serializer/deserializer.h"

namespace kuzu {
namespace common {

template<>
void Deserializer::deserializeValue(std::string& value) {
    uint64_t valueLength = 0;
    deserializeValue(valueLength);
    value.resize(valueLength);
    reader->read((uint8_t*)value.data(), valueLength);
}

void Deserializer::validateDebuggingInfo(std::string& value, std::string expectedVal) {
#if defined(ENABLE_DESER_DEBUG) && (defined(KUZU_RUNTIME_CHECKS) || !defined(NDEBUG))
    deserializeValue<std::string>(value);
    KU_ASSERT(value == expectedVal);
#endif
    // DO NOTHING
    KU_UNUSED(value);
    KU_UNUSED(expectedVal);
}

} // namespace common
} // namespace kuzu
