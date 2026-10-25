// Pooled allocation for the WebAssembly build.
//
// The C++ core renders a formula by building a short lived tree of DOM nodes
// and then serialising it.  Every node, every class list and every style
// entry costs one heap allocation, and the WebAssembly allocator is roughly
// three times slower than the native one.  Recycling the blocks through a
// thread local pool removes that cost and keeps the memory hot in the cache.
//
// The pool is only compiled into the WebAssembly module.  The shared library
// that the .NET endpoint loads must not replace the global `operator new` of
// the host process, so the native build keeps the system allocator.
//
// `VELOCITEX_POOL_ALLOC` compiles the same pool into a native build.  It is
// only used to profile the native core the way the WebAssembly module runs;
// the shipped native library never defines it.
#if defined(__EMSCRIPTEN__) || defined(VELOCITEX_POOL_ALLOC)

#include <cstddef>
#include <cstdlib>
#include <new>

#include "state.hpp"

namespace {

// The number of blocks the pool served.  Only the profiling build keeps
// track of it; the shipped module does not pay for the counter.
unsigned long long g_alloc_count = 0;

// The block sizes the pool hands out.  Every allocation is rounded up to the
// next power of two, so a free block can be reused for any request of the
// same class.
constexpr std::size_t kSmallestBlock = 32;
constexpr std::size_t kLargestBlock = 32768;
constexpr int kClassCount = 11;  // 32, 64, ..., 32768

// The pool keeps its free blocks in a per size class list.  The list head is
// stored in the first bytes of the block itself.
struct FreeBlock {
    FreeBlock* next;
    unsigned int klass;
};

constexpr std::size_t kChunkSize = 1 << 18;

class Pool {
public:
    Pool() = default;

    static Pool& Instance();

    void* Allocate(std::size_t size) {
        ++g_alloc_count;
        const int klass = ClassIndex(size);
        if (klass < 0) {
            // Large blocks are served by the system allocator.  The header
            // records the class so that the matching delete can find it.
            FreeBlock* header = static_cast<FreeBlock*>(std::malloc(sizeof(FreeBlock) + size));
            if (header == nullptr) throw std::bad_alloc();
            header->klass = static_cast<unsigned int>(-1);
            return header + 1;
        }
        FreeBlock*& head = free_lists_[klass];
        if (head != nullptr) {
            FreeBlock* block = head;
            head = block->next;
            block->klass = static_cast<unsigned int>(klass);
            return block + 1;
        }
        const std::size_t block_size = kSmallestBlock << klass;
        if (used_ + block_size > chunk_size_) {
            chunk_ = static_cast<char*>(std::malloc(kChunkSize));
            if (chunk_ == nullptr) throw std::bad_alloc();
            used_ = 0;
            chunk_size_ = kChunkSize;
        }
        FreeBlock* header = reinterpret_cast<FreeBlock*>(chunk_ + used_);
        used_ += block_size;
        header->klass = static_cast<unsigned int>(klass);
        return header + 1;
    }

    void Deallocate(void* pointer) noexcept {
        if (pointer == nullptr) return;
        FreeBlock* header = reinterpret_cast<FreeBlock*>(pointer) - 1;
        const int klass = static_cast<int>(header->klass);
        if (klass < 0) {
            std::free(header);
            return;
        }
        header->next = free_lists_[klass];
        free_lists_[klass] = header;
    }

private:
    // Returns the size class of a request, or -1 when the request is too big
    // for the pool.  The payload of a block starts behind the header, so the
    // header size is part of the request.
    static int ClassIndex(std::size_t size) {
        const std::size_t total = size + sizeof(FreeBlock);
        if (total > kLargestBlock) {
            return -1;
        }
        if (total <= kSmallestBlock) {
            return 0;
        }
        // ceil(log2(total)) - log2(kSmallestBlock), computed without a loop.
        const unsigned int bits = 32u - static_cast<unsigned int>(__builtin_clz(
            static_cast<unsigned int>(total - 1)));
        int klass = static_cast<int>(bits) - 5;
        if (klass < 0) {
            klass = 0;
        }
        if (klass >= kClassCount) {
            klass = kClassCount - 1;
        }
        return klass;
    }

    FreeBlock* free_lists_[kClassCount];
    char* chunk_;
    std::size_t used_;
    std::size_t chunk_size_;
};

// The pool of the thread that renders.  It is a namespace scope object, so an
// access to it does not pay for the initialization guard of a `thread_local`:
// the guard is a call into the runtime, and the pool is asked for a block on
// every allocation.  The type has no initializer of its own, so a thread
// local of it is zero initialized and the guard is not needed either.
VELOCITEX_REUSED_STATE(Pool, g_pool);

Pool& Pool::Instance() { return g_pool; }

}  // namespace

void* operator new(std::size_t size) {
    void* pointer = Pool::Instance().Allocate(size);
    if (pointer == nullptr) throw std::bad_alloc();
    return pointer;
}

void* operator new[](std::size_t size) { return operator new(size); }

void operator delete(void* pointer) noexcept { Pool::Instance().Deallocate(pointer); }
void operator delete[](void* pointer) noexcept { Pool::Instance().Deallocate(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { Pool::Instance().Deallocate(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { Pool::Instance().Deallocate(pointer); }

#endif  // __EMSCRIPTEN__ || VELOCITEX_POOL_ALLOC

#if defined(VELOCITEX_POOL_ALLOC)
namespace velocitex {
// Returns the number of blocks the pool served so far.  The counter is only
// compiled into the profiling build; it lets the benchmark report how many
// blocks a phase needs without changing the behaviour of the shipped module.
unsigned long long alloc_count_get() { return g_alloc_count; }
}  // namespace velocitex
#endif
