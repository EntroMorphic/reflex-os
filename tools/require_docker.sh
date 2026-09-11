#!/usr/bin/env bash
#
# Fail fast when Docker cannot serve a gate, instead of hanging on it.
#
# `command -v docker` only proves the client is installed. If the daemon is not
# running or has wedged, the client blocks indefinitely and the gate never
# returns — `make verify` sat for thirty-four minutes inside a `docker run`
# before anyone looked, and it would have sat there all day. A gate that hangs
# is worse than one that fails, because a failure is a result and a hang is not.
set -uo pipefail

what="${1:-this target}"
limit="${DOCKER_PROBE_SECONDS:-20}"

if ! command -v docker >/dev/null 2>&1; then
    echo "docker required for $what, and it is not installed."
    exit 1
fi

# Bounded wait around the probe, by watching a background client and killing it.
#
# Not `timeout(1)`: coreutils is not present by default on macOS. Not perl's
# alarm either, which was tried and does not work here — the Docker client is a
# Go program and the Go runtime handles SIGALRM, so the signal is swallowed and
# the probe hangs exactly as before.
docker info >/dev/null 2>&1 &
probe=$!
waited=0
while kill -0 "$probe" 2>/dev/null; do
    if [ "$waited" -ge "$limit" ]; then
        kill -9 "$probe" 2>/dev/null
        wait "$probe" 2>/dev/null
        echo "docker is installed but its daemon did not answer within ${limit}s."
        echo "  $what cannot run. Start Docker and try again."
        exit 1
    fi
    sleep 1
    waited=$((waited + 1))
done

if ! wait "$probe"; then
    echo "docker is installed but 'docker info' failed."
    echo "  $what cannot run. Start Docker and try again."
    exit 1
fi
