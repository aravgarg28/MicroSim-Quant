#!/usr/bin/env bash
#
# Run a benchmark suite and store its JSON + a manifest (task R1-23).
#
#   ./scripts/run_benchmarks.sh --suite r1                 run the R1 suite
#   ./scripts/run_benchmarks.sh --suite r1 --dry-run       print the plan only
#   ./scripts/run_benchmarks.sh --suite all --reps 20      full suite, 20 reps
#
# Authoritative numbers come only from the dev Mac under METHODOLOGY.md rules
# (AC power, quiet machine, Release/bench preset). This script records what it
# can verify about the environment in a manifest next to the JSON; it does not
# and cannot certify the machine was quiet -- that is the operator's discipline.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

suite="r1"
reps=10
dry_run=0
build_dir="build/bench"
out_dir="results/benchmarks"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite) suite="$2"; shift 2 ;;
    --reps) reps="$2"; shift 2 ;;
    --dry-run) dry_run=1; shift ;;
    --build-dir) build_dir="$2"; shift 2 ;;
    --out) out_dir="$2"; shift 2 ;;
    -h | --help)
      grep '^#' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "error: unknown argument '$1'" >&2
      exit 2
      ;;
  esac
done

# Benchmark binaries per suite (names match benchmarks/CMakeLists.txt targets).
case "${suite}" in
  smoke) binaries=(bm_smoke) ;;
  r1) binaries=(bm_book bm_match) ;;
  all) binaries=(bm_smoke bm_book bm_match) ;;
  *)
    echo "error: unknown suite '${suite}' (expected: smoke | r1 | all)" >&2
    exit 2
    ;;
esac

stamp="$(date +%Y%m%d-%H%M%S)"
run_dir="${out_dir}/${suite}-${stamp}"

echo "suite:      ${suite}"
echo "binaries:   ${binaries[*]}"
echo "repetitions:${reps}"
echo "build dir:  ${build_dir}"
echo "output dir: ${run_dir}"
echo

if [[ ${dry_run} -eq 1 ]]; then
  echo "[dry run] would create ${run_dir}/ and write a manifest, then run:"
  for b in "${binaries[@]}"; do
    echo "  ${build_dir}/bin/${b} --benchmark_format=json \\"
    echo "    --benchmark_repetitions=${reps} --benchmark_report_aggregates_only=true \\"
    echo "    --benchmark_out=${run_dir}/${b}.json"
  done
  echo
  echo "[dry run] nothing was executed."
  exit 0
fi

# Verify the binaries exist before touching the filesystem.
missing=0
for b in "${binaries[@]}"; do
  if [[ ! -x "${build_dir}/bin/${b}" ]]; then
    echo "error: ${build_dir}/bin/${b} not found." >&2
    missing=1
  fi
done
if [[ ${missing} -eq 1 ]]; then
  echo "       configure and build the bench preset first:" >&2
  echo "       cmake --preset bench && cmake --build ${build_dir}" >&2
  exit 1
fi

mkdir -p "${run_dir}"

# Manifest: the environment facts METHODOLOGY.md requires alongside every result,
# as JSON (the committed record of a run — see .gitignore). Values are shell-
# escaped into JSON strings; missing sources become "unknown".
manifest="${run_dir}/manifest.json"
json_str() { printf '%s' "${1:-unknown}" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))'; }

chip="unknown"
cpu_cores="unknown"
mem_bytes="unknown"
power="unknown"
if [[ "$(uname -s)" == "Darwin" ]]; then
  chip="$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo unknown)"
  cpu_cores="$(sysctl -n hw.ncpu 2>/dev/null || echo unknown)"
  mem_bytes="$(sysctl -n hw.memsize 2>/dev/null || echo unknown)"
  power="$(pmset -g ps 2>/dev/null | head -1 || echo unknown)"
fi

{
  echo "{"
  echo "  \"date_utc\": $(json_str "$(date -u +%Y-%m-%dT%H:%M:%SZ)"),"
  echo "  \"suite\": $(json_str "${suite}"),"
  echo "  \"repetitions\": ${reps},"
  echo "  \"git_rev\": $(json_str "$(git rev-parse HEAD 2>/dev/null || echo unknown)"),"
  echo "  \"git_dirty\": $([[ -n "$(git status --porcelain 2>/dev/null)" ]] && echo true || echo false),"
  echo "  \"uname\": $(json_str "$(uname -a)"),"
  echo "  \"chip\": $(json_str "${chip}"),"
  echo "  \"cpu_cores\": $(json_str "${cpu_cores}"),"
  echo "  \"mem_bytes\": $(json_str "${mem_bytes}"),"
  echo "  \"power\": $(json_str "${power}"),"
  echo "  \"note\": \"confirm AC power + quiet machine (METHODOLOGY.md rule 3)\""
  echo "}"
} >"${manifest}"

echo "wrote ${manifest}"
echo

for b in "${binaries[@]}"; do
  echo "running ${b} ..."
  "${build_dir}/bin/${b}" \
    --benchmark_format=json \
    --benchmark_repetitions="${reps}" \
    --benchmark_report_aggregates_only=true \
    --benchmark_out="${run_dir}/${b}.json"
  echo "  -> ${run_dir}/${b}.json"
done

echo
echo "done. compare against a stored baseline with:"
echo "  scripts/bench_compare.py <baseline>.json ${run_dir}/<binary>.json"
