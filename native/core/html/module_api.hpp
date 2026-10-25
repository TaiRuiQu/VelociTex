// Shared contract for the HTML renderer modules.
//
// Every KaTeX function module that the C++ core renders lives in its own
// translation unit and plugs into `Builder::BuildGroup` through one of the
// entry points declared here.  A module never touches `build_html.cpp`; it
// only receives the semantic tree and a callback that renders a child group
// with KaTeX's `buildGroup` semantics.
#pragma once

#include <functional>
#include <span>
#include <vector>

#include "../types.hpp"
#include "build_common.hpp"
#include "options.hpp"

namespace velocitex::html {

// Renders a child group, exactly like KaTeX's `html.buildGroup`.
using BuildGroupFn = std::function<HtmlNodePtr(int index, const HtmlOptions& options)>;

struct ModuleContext {
    std::span<const SemanticNode> semantic;
    const BuildGroupFn& build_group;

    const SemanticNode& Node(int index) const { return semantic[static_cast<size_t>(index)]; }

    IndexVector Children(int index) const {
        IndexVector result;
        if (index < 0) {
            return result;
        }
        // The children are counted before the list is filled, so that the
        // list is allocated once instead of at every step of its growth.
        size_t count = 0;
        for (int child = Node(index).FirstChild; child >= 0; child = Node(child).NextSibling) {
            ++count;
        }
        result.reserve(count);
        for (int child = Node(index).FirstChild; child >= 0; child = Node(child).NextSibling) {
            result.push_back(child);
        }
        return result;
    }
};

}  // namespace velocitex::html
