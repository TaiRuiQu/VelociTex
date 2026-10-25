// VelociTex HTML DOM tree implementation.
//
// Port of KaTeX 0.19.0 `src/domTree.ts` (markup serialisation) and
// `src/utils.ts` (escape/hyphenate helpers).
#include "domtree.hpp"

#include <cctype>
#include <cstdio>

#include "jsnum.hpp"
#include "../state.hpp"

namespace velocitex::html {


namespace {

// The bump arena that holds the DOM nodes of one render.
//
// The arena hands out blocks from a single buffer and is reset when the next
// render starts.  The nodes of the previous render are destroyed by then, so
// their memory can be reused.
// The buffers that the arena retires when it grows.  The list lives in an
// object of its own, so that the arena keeps a trivial destructor: a
// `thread_local` whose type has a destructor carries an initialization guard
// on every access, and the guard is a call into the runtime.  The list is
// only touched when the arena grows and when it is reset, which is rare.
VELOCITEX_REUSED_STATE(std::vector<char*>, g_retired_arena_buffers);

class NodeArena {
public:
    static NodeArena& Instance();
    void* Allocate(std::size_t size) {
        // The blocks are aligned so that every node keeps its alignment.
        const std::size_t aligned = (size + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
        if (used_ + aligned > capacity_) {
            Grow(aligned);
        }
        void* pointer = memory_ + used_;
        used_ += aligned;
        last_start_ = static_cast<char*>(pointer);
        last_size_ = aligned;
        return pointer;
    }
    // Writes `extra` bytes behind the content of the last block, if that
    // block is the one the caller holds.  `content` is the number of bytes
    // of the text; the terminator of the text is the first of the bytes that
    // are written behind it.
    char* ExtendLast(const char* data, std::size_t content, std::size_t extra) {
        if (data == nullptr || data != last_start_) {
            return nullptr;
        }
        const std::size_t needed = content + extra;
        if (needed <= last_size_) {
            // The padding of the block is enough.
            return const_cast<char*>(data) + content;
        }
        // The block has to grow.  Its end stays aligned, so the next block
        // keeps the alignment of the arena.
        const std::size_t aligned =
            (needed + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);
        const std::size_t offset = static_cast<std::size_t>(last_start_ - memory_);
        if (offset + aligned > capacity_) {
            return nullptr;
        }
        used_ = offset + aligned;
        last_size_ = aligned;
        return const_cast<char*>(data) + content;
    }
    void Reset() {
        // The blocks of the previous render are dead now, so the buffers that
        // the arena retired when it grew are released.  Without this the arena
        // would keep every buffer it ever had.
        for (char* buffer : g_retired_arena_buffers) {
            ::operator delete(buffer);
        }
        g_retired_arena_buffers.clear();
        used_ = 0;
        last_start_ = nullptr;
        last_size_ = 0;
    }

private:
    void Grow(std::size_t size) {
        std::size_t capacity = capacity_ != 0 ? capacity_ * 2 : 16384;
        if (capacity < size) {
            capacity = size;
        }
        // The previous buffer is retired instead of released: the nodes of the
        // current render live in it, and they are read while the render walks
        // the tree.  The retired buffers are released when the arena is reset.
        if (memory_ != nullptr) {
            g_retired_arena_buffers.push_back(memory_);
        }
        memory_ = static_cast<char*>(::operator new(capacity));
        capacity_ = capacity;
        used_ = 0;
        last_start_ = nullptr;
        last_size_ = 0;
    }
    // The members have no initializers, so that the type is trivially
    // constructible: a `thread_local` of such a type is zero initialized and
    // carries no initialization guard.
    char* memory_;
    std::size_t used_;
    std::size_t capacity_;
    // The block that was handed out last.  A text that is built by appending
    // can grow in place while it is the last block of the arena.
    char* last_start_;
    std::size_t last_size_;
};

// The arena of the nodes of one render.
VELOCITEX_REUSED_STATE(NodeArena, g_node_arena);

NodeArena& NodeArena::Instance() {
    // The arena is a namespace scope object, so it is initialized before the
    // module runs and an access to it does not pay for the initialization
    // guard of a `thread_local`.  Its type is trivially constructible and
    // trivially destructible, so the guard is not needed in the native build
    // either.
    return g_node_arena;
}

}  // namespace

void ArenaText::Assign(std::string_view text) {
    length_ = static_cast<uint32_t>(text.size());
    if (length_ == 0) {
        data_ = nullptr;
        return;
    }
    char* buffer = static_cast<char*>(AllocateNodeArena(length_ + 1));
    detail::CopyText(buffer, text.data(), length_);
    buffer[length_] = '\0';
    data_ = buffer;
}

void ArenaText::Append(std::string_view text) {
    if (text.empty()) {
        return;
    }
    // The text is grown in place when it is the last block of the arena,
    // which is the common case: the text of a class list is built by
    // appending a name to it.  The new text starts behind the old one, so
    // the terminator of the old text is the first byte that is written.
    char* tail = ExtendNodeArena(data_, length_, text.size() + 1);
    if (tail != nullptr) {
        detail::CopyText(tail, text.data(), text.size());
        tail[text.size()] = '\0';
        length_ += static_cast<uint32_t>(text.size());
        return;
    }
    const uint32_t total = length_ + static_cast<uint32_t>(text.size());
    char* buffer = static_cast<char*>(AllocateNodeArena(total + 1));
    if (length_ != 0) {
        detail::CopyText(buffer, data_, length_);
    }
    detail::CopyText(buffer + length_, text.data(), text.size());
    buffer[total] = '\0';
    data_ = buffer;
    length_ = total;
}

void ArenaText::Replace(size_t start, size_t length, std::string_view name) {
    const std::string_view text = View();
    const uint32_t total = static_cast<uint32_t>(text.size() - length + name.size());
    char* buffer = static_cast<char*>(AllocateNodeArena(total + 1));
    detail::CopyText(buffer, text.data(), start);
    detail::CopyText(buffer + start, name.data(), name.size());
    detail::CopyText(buffer + start + name.size(), text.data() + start + length, text.size() - start - length);
    buffer[total] = '\0';
    data_ = buffer;
    length_ = total;
}

void ArenaText::AppendWithSpace(std::string_view name) {
    // The text is grown in place when it is the last block of the arena.
    // The separator and the name are written behind the text, so the
    // terminator of the old text is the first byte that is written.
    char* tail = ExtendNodeArena(data_, length_, name.size() + 2);
    if (tail != nullptr) {
        tail[0] = ' ';
        detail::CopyText(tail + 1, name.data(), name.size());
        tail[1 + name.size()] = '\0';
        length_ += 1 + static_cast<uint32_t>(name.size());
        return;
    }
    const uint32_t total = length_ + 1 + static_cast<uint32_t>(name.size());
    char* buffer = static_cast<char*>(AllocateNodeArena(total + 1));
    if (length_ != 0) {
        detail::CopyText(buffer, data_, length_);
    }
    buffer[length_] = ' ';
    detail::CopyText(buffer + length_ + 1, name.data(), name.size());
    buffer[total] = '\0';
    data_ = buffer;
    length_ = total;
}

void ArenaText::Prepend(std::string_view name) {
    const uint32_t total = static_cast<uint32_t>(name.size()) + 1 + length_;
    char* buffer = static_cast<char*>(AllocateNodeArena(total + 1));
    detail::CopyText(buffer, name.data(), name.size());
    buffer[name.size()] = ' ';
    if (length_ != 0) {
        detail::CopyText(buffer + name.size() + 1, data_, length_);
    }
    buffer[total] = '\0';
    data_ = buffer;
    length_ = total;
}

std::string_view CopyToNodeArena(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    char* buffer = static_cast<char*>(AllocateNodeArena(text.size() + 1));
    detail::CopyText(buffer, text.data(), text.size());
    buffer[text.size()] = '\0';
    return std::string_view(buffer, text.size());
}

void ResetNodeArena() {
    NodeArena::Instance().Reset();
}

void* AllocateNodeArena(std::size_t size) {
    return NodeArena::Instance().Allocate(size);
}

char* ExtendNodeArena(const char* data, std::size_t used, std::size_t extra) {
    return NodeArena::Instance().ExtendLast(data, used, extra);
}

void* HtmlNode::operator new(std::size_t size) {
    return NodeArena::Instance().Allocate(size);
}

void* CssStyle::operator new(std::size_t size) {
    return NodeArena::Instance().Allocate(size);
}

void CssStyle::operator delete(void*) noexcept {
    // The arena owns the memory; it is released when the next render starts.
}

void CssStyle::operator delete(void*, std::size_t) noexcept {
}

void HtmlNode::operator delete(void*) noexcept {
    // The arena owns the memory; it is released when the next render starts.
}

void HtmlNode::operator delete(void*, std::size_t) noexcept {
}

std::string Hyphenate(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 4);
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (std::isupper(c)) {
            out.push_back('-');
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            out.push_back(raw);
        }
    }
    return out;
}


