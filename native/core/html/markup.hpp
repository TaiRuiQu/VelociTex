#pragma once
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace velocitex::html {

namespace detail {

// Copies a text of an arbitrary length with moves of a constant size.
//
// The sizes of the moves are constants, so the compiler turns them into loads
// and stores and the copy does not call into the C library.  A call to
// `memcpy` with a length that is not known at compile time costs more in the
// WebAssembly build than the copy itself, and the markup writer makes a few
// hundred of them per formula: every class name of a node is written on its
// own, and every opening tag is copied out of the buffer of the stack.
//
// The loop reads exactly `length` bytes and writes exactly `length` bytes, so
// nothing outside of the text is touched.
inline void CopyBytes(char* target, const char* text, size_t length) {
    while (length >= 8) {
        std::memcpy(target, text, 8);
        target += 8;
        text += 8;
        length -= 8;
    }
    if (length >= 4) {
        std::memcpy(target, text, 4);
        target += 4;
        text += 4;
        length -= 4;
    }
    if (length >= 2) {
        std::memcpy(target, text, 2);
        target += 2;
        text += 2;
        length -= 2;
    }
    if (length >= 1) {
        target[0] = text[0];
    }
}

// Copies a short text with two overlapping moves.
//
// The moves overlap, so that the first one covers the head and the last one
// the tail; none of them reads or writes outside the text.
//
// The moves have a constant size, so the compiler turns them into loads and
// stores.  A loop over the bytes would be turned into a call into the C
// library instead: the compiler recognises the pattern and replaces it with a
// `memcpy` whose length is only known at run time, and such a call costs more
// in the WebAssembly build than the copy itself.  The text of the shortest
// fragments (the last digit of a fraction, a class name of three letters) is
// copied a few hundred times per formula, so the path matters.
inline void CopyShort(char* target, const char* text, size_t length) {
    if (length >= 8) {
        std::memcpy(target, text, 8);
        std::memcpy(target + length - 8, text + length - 8, 8);
        if (length > 16) {
            std::memcpy(target + 8, text + 8, 8);
            std::memcpy(target + length - 16, text + length - 16, 8);
            if (length > 24) {
                std::memcpy(target + 16, text + 16, 8);
                std::memcpy(target + length - 24, text + length - 24, 8);
            }
        }
    } else if (length >= 4) {
        std::memcpy(target, text, 4);
        std::memcpy(target + length - 4, text + length - 4, 4);
    } else if (length >= 2) {
        std::memcpy(target, text, 2);
        if (length > 2) {
            target[2] = text[2];
        }
    } else if (length == 1) {
        target[0] = text[0];
    }
}

}  // namespace detail

// A buffer for the generated markup.
//
// A formula turns into a few hundred short fragments: every node writes its
// tag, its class list and its style declarations in pieces.  The standard
// string appends call into the allocator's `memmove` for every one of those
// fragments, and in the WebAssembly build that call costs several times more
// than the copy itself.  This buffer copies the short fragments inline and
// only calls `memmove` for the long ones.
//
// The buffer owns its storage and is not copyable.
class MarkupBuffer {
public:
    MarkupBuffer() = default;
    ~MarkupBuffer() { delete[] data_; }
    MarkupBuffer(const MarkupBuffer&) = delete;
    MarkupBuffer& operator=(const MarkupBuffer&) = delete;

    void Clear() { length_ = 0; }
    void Reserve(size_t capacity) {
        if (capacity <= capacity_) {
            return;
        }
        char* fresh = new char[capacity];
        if (length_ > 0) {
            std::memcpy(fresh, data_, length_);
        }
        delete[] data_;
        data_ = fresh;
        capacity_ = capacity;
    }

    void Append(char c) {
        if (length_ + 1 > capacity_) {
            Grow(1);
        }
        data_[length_++] = c;
    }
    void Append(const char* text, size_t length) {
        if (length == 0) {
            return;
        }
        // The growth of the buffer is rare, so it lives in a function of its
        // own: what is left is small enough to be inlined into the caller,
        // and the markup writer calls it a few hundred times per formula.
        if (length_ + length > capacity_) {
            Grow(length);
        }
        char* target = data_ + length_;
        // The fragments are copied with wide stores instead of one store per
        // byte.  The copies of a fragment overlap, so that the first one
        // covers the head and the last one the tail; none of them reads or
        // writes outside the fragment.
        //
        // The short path is deliberately not split off into a function of its
        // own: a version with a sixteen byte fast path and a call for the
        // rest measured faster natively (+1.3% to +1.9%) and faster in the
        // WebAssembly build of Node (+0.5% to +5.8%), but **slower in the
        // browser** (-1.5% on the fastest round, -4.7% on the median round).
        // The browser is the place the gate runs, so the split was reverted.
        CopyFragment(target, text, length);
        length_ += length;
    }

    // Writes a text with a byte before it and a byte after it, in one call.
    // A byte of '\0' means that there is none.
    //
    // The class list and the style declarations are written this way.  Every
    // piece of the markup costs one call into the buffer, and the markup of
    // a formula is a few hundred calls: the class list of a node alone is
    // two calls per name (the space and the name), and the space is folded
    // into the copy of the name here.
    void AppendWrapped(char before, const char* text, size_t length, char after) {
        const size_t total = length + (before != '\0' ? 1u : 0u) + (after != '\0' ? 1u : 0u);
        if (length_ + total > capacity_) {
            Grow(total);
        }
        char* target = data_ + length_;
        size_t offset = 0;
        if (before != '\0') {
            target[offset++] = before;
        }
        CopyFragment(target + offset, text, length);
        offset += length;
        if (after != '\0') {
            target[offset] = after;
        }
        length_ += total;
    }

    void Append(std::string_view text) { Append(text.data(), text.size()); }
    void Append(const std::string& text) { Append(text.data(), text.size()); }

    // The names of the standard string interface, so that the same code can
    // write into either buffer.
    void push_back(char c) { Append(c); }
    void append(const char* text, size_t length) { Append(text, length); }
    void append(std::string_view text) { Append(text); }
    void append(std::string_view text, size_t position, size_t length) {
        Append(text.data() + position, length);
    }
    MarkupBuffer& operator+=(char c) {
        Append(c);
        return *this;
    }
    MarkupBuffer& operator+=(const char* text) {
        Append(text, std::strlen(text));
        return *this;
    }
    MarkupBuffer& operator+=(std::string_view text) {
        Append(text);
        return *this;
    }
    MarkupBuffer& operator+=(const std::string& text) {
        Append(text);
        return *this;
    }

    const char* data() const { return data_ != nullptr ? data_ : ""; }
    size_t size() const { return length_; }
    bool empty() const { return length_ == 0; }
    std::string_view view() const { return std::string_view(data(), length_); }
    void AssignTo(std::string& target) const { target.assign(data(), length_); }
    // Replaces the content with the given text.
    void Assign(std::string_view text) {
        Clear();
        Append(text);
    }

private:
    // Copies a fragment with wide stores.  The short fragments are the
    // common case, and they are covered by two overlapping moves.
    static void CopyFragment(char* target, const char* text, size_t length) {
        if (length <= 48) {
            detail::CopyShort(target, text, length);
        } else {
            detail::CopyBytes(target, text, length);
        }
    }

    // Makes room for `extra` more bytes.  The buffer grows in doubles, so a
    // formula that is rendered again finds the room it needs.
    [[gnu::noinline]] void Grow(size_t extra) {
        size_t capacity = capacity_ != 0 ? capacity_ * 2 : 4096;
        if (capacity < length_ + extra) {
            capacity = length_ + extra;
        }
        Reserve(capacity);
    }

    char* data_ = nullptr;
    size_t length_ = 0;
    size_t capacity_ = 0;
};

}  // namespace velocitex::html
