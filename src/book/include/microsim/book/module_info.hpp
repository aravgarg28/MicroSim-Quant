#pragma once

/// \file
/// Placeholder translation unit for the \c microsim_book target.
///
/// Exists so the library, its dependency edges, and its test target are wired
/// and verified from the first commit (task R1-01). Replaced by real types as
/// the module's tasks land; see docs/execution/IMPLEMENTATION_TASKS.md.

namespace microsim::book {

/// Returns the module's name. Used by the link smoke test.
[[nodiscard]] const char* module_name() noexcept;

}  // namespace microsim::book
