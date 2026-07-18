#!/usr/bin/env bash
#
# Format (or check) all first-party C++ sources with clang-format.
#
#   ./scripts/format.sh          rewrite files in place
#   ./scripts/format.sh --check  exit non-zero if anything is unformatted (CI)
#
# Third-party sources under build/ are never touched.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "error: clang-format not found in PATH" >&2
  echo "       macOS: brew install clang-format" >&2
  echo "       Linux: apt-get install clang-format" >&2
  exit 127
fi

mode="fix"
if [[ "${1:-}" == "--check" ]]; then
  mode="check"
elif [[ $# -gt 0 ]]; then
  echo "usage: $0 [--check]" >&2
  exit 2
fi

# Collected with a read loop rather than mapfile: macOS ships bash 3.2.
files=()
while IFS= read -r f; do
  files+=("${f}")
done < <(
  find src apps tests benchmarks python -type f \
    \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' \) \
    2>/dev/null | sort
)

if [[ ${#files[@]} -eq 0 ]]; then
  echo "no C++ sources found"
  exit 0
fi

if [[ "${mode}" == "check" ]]; then
  if clang-format --dry-run --Werror "${files[@]}" 2>&1; then
    echo "clang-format: ${#files[@]} files OK"
  else
    echo "clang-format: formatting issues above; run ./scripts/format.sh" >&2
    exit 1
  fi
else
  clang-format -i "${files[@]}"
  echo "clang-format: formatted ${#files[@]} files"
fi
