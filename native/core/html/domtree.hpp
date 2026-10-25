// VelociTex HTML DOM tree.
//
// A faithful port of the parts of KaTeX's `src/domTree.ts` that the HTML
// renderer needs.  The markup produced here is byte-for-byte identical to
// KaTeX's `toMarkup()` output, which is what the acceptance diff compares.
//
// Reference: KaTeX 0.19.0 `src/domTree.ts`
#pragma once
#include "html/markup.hpp"
#include "jsnum.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace velocitex::html {

class HtmlNode;

// The nodes of a render live in the arena of the nodes, and the arena is
// released when the next render starts.  The pointer therefore only carries
// the address: it never runs the destructor of the node, and the node does
// not own any memory of its own.
struct ArenaDeleter {
    template <class T>
    void operator()(T* node) const noexcept {
        (void)node;
    }
};

template <class T>
using NodePtr = std::unique_ptr<T, ArenaDeleter>;

using HtmlNodePtr = std::unique_ptr<HtmlNode, ArenaDeleter>;

// The allocator of the vectors that belong to the DOM tree of one render.
//
// The children of a node, the style declarations and the attributes all live
// as long as the tree does, and the tree is thrown away when the next render
// starts.  Handing their storage to the arena of the nodes therefore costs
// a pointer bump instead of a heap allocation, and the release of the tree
// does not have to walk the vectors at all.
template <class T>
struct ArenaAllocator {
    using value_type = T;

    ArenaAllocator() = default;
    template <class U>
    constexpr ArenaAllocator(const ArenaAllocator<U>&) noexcept {}

    T* allocate(std::size_t count);
    void deallocate(T* pointer, std::size_t count) noexcept;

    template <class U>
    bool operator==(const ArenaAllocator<U>&) const noexcept {
        return true;
    }
    template <class U>
    bool operator!=(const ArenaAllocator<U>&) const noexcept {
        return false;
    }
};

// Reserves `size` bytes in the arena that holds the DOM nodes of the render.
void* AllocateNodeArena(std::size_t size);

// Extends the text of a node in place.
//
// The text of a class list is built by appending a name to it, and the text
// is usually the last thing the arena handed out.  The arena can then write
// the new name behind the old one instead of copying the whole text into a
// new block.  `data` is the block of the text, `content` is the number of
// bytes of the text and `extra` is the number of bytes that are written
// behind it; the terminator of the text is the first of them.  The result is
// the position behind the text, or null when the text is not the last block
// of the arena or when the arena has no room left.
char* ExtendNodeArena(const char*data, std::size_t content, std::size_t extra);

// Copies a text into the arena of the nodes and returns a view of it.
//
// The class list of a node holds the names of the classes as views, so a name
// has to live as long as the render.  The names of the common classes are
// literals or entries of a table and live forever; a name that is built at
// run time has to be copied into the arena, which is released when the next
// render starts.
std::string_view CopyToNodeArena(std::string_view text);

template <class T>
T* ArenaAllocator<T>::allocate(std::size_t count) {
    if (count == 0) {
        return nullptr;
    }
    return static_cast<T*>(AllocateNodeArena(count * sizeof(T)));
}

template <class T>
void ArenaAllocator<T>::deallocate(T*, std::size_t) noexcept {
    // The arena releases the memory when the next render starts.
}

// The text of a node lives in the arena of the nodes.
//
// The nodes of a render are released as a whole when the next render starts,
// so the text of a node never has to be freed on its own.  A text that grows
// copies itself into the arena; the copies that are left behind cost a few
// hundred bytes per formula and are reused with the arena.
class ArenaText {
public:
    ArenaText() = default;
    ArenaText(std::string_view text) { Assign(text); }
    ArenaText(const char* text) { Assign(std::string_view(text)); }
    ArenaText& operator=(std::string_view text) {
        Assign(text);
        return *this;
    }
    ArenaText& operator=(const char* text) {
        Assign(std::string_view(text));
        return *this;
    }
    // Replaces a part of the text with another one.
    void Replace(size_t start, size_t length, std::string_view name);
    void Append(std::string_view text);
    // Appends a separator and a name.
    void AppendWithSpace(std::string_view name);
    // Puts a name in front of the text.
    void Prepend(std::string_view name);
    void Clear() { length_ = 0; }
    std::string_view View() const { return std::string_view(data_, length_); }
    size_t Size() const { return length_; }
    bool Empty() const { return length_ == 0; }
    const char* Data() const { return data_ != nullptr ? data_ : ""; }

private:
    void Assign(std::string_view text);

