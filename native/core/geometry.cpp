#include "geometry.hpp"

#include "layout_support.hpp"

#include <map>
#include <mutex>
namespace velocitex {
#include "RadicalPathData.inc"
#include "StretchyPathData.inc"
namespace MathPathCache {
template <class Key, class Create>
std::vector<MathPathSegment> cached(const Key& key, Create create) {
    static std::mutex mutex;
    static std::map<Key, std::vector<MathPathSegment>> cache;
    std::lock_guard lock(mutex);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;
    if (cache.size() > 512)
        cache.clear();
    auto value = create();
    cache.emplace(key, value);
    return value;
}
std::vector<MathPathSegment> Get(std::u16string_view name) {
    return cached(std::u16string(name), [&] { return StretchyPathData::Get(std::u16string(name)); });
}
std::vector<MathPathSegment> TallPath(std::u16string_view label, int64_t height) {
    return cached(std::pair(std::u16string(label), height),
                  [&] { return StretchyPathData::Tall(std::u16string(label), height / 1000.0); });
}
std::vector<MathPathSegment> InnerPath(int cp, int64_t height) {
    return cached(std::pair(cp, height), [&] { return StretchyPathData::Inner(cp, height / 1000.0); });
}
std::vector<MathPathSegment> Radical(double height) {
    auto h = int64_t(std::nearbyint(height));
    return cached(h, [&] { return RadicalPathData::Tall(double(h)); });
}
std::vector<MathPathSegment> RadicalWithRule(int size, double extra, double height) {
    auto e = int64_t(std::nearbyint(extra * 1000000)), h = int64_t(std::nearbyint(height));
    return cached(std::tuple(size, e, h), [&] { return RadicalPathData::WithRule(size, e / 1000000.0, double(h)); });
}
} // namespace MathPathCache
} // namespace velocitex