void CssStyle::Set(std::string_view key, std::string_view value) {
    // KaTeX keys the style declarations by their JavaScript name, so
    // "verticalAlign" and "vertical-align" are two different declarations.
    for (auto& entry : entries_) {
        if (SameStyleKey(entry.key, key)) {
            entry.value = value;
            entry.is_em = false;
            return;
        }
    }
    if (entries_.empty())
        entries_.reserve(4);
    Entry entry;
    entry.key = key;
    entry.value = value;
    entries_.push_back(std::move(entry));
}

void CssStyle::SetEm(std::string_view key, double em) {
    // The length is kept as a number; KaTeX's `makeEm` form is written when
    // the declaration reaches the markup, so a length never needs a string of
    // its own.
    for (auto& entry : entries_) {
        if (SameStyleKey(entry.key, key)) {
            entry.value.Clear();
            entry.em = em;
            entry.is_em = true;
            return;
        }
    }
    if (entries_.empty())
        entries_.reserve(4);
    Entry entry;
    entry.key = key;
    entry.em = em;
    entry.is_em = true;
    entries_.push_back(std::move(entry));
}

std::optional<std::string_view> CssStyle::Find(std::string_view key) const {
    for (const auto& entry : entries_) {
        if (SameStyleKey(entry.key, key) && !entry.is_em) {
            return entry.value.View();
        }
    }
    return std::nullopt;
}