    char* data_ = nullptr;
    uint32_t length_ = 0;
};

// Builds a node in the arena of the nodes.
template <class T, class... Args>
NodePtr<T> MakeNode(Args&&... args) {
    return NodePtr<T>(new T(std::forward<Args>(args)...));
}

// The children of a DOM node.
using NodeVector = std::vector<HtmlNodePtr, ArenaAllocator<HtmlNodePtr>>;

// The indices of the children of a semantic node.
using IndexVector = std::vector<int, ArenaAllocator<int>>;

namespace detail {

// Copies a text of any length.
//
// `CopyShort` moves the head and the tail of the text, which covers every byte
// of a text of up to forty-eight bytes.  A longer text has a hole in the
// middle, so it goes through `memcpy`: the path data of an SVG is hundreds of
// bytes long, and the arena hands out the blocks one after the other, so the
// hole would show the bytes of the next block.
inline void CopyText(char* target, const char* source, size_t length) {
    if (length <= 48) {
        CopyShort(target, source, length);
        return;
    }
    std::memcpy(target, source, length);
}

// The characters that KaTeX escapes in text content.
struct EscapeTable {
    bool value[256]{};
    constexpr EscapeTable() {
        value[static_cast<unsigned char>('&')] = true;
        value[static_cast<unsigned char>('>')] = true;
        value[static_cast<unsigned char>('<')] = true;
        value[static_cast<unsigned char>('"')] = true;
        value[static_cast<unsigned char>('\'')] = true;
    }
};
inline constexpr EscapeTable kEscapeTable{};

// Writes the text of a node, escaping the characters KaTeX escapes.
//
// The helper lives in the header because the write of a node is inlined into
// the recursion that writes the tree, and the escaping is part of it.
inline void AppendEscaped(MarkupBuffer& out, std::string_view text) {
    // Most values contain nothing that has to be escaped, so the text is
    // copied in runs instead of one character at a time.
    size_t run = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char raw = text[i];
        if (!kEscapeTable.value[static_cast<unsigned char>(raw)]) {
            continue;
        }
        out.append(text.data() + run, i - run);
        switch (raw) {
            case '&': out.append("&amp;", 5); break;
            case '>': out.append("&gt;", 4); break;
            case '<': out.append("&lt;", 4); break;
            case '"': out.append("&quot;", 6); break;
            default: out.append("&#x27;", 6); break;
        }
        run = i + 1;
    }
    out.append(text.data() + run, text.size() - run);
}

}  // namespace detail


