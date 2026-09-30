#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <base-revision> <head-revision>" >&2
    exit 2
fi

base_revision=$1
head_revision=$2
clang_format=${CLANG_FORMAT:-clang-format}

mapfile -t changed_sources < <(
    git diff --name-only --diff-filter=ACMR \
        "${base_revision}" "${head_revision}" -- \
        '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' |
        while IFS= read -r source; do
            [[ -f "${source}" ]] && printf '%s\n' "${source}"
        done
)

if [[ ${#changed_sources[@]} -eq 0 ]]; then
    echo "No modified C/C++ sources require clang-format."
    exit 0
fi

"${clang_format}" --version
"${clang_format}" --dry-run --Werror "${changed_sources[@]}"
