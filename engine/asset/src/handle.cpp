#include "asset/handle.hpp"

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace ets {
namespace {

struct AssetHandleConverters {
    std::shared_mutex mutex;
    std::unordered_map<TypeId, AssetHandleConverter> values;
};

AssetHandleConverters& asset_handle_converters() {
    static AssetHandleConverters converters;
    return converters;
}

} // namespace

void register_asset_handle_converter(
    const TypeId handle_type,
    const AssetHandleConverter converter
) {
    auto& converters = asset_handle_converters();
    std::unique_lock lock(converters.mutex);
    converters.values.insert_or_assign(handle_type, converter);
}

Optional<UntypedHandle> convert_to_untyped_handle(const Ref handle) {
    if (!handle) {
        return nullopt;
    }
    auto& converters = asset_handle_converters();
    std::shared_lock lock(converters.mutex);
    const auto converter = converters.values.find(handle.type_id());
    if (converter == converters.values.end()) {
        return nullopt;
    }
    return converter->second(handle);
}

} // namespace ets