void HtmlNode::SetAttribute(std::string_view name, std::string_view value) {
    for (auto& entry : attributes_) {
        if (entry.first == name) {
            entry.second = value;
            return;
        }
    }
    if (attributes_.empty())
        attributes_.reserve(2);
    attributes_.emplace_back(name, std::move(value));
}

void HtmlNode::SetAttributeEm(std::string_view name, double em) {
    // The length is formatted into a buffer on the stack and then copied into
    // the arena, so that the attribute does not need a string of its own and
    // the formatting does not ask the string library for a copy.
    StackText formatted;
    AppendEm(formatted, em);
    const std::string_view text = formatted.View();
    for (auto& entry : attributes_) {
        if (entry.first == name) {
            entry.second = text;
            return;
        }
    }
    if (attributes_.empty())
        attributes_.reserve(2);
    attributes_.emplace_back(name, ArenaText());
    attributes_.back().second = text;
}

std::optional<std::string_view> HtmlNode::Attribute(std::string_view name) const {
    for (const auto& entry : attributes_) {
        if (entry.first == name) {
            return entry.second.View();
        }
    }
    return std::nullopt;
}

void HtmlNode::AddClass(std::string_view name) {
    Classes.Add(name);
}

void HtmlSymbol::WriteBody(MarkupBuffer& out) const {
    // KaTeX renders a symbol without classes or styles as bare text.
    const bool has_style = italic_ > 0.0 || !Style.Empty();
    if (Classes.Empty() && !has_style) {
        detail::AppendEscaped(out, text_.View());
        return;
    }
    // The opening tag goes straight into the output.  A buffer of the stack
    // between the tag and the output would copy every byte of the tag twice,
    // and the tag is the largest fragment the writer produces.
    WriteOpenTag(out);
    out.push_back('>');
    detail::AppendEscaped(out, text_.View());
    out.append("</span>", 7);
}

