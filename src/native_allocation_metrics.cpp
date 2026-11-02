#include "native_allocation_metrics.hpp"

#include <atomic>
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#endif
#include <new>

namespace {
std::atomic<std::uint64_t> calls{0};
std::atomic<std::uint64_t> bytes{0};

void record(std::size_t size) noexcept {
    calls.fetch_add(1, std::memory_order_relaxed);
    bytes.fetch_add(size, std::memory_order_relaxed);
}

void *allocate_aligned(std::size_t size, std::size_t alignment) noexcept {
#ifdef _WIN32
    return _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
    void *pointer = nullptr;
    return posix_memalign(&pointer, alignment, size == 0 ? 1 : size) == 0 ? pointer : nullptr;
#endif
}

void free_aligned(void *pointer) noexcept {
#ifdef _WIN32
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
} // namespace

namespace ayther::runtime::allocation_metrics {
Snapshot snapshot() noexcept {
    return {calls.load(std::memory_order_relaxed), bytes.load(std::memory_order_relaxed)};
}
} // namespace ayther::runtime::allocation_metrics

void *operator new(std::size_t size) {
    if (void *result = std::malloc(size == 0 ? 1 : size)) {
        record(size);
        return result;
    }
    throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }

void *operator new(std::size_t size, std::align_val_t alignment) {
    if (void *result = allocate_aligned(size, static_cast<std::size_t>(alignment))) {
        record(size);
        return result;
    }
    throw std::bad_alloc{};
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void *pointer, std::align_val_t) noexcept { free_aligned(pointer); }
void operator delete[](void *pointer, std::align_val_t) noexcept { free_aligned(pointer); }
void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
    free_aligned(pointer);
}
void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
    free_aligned(pointer);
}