// Compares two style keys.
//
// The keys are the literal names KaTeX uses, so they are short.  The
// comparison of two `string_view`s calls into `memcmp`, and in WebAssembly
// that call is a byte loop, so the characters are compared here instead.
inline bool SameStyleKey(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

// Ordered CSS declaration list.  KaTeX stores styles in plain JS objects, so
// the declaration order is the insertion order; we keep the same guarantee by
// storing an ordered vector instead of a map.
//
// The keys are the literal names KaTeX uses.  They are views, so a
// declaration never copies its name.
class CssStyle {
public:
    // The style declarations of a node live as long as the tree does, so the
    // style object itself comes from the arena of the nodes.
    static void* operator new(std::size_t size);
    static void operator delete(void* pointer) noexcept;
    static void operator delete(void* pointer, std::size_t) noexcept;

    void Set(std::string_view key, std::string_view value);
    // Sets a length in `em`.  The number is kept as a number and is written
    // in KaTeX's `makeEm` form when the declaration reaches the markup, so a
    // length never needs a string of its own.
    void SetEm(std::string_view key, double em);
    std::optional<std::string_view> Find(std::string_view key) const;
    bool Empty() const { return entries_.empty(); }
    // Renders as "key:value;key:value;".  Appends the same text without
    // building an intermediate string.
    void WriteTo(MarkupBuffer& out) const;

private:
    struct Entry {
        // The literal name KaTeX uses for the declaration.
        std::string_view key;
        // The value of a declaration that is not a length.
        ArenaText value;
        // The length of a declaration that is given in `em`.
        double em{0.0};
        bool is_em{false};
    };

    // The declarations live in the arena of the nodes, so a styled node does
    // not ask the heap for the storage of its declarations.
    std::vector<Entry, ArenaAllocator<Entry>> entries_;
};

// The declarations of a style are written as "key:value;key:value;".
inline void CssStyle::WriteTo(MarkupBuffer& out) const {
    for (const auto& entry : entries_) {
        // The keys of the declarations are the CSS names already, so that the
        // markup does not have to hyphenate them while it is written.
        //
        // The colon is written with the key and the semicolon with the
        // value, so that a declaration costs one call into the buffer and not
        // four: every piece of the markup costs one call, and the markup of a
        // formula is a few hundred calls.
        out.AppendWrapped('\0', entry.key.data(), entry.key.size(), ':');
        if (entry.is_em) {
            MakeEmInto(out, entry.em, ';');
        } else {
            const std::string_view value = entry.value.View();
            out.AppendWrapped('\0', value.data(), value.size(), ';');
        }
    }
}

// The class list of a node.
//
// KaTeX keeps the classes in an array and joins them with a space when the
// node is written.  The list therefore holds the names themselves and not a
// joined text: a name is never copied into the arena, and the markup writer
// joins the names when it writes the attribute.
//
// The names are literals of the renderer, the entries of the size tables or
// the names of the script table, so they live as long as the render.
class ClassList {
public:
    // The number of classes a node can carry.  The widest list of the corpus
    // carries nine: the class of the atom, the three font classes, the three
    // sizing classes, the "mtight" class and the fallback class of a script.
    static constexpr size_t kCapacity = 12;

    ClassList() = default;
    // The copy and the move carry only the names that the list holds.  The
    // rest of the array is not initialized, and copying it would read
    // indeterminate values.
    ClassList(const ClassList& other) { CopyFrom(other); }
    ClassList(ClassList&& other) noexcept { CopyFrom(other); }
    ClassList& operator=(const ClassList& other) {
        CopyFrom(other);
        return *this;
    }
    ClassList& operator=(ClassList&& other) noexcept {
        CopyFrom(other);
        return *this;
    }
    // A list that is written as one text, "mord mathnormal".
    ClassList(std::string_view joined) { Assign(joined); }
    ClassList(std::initializer_list<std::string_view> classes) {
        for (const std::string_view name : classes) {
            Add(name);
        }
    }
    ClassList& operator=(std::initializer_list<std::string_view> classes) {
        count_ = 0;
        has_empty_ = false;
        for (const std::string_view name : classes) {
            Add(name);
        }
        return *this;
    }
    ClassList& operator=(std::string_view joined) {
        Assign(joined);
        return *this;
    }

    bool Empty() const { return count_ == 0; }
    // KaTeX's `canCombine` asks a class list for its size for every pair of
    // adjacent symbols, so the count is kept next to the names.
    size_t Size() const { return count_; }
    std::string_view At(size_t index) const {
        if (index >= count_) {
            return {};
        }
#ifdef VELOCITEX_CHECK_CLASS_NAMES
        VerifyName(index);
#endif
        return std::string_view(names_[index], lengths_[index]);
    }
    std::string_view Front() const { return At(0); }
    void SetAt(size_t index, std::string_view name);
    void SetFront(std::string_view name) { SetAt(0, name); }
    void Prepend(std::string_view name);
    void Add(std::string_view name);
    bool Has(std::string_view name) const;
    // The index of the class, or `npos` when the node does not carry it.
    size_t IndexOf(std::string_view name) const;
    // True when the list carries an empty class name.  KaTeX keeps the empty
    // names in its class list but drops them when it writes the attribute, so
    // the list has to remember them.
    bool HasEmptyName() const { return has_empty_; }

private:
    // Splits a joined text into its names.
    void Assign(std::string_view joined);

    // Takes the names of another list over.  Only the names that the other
    // list holds are read: the rest of the array is not initialized.
    void CopyFrom(const ClassList& other) {
        count_ = other.count_;
        has_empty_ = other.has_empty_;
        for (size_t i = 0; i < count_; ++i) {
            names_[i] = other.names_[i];
            lengths_[i] = other.lengths_[i];
#ifdef VELOCITEX_CHECK_CLASS_NAMES
            RememberName(i, std::string_view(names_[i], lengths_[i]));
#endif
        }
    }

    // The names of the list.  A name is a view of a literal or of a table
    // that lives as long as the render, so it is never copied.
    //
    // The names are not initialized: only the first `count_` of them are ever
    // read, and zeroing the whole array would cost a hundred bytes for every
    // class list of a formula.
    const char* names_[kCapacity];
    uint8_t lengths_[kCapacity];
    uint8_t count_{0};
    bool has_empty_{false};

#ifdef VELOCITEX_CHECK_CLASS_NAMES
    // The names of a class list are views: the caller has to hand over a name
    // that lives as long as the render.  A name that comes from a temporary
    // string dangles, and the renderer writes garbage into the class
    // attribute.  The build with `VELOCITEX_CHECK_CLASS_NAMES` keeps a copy
    // of every name and compares it with the bytes of the view when the name
    // is used, so a dangling name is reported instead of written out.
    static constexpr size_t kShadowBytes = 48;
    char shadow_[kCapacity][kShadowBytes]{};
    uint8_t shadow_lengths_[kCapacity]{};
    bool shadow_known_[kCapacity]{};

    void RememberName(size_t index, std::string_view name) {
        shadow_known_[index] = name.size() < kShadowBytes;
        shadow_lengths_[index] = static_cast<uint8_t>(name.size());
        if (shadow_known_[index]) {
            std::memcpy(shadow_[index], name.data(), name.size());
        }
    }

    void VerifyName(size_t index) const {
        if (!shadow_known_[index]) {
            return;
        }
        const size_t length = shadow_lengths_[index];
        const char* name = names_[index];
        for (size_t i = 0; i < length; ++i) {
            if (name[i] != shadow_[index][i]) {
                ReportDanglingName(std::string_view(shadow_[index], length));
                return;
            }
        }
    }

    // Reports a name whose bytes changed under the renderer.  The name is
    // reported once, so that one run of the corpus names every class that
    // dangles instead of the first one.
    static void ReportDanglingName(std::string_view expected) {
        static thread_local std::vector<std::string> reported;
        for (const std::string& name : reported) {
            if (name == expected) {
                return;
            }
        }
        reported.emplace_back(expected);
        std::fprintf(stderr, "DANGLING CLASS NAME: '%.*s'\n", static_cast<int>(expected.size()),
                     expected.data());
        std::fflush(stderr);
    }
#endif
};

// Compares two class names.
//
// The names are short, so a call into `memcmp` costs more than the
// comparison itself, and in WebAssembly it is a call into libc++.
inline bool SameClassName(const char* left, const char* right, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

inline void ClassList::Assign(std::string_view joined) {
    count_ = 0;
    has_empty_ = false;
    if (joined.empty()) {
        return;
    }
    size_t start = 0;
    for (size_t i = 0; i <= joined.size(); ++i) {
        if (i == joined.size() || joined[i] == ' ') {
            const size_t length = i - start;
            if (count_ < kCapacity) {
                names_[count_] = joined.data() + start;
                lengths_[count_] = static_cast<uint8_t>(length);
#ifdef VELOCITEX_CHECK_CLASS_NAMES
                RememberName(count_, std::string_view(names_[count_], length));
#endif
                ++count_;
            }
            if (length == 0) {
                has_empty_ = true;
            }
            start = i + 1;
        }
    }
}

inline void ClassList::Add(std::string_view name) {
#ifdef VELOCITEX_CHECK_CLASS_CAPACITY
    if (count_ >= kCapacity) {
        std::fprintf(stderr, "class list overflow\n");
        std::abort();
    }
    // The widest list of the corpus decides how many slots a class list needs.
    // The build with `VELOCITEX_CHECK_CLASS_CAPACITY` reports the width, so
    // that the capacity is not a guess.
    {
        static thread_local uint32_t widest = 0;
        const uint32_t width = count_ + 1;
        if (width > widest) {
            widest = width;
            std::fprintf(stderr, "class list width: %u\n", widest);
        }
    }
#endif
    // KaTeX keeps an empty class name in its list, but the list of a node
    // that carries no class at all stays empty.  The rule of the joined text
    // is kept: a name is counted when the list already holds a name or when
    // the name itself is not empty.
    if ((count_ != 0 || !name.empty()) && count_ < kCapacity) {
        names_[count_] = name.data();
        lengths_[count_] = static_cast<uint8_t>(name.size());
#ifdef VELOCITEX_CHECK_CLASS_NAMES
        RememberName(count_, name);
#endif
        ++count_;
    }
    if (name.empty()) {
        has_empty_ = true;
    }
}

inline void ClassList::Prepend(std::string_view name) {
    if (count_ >= kCapacity) {
        return;
    }
    for (size_t i = count_; i > 0; --i) {
        names_[i] = names_[i - 1];
        lengths_[i] = lengths_[i - 1];
#ifdef VELOCITEX_CHECK_CLASS_NAMES
        std::memcpy(shadow_[i], shadow_[i - 1], kShadowBytes);
        shadow_lengths_[i] = shadow_lengths_[i - 1];
        shadow_known_[i] = shadow_known_[i - 1];
#endif
    }
    names_[0] = name.data();
    lengths_[0] = static_cast<uint8_t>(name.size());
#ifdef VELOCITEX_CHECK_CLASS_NAMES
    RememberName(0, name);
#endif
    if (count_ != 0 || !name.empty()) {
        ++count_;
    }
    if (name.empty()) {
        has_empty_ = true;
    }
}

inline void ClassList::SetAt(size_t index, std::string_view name) {
    if (index >= count_) {
        return;
    }
    names_[index] = name.data();
    lengths_[index] = static_cast<uint8_t>(name.size());
#ifdef VELOCITEX_CHECK_CLASS_NAMES
    RememberName(index, name);
#endif
    if (name.empty()) {
        has_empty_ = true;
    }
}

inline bool ClassList::Has(std::string_view name) const {
    const size_t length = name.size();
    for (size_t i = 0; i < count_; ++i) {
#ifdef VELOCITEX_CHECK_CLASS_NAMES
        VerifyName(i);
#endif
        if (lengths_[i] == length && SameClassName(names_[i], name.data(), length)) {
            return true;
        }
    }
    return false;
}

inline size_t ClassList::IndexOf(std::string_view name) const {
    const size_t length = name.size();
    for (size_t i = 0; i < count_; ++i) {
#ifdef VELOCITEX_CHECK_CLASS_NAMES
        VerifyName(i);
#endif
        if (lengths_[i] == length && SameClassName(names_[i], name.data(), length)) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

namespace detail {

// Writes the class list the way KaTeX's `createClass` does, without building
// an intermediate string.
inline void AppendClasses(MarkupBuffer& out, const ClassList& classes) {
    // KaTeX drops the empty class names when it writes the attribute.
    const size_t count = classes.Size();
    bool first = true;
    for (size_t i = 0; i < count; ++i) {
        const std::string_view name = classes.At(i);
        if (name.empty()) {
            continue;
        }
        // The space and the name are written in one piece: every piece of the
        // markup costs one call into the buffer, and the markup of a formula
        // is a few hundred calls.
        if (first) {
            out.Append(name.data(), name.size());
        } else {
            out.AppendWrapped(' ', name.data(), name.size(), '\0');
        }
        first = false;
    }
}

}  // namespace detail

enum class HtmlNodeKind { Symbol, Span, Anchor, Fragment, Svg, Path, Line, Img };

// Releases the memory of the nodes of the previous render.  It is called at
// the start of every render, so the nodes never outlive their arena.
void ResetNodeArena();

class HtmlNode {
public:
    // The node is owned by the arena of the nodes, so it is never deleted
    // and it does not need a virtual destructor.
    ~HtmlNode() = default;

    // The nodes of one render come from a bump arena that is reset when the
    // next render starts.  A formula builds a few hundred nodes, and one
    // heap allocation per node costs more than the node itself; the arena
    // also keeps the tree contiguous, which the serialization walks.
    //
    // The nodes must not outlive the render that created them.
    static void* operator new(std::size_t size);
    static void operator delete(void* pointer) noexcept;
    static void operator delete(void* pointer, std::size_t) noexcept;

    // The kind of the node.  It is a plain field and not a virtual function:
    // a formula writes a few hundred nodes, and the call through a vtable
    // costs more than the switch that replaces it.
    HtmlNodeKind Kind() const { return kind_; }

    // Writes the node and its children.  The switch on the kind lets the
    // compiler inline the bodies of the common kinds into the recursion.
    void WriteMarkup(MarkupBuffer& out) const;

    const NodeVector& Children() const { return children_; }
    NodeVector& Children() { return children_; }

    void SetAttribute(std::string_view name, std::string_view value);
    // Writes a KaTeX `makeEm` length into the attribute without a temporary
    // string.
    void SetAttributeEm(std::string_view name, double em);
    std::optional<std::string_view> Attribute(std::string_view name) const;
    // The renderer asks a node for several classes, and the names are
    // constants at the call sites, so the lookup is inlined.
    bool HasClass(std::string_view name) const { return Classes.Has(name); }
    void AddClass(std::string_view name);

    // KaTeX's per-node size bookkeeping, used by makeVList and friends.
    double Height{0.0};
    double Depth{0.0};
    double MaxFontSize{0.0};
    double Width{0.0};
    bool HasWidth{false};
    // Italic correction carried from a SymbolNode into a vlist.
    double Italic{0.0};

    ClassList Classes;
    CssStyle Style;

protected:
    explicit HtmlNode(HtmlNodeKind kind) : kind_(kind) {}

private:
    HtmlNodeKind kind_;
#ifdef VELOCITEX_PAD_NODES
    // The probe of the footprint: the node is padded, so that the tree of one
    // render does not fit into the cache of the machine.  The member is not
    // initialized, so the probe does not add a write per node.
    char padding[128];
#endif

protected:
    NodeVector children_;
    // The attribute names are the literal names KaTeX uses, so they are views
    // and never copied.
    std::vector<std::pair<std::string_view, ArenaText>,
                ArenaAllocator<std::pair<std::string_view, ArenaText>>>
        attributes_;
};

class HtmlSymbol final : public HtmlNode {
public:
    explicit HtmlSymbol(std::string_view text) : HtmlNode(HtmlNodeKind::Symbol), text_(text) {}
    void WriteBody(MarkupBuffer& out) const;
    // Writes the opening tag of the node.
    //
    // The pieces of the tag are merged as far as the byte order allows:
    // every piece costs one call into the buffer, and the markup of a
    // formula is a few hundred calls.
    void WriteOpenTag(MarkupBuffer& out) const {
        const bool has_style = italic_ > 0.0 || !Style.Empty();
        if (!Classes.Empty()) {
            out.append("<span class=\"", 13);
            detail::AppendClasses(out, Classes);
            if (has_style) {
                out.append("\" style=\"", 9);
                WriteStyle(out);
                out.push_back('"');
            } else {
                out.push_back('"');
            }
        } else {
            out.append("<span", 5);
            if (has_style) {
                out.append(" style=\"", 8);
                WriteStyle(out);
                out.push_back('"');
            }
        }
    }

    std::string_view Text() const { return text_.View(); }
    void AppendText(std::string_view text) { text_.Append(text); }
    void SetItalic(double v) { italic_ = v; }
    void SetSkew(double v) { skew_ = v; }
    void SetWidth(double v) {
        width_ = v;
        has_width_ = true;
    }
    double ItalicValue() const { return italic_; }
    double SkewValue() const { return skew_; }
    double WidthValue() const { return width_; }
    bool HasWidthValue() const { return has_width_; }

private:
    ArenaText text_;
    double italic_{0.0};
    double skew_{0.0};
    double width_{0.0};
    bool has_width_{false};

    // Writes the declarations of the style attribute: the italic correction
    // of the symbol and the declarations of the node.
    void WriteStyle(MarkupBuffer& out) const {
        if (italic_ > 0.0) {
            out.append("margin-right:", 13);
            MakeEmInto(out, italic_, ';');
        }
        Style.WriteTo(out);
    }
};

class HtmlSpan final : public HtmlNode {
public:
    HtmlSpan() : HtmlNode(HtmlNodeKind::Span) {}
    explicit HtmlSpan(ClassList&& classes) : HtmlNode(HtmlNodeKind::Span) { Classes = std::move(classes); }
    void WriteBody(MarkupBuffer& out) const;
    // Writes the opening tag of the node, see `HtmlSymbol::WriteOpenTag`.
    void WriteOpenTag(MarkupBuffer& out) const {
        if (!Classes.Empty()) {
            out.append("<span class=\"", 13);
            detail::AppendClasses(out, Classes);
            if (!Style.Empty()) {
                out.append("\" style=\"", 9);
                Style.WriteTo(out);
                out.push_back('"');
            } else {
                out.push_back('"');
            }
        } else {
            out.append("<span", 5);
            if (!Style.Empty()) {
                out.append(" style=\"", 8);
                Style.WriteTo(out);
                out.push_back('"');
            }
        }
    }
};

class HtmlAnchor final : public HtmlNode {
public:
    HtmlAnchor(std::string href, ClassList&& classes) : HtmlNode(HtmlNodeKind::Anchor) {
        Classes = std::move(classes);
        SetAttribute("href", std::move(href));
    }
    void WriteBody(MarkupBuffer& out) const;
};

class HtmlFragment final : public HtmlNode {
public:
    HtmlFragment() : HtmlNode(HtmlNodeKind::Fragment) {}
    void WriteBody(MarkupBuffer& out) const;
};

class HtmlPath final : public HtmlNode {
public:
    // The path data lives in the arena of the nodes and not in a string of
    // its own: the nodes are owned by the arena and are never destructed, so
    // a string inside a node would never release its buffer.
    explicit HtmlPath(std::string_view d) : HtmlNode(HtmlNodeKind::Path), d_(d) {}
    void WriteBody(MarkupBuffer& out) const;

private:
    ArenaText d_;
};

class HtmlLine final : public HtmlNode {
public:
    HtmlLine() : HtmlNode(HtmlNodeKind::Line) {}
    void WriteBody(MarkupBuffer& out) const;
};

class HtmlSvg final : public HtmlNode {
public:
    HtmlSvg() : HtmlNode(HtmlNodeKind::Svg) {
        SetAttribute("xmlns", "http://www.w3.org/2000/svg");
    }
    void WriteBody(MarkupBuffer& out) const;
};

class HtmlImg final : public HtmlNode {
public:
    HtmlImg(std::string src, std::string alt) : HtmlNode(HtmlNodeKind::Img) {
        SetAttribute("src", std::move(src));
        SetAttribute("alt", std::move(alt));
    }
    void WriteBody(MarkupBuffer& out) const;
};

inline void HtmlNode::WriteMarkup(MarkupBuffer& out) const {
    // A span is by far the most common node, so it is checked first.
    if (kind_ == HtmlNodeKind::Span) {
        static_cast<const HtmlSpan*>(this)->WriteBody(out);
        return;
    }
    if (kind_ == HtmlNodeKind::Symbol) {
        static_cast<const HtmlSymbol*>(this)->WriteBody(out);
        return;
    }
    switch (kind_) {
        case HtmlNodeKind::Anchor: static_cast<const HtmlAnchor*>(this)->WriteBody(out); break;
        case HtmlNodeKind::Fragment: static_cast<const HtmlFragment*>(this)->WriteBody(out); break;
        case HtmlNodeKind::Svg: static_cast<const HtmlSvg*>(this)->WriteBody(out); break;
        case HtmlNodeKind::Path: static_cast<const HtmlPath*>(this)->WriteBody(out); break;
        case HtmlNodeKind::Line: static_cast<const HtmlLine*>(this)->WriteBody(out); break;
        case HtmlNodeKind::Img: static_cast<const HtmlImg*>(this)->WriteBody(out); break;
        case HtmlNodeKind::Symbol:
        case HtmlNodeKind::Span: break;
    }
}

// Utility helpers shared with the rest of the renderer.
std::string Hyphenate(std::string_view text);

}  // namespace velocitex::html
