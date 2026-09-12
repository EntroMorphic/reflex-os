#!/usr/bin/env bash
#
# Lint the GitHub Actions workflows with actionlint.
#
# Prefer a native actionlint over the container. The container form was the
# only form for a long time, which meant this gate simply did not run on a
# machine without a Docker daemon -- and a gate that cannot run is a gate that
# is not protecting anything. `pip install actionlint-py` provides the binary.
#
# Usage: tools/check_workflows.sh   (or: make ci-lint)

set -uo pipefail
cd "$(dirname "$0")/.."

ACTIONLINT_IMAGE=${ACTIONLINT_IMAGE:-rhysd/actionlint:latest}

n=$(ls .github/workflows/*.yml .github/workflows/*.yaml 2>/dev/null | wc -l | tr -d ' ')
[ "$n" -gt 0 ] || { echo "no workflows found under .github/workflows"; exit 1; }

if command -v actionlint >/dev/null 2>&1; then
    out=$(actionlint -no-color -shellcheck= 2>&1)
    rc=$?
else
    "$(dirname "$0")/require_docker.sh" \
        "ci-lint (or: pip install actionlint-py)" || exit 1
    out=$(docker run --rm -v "$PWD":/repo -w /repo \
        "$ACTIONLINT_IMAGE" -no-color -shellcheck= 2>&1)
    rc=$?
fi

if [ $rc -ne 0 ]; then
    echo "$out"
    echo
    echo "Workflow lint FAILED."
    exit 1
fi
echo "Workflows: $n file(s), schema valid."