void HtmlSpan::WriteBody(MarkupBuffer& out) const {
    WriteOpenTag(out);
    for (const auto& attribute : attributes_) {
        out.push_back(' ');
        out += attribute.first;
        out.append("=\"", 2);
        detail::AppendEscaped(out, attribute.second.View());
        out.push_back('"');
    }
    out.push_back('>');
    for (const auto& child : children_) {
        child->WriteMarkup(out);
    }
    out.append("</span>", 7);
}

void HtmlAnchor::WriteBody(MarkupBuffer& out) const {
    if (!Classes.Empty()) {
        out.append("<a class=\"", 10);
        detail::AppendClasses(out, Classes);
        if (!Style.Empty()) {
            out.append("\" style=\"", 9);
            Style.WriteTo(out);
            out.push_back('"');
        } else {
            out.push_back('"');
        }
    } else {
        out.append("<a", 2);
        if (!Style.Empty()) {
            out.append(" style=\"", 8);
            Style.WriteTo(out);
            out.push_back('"');
        }
    }
    for (const auto& attribute : attributes_) {
        out.push_back(' ');
        out += attribute.first;
        out.append("=\"", 2);
        detail::AppendEscaped(out, attribute.second.View());
        out.push_back('"');
    }
    out.push_back('>');
    for (const auto& child : children_) {
        child->WriteMarkup(out);
    }
    out.append("</a>", 4);
}

void HtmlFragment::WriteBody(MarkupBuffer& out) const {
    for (const auto& child : children_) {
        child->WriteMarkup(out);
    }
}

void HtmlPath::WriteBody(MarkupBuffer& out) const {
    out.append("<path d=\"", 9);
    detail::AppendEscaped(out, d_.View());
    out.append("\"/>", 3);
}

void HtmlLine::WriteBody(MarkupBuffer& out) const {
    out.append("<line", 5);
    for (const auto& attribute : attributes_) {
        out.push_back(' ');
        out += attribute.first;
        out.append("=\"", 2);
        detail::AppendEscaped(out, attribute.second.View());
        out.push_back('"');
    }
    out.append("/>", 2);
}

void HtmlSvg::WriteBody(MarkupBuffer& out) const {
    out.append("<svg", 4);
    for (const auto& attribute : attributes_) {
        out.push_back(' ');
        out += attribute.first;
        out.append("=\"", 2);
        detail::AppendEscaped(out, attribute.second.View());
        out.push_back('"');
    }
    out.push_back('>');
    for (const auto& child : children_) {
        child->WriteMarkup(out);
    }
    out.append("</svg>", 6);
}

void HtmlImg::WriteBody(MarkupBuffer& out) const {
    out.append("<img src=\"", 10);
    const std::optional<std::string_view> src = Attribute("src");
    if (src.has_value()) {
        detail::AppendEscaped(out, *src);
    }
    out.append("\" alt=\"", 7);
    const std::optional<std::string_view> alt = Attribute("alt");
    if (alt.has_value()) {
        detail::AppendEscaped(out, *alt);
    }
    out.push_back('"');
    if (!Style.Empty()) {
        out.append(" style=\"", 8);
        Style.WriteTo(out);
        out.push_back('"');
    }
    // KaTeX emits a stray apostrophe before the closing slash; keep it so the
    // generated markup stays byte-identical.
    out.append("'/>", 3);
}

}  // namespace velocitex::html
