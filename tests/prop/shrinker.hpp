#pragma once

/// \file
/// Scenario shrinker for the property suite (task R1-18, TEST_STRATEGY.md). Given
/// a failing message stream and a predicate that says whether a stream still
/// fails, it produces a much smaller stream that still fails — the case that gets
/// printed and graduates to a committed fixture. Two passes:
///   1. prefix bisection — the shortest failing prefix (a failure usually needs
///      only the messages up to the offending one),
///   2. deletion passes — remove chunks (halving the chunk size each round) and
///      keep any removal that still fails, to fixpoint.
/// The predicate must be deterministic (our engine is), so shrinking is stable.

#include <cstddef>
#include <utility>
#include <vector>

#include "microsim/core/messages.hpp"

namespace microsim::prop {

/// Shrink `scenario` to a minimal sub-stream for which `fails` is still true.
/// `fails` is called on candidate streams; it must return true iff that stream
/// reproduces the failure.
template <class FailsPredicate>
std::vector<microsim::core::Inbound> shrink(std::vector<microsim::core::Inbound> scenario,
                                            FailsPredicate fails) {
  using Stream = std::vector<microsim::core::Inbound>;

  // Pass 1: shortest failing prefix via binary search on the length.
  {
    std::size_t lo = 0;
    std::size_t hi = scenario.size();
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      Stream prefix(scenario.begin(), scenario.begin() + static_cast<std::ptrdiff_t>(mid));
      if (fails(prefix)) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    if (lo < scenario.size()) {
      scenario.resize(lo);
    }
  }

  // Pass 2: chunked deletion to fixpoint. Halve the chunk each round so large
  // useless spans go first, then single messages.
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t chunk = scenario.size() / 2 + 1; chunk >= 1; chunk = chunk / 2) {
      std::size_t i = 0;
      while (i < scenario.size()) {
        Stream candidate;
        candidate.reserve(scenario.size());
        candidate.insert(candidate.end(), scenario.begin(),
                         scenario.begin() + static_cast<std::ptrdiff_t>(i));
        const std::size_t drop_end = (i + chunk < scenario.size()) ? i + chunk : scenario.size();
        candidate.insert(candidate.end(), scenario.begin() + static_cast<std::ptrdiff_t>(drop_end),
                         scenario.end());
        if (candidate.size() < scenario.size() && fails(candidate)) {
          scenario = std::move(candidate);
          changed = true;  // keep i: try the next chunk at this position
        } else {
          i += chunk;
        }
      }
      if (chunk == 1) {
        break;
      }
    }
  }
  return scenario;
}

}  // namespace microsim::prop
