#include "alloc_counter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

// Replace the global allocation functions with counting wrappers. Every form the
// standard library may pick is provided (throwing / nothrow, array, sized, and
// C++17 over-aligned) so the count is complete and the matching delete is always
// the counting one. Allocation itself is delegated to malloc/free (aligned via
// posix_memalign) — the point is the count, not a custom arena.

namespace {

std::atomic<std::uint64_t> g_alloc_count{0};
std::atomic<std::uint64_t> g_alloc_bytes{0};

void* counted_alloc(std::size_t size) {
  g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  g_alloc_bytes.fetch_add(size, std::memory_order_relaxed);
  // Never hand back nullptr for a zero-sized request (a valid, unique pointer is
  // required); one byte is enough and keeps free() well-defined.
  return std::malloc(size == 0 ? 1 : size);
}

void* counted_aligned_alloc(std::size_t size, std::size_t alignment) {
  g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  g_alloc_bytes.fetch_add(size, std::memory_order_relaxed);
  void* p = nullptr;
  // posix_memalign requires alignment to be a power of two and a multiple of
  // sizeof(void*); operator new's align_val_t always satisfies the former, and
  // we round the request size up so a zero size still returns a live pointer.
  const std::size_t align = alignment < sizeof(void*) ? sizeof(void*) : alignment;
  if (posix_memalign(&p, align, size == 0 ? align : size) != 0) {
    return nullptr;
  }
  return p;
}

}  // namespace

namespace microsim::bench {

std::uint64_t alloc_count() noexcept {
  return g_alloc_count.load(std::memory_order_relaxed);
}

std::uint64_t alloc_bytes() noexcept {
  return g_alloc_bytes.load(std::memory_order_relaxed);
}

void reset_alloc_stats() noexcept {
  g_alloc_count.store(0, std::memory_order_relaxed);
  g_alloc_bytes.store(0, std::memory_order_relaxed);
}

}  // namespace microsim::bench

// ----- throwing new -----------------------------------------------------------

void* operator new(std::size_t size) {
  void* p = counted_alloc(size);
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

void* operator new[](std::size_t size) {
  void* p = counted_alloc(size);
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

void* operator new(std::size_t size, std::align_val_t alignment) {
  void* p = counted_aligned_alloc(size, static_cast<std::size_t>(alignment));
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  void* p = counted_aligned_alloc(size, static_cast<std::size_t>(alignment));
  if (p == nullptr) {
    throw std::bad_alloc{};
  }
  return p;
}

// ----- nothrow new ------------------------------------------------------------

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return counted_alloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return counted_alloc(size);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  return counted_aligned_alloc(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  return counted_aligned_alloc(size, static_cast<std::size_t>(alignment));
}

// ----- delete (all forms free through the same path) --------------------------

void operator delete(void* p) noexcept {
  std::free(p);
}

void operator delete[](void* p) noexcept {
  std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
  std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
  std::free(p);
}

void operator delete(void* p, std::align_val_t) noexcept {
  std::free(p);
}

void operator delete[](void* p, std::align_val_t) noexcept {
  std::free(p);
}

void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
  std::free(p);
}

void operator delete(void* p, const std::nothrow_t&) noexcept {
  std::free(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept {
  std::free(p);
}

void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(p);
}

void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(p);
}
