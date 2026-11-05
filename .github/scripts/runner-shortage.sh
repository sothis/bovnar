#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Janos Sonntag
#
# runner-shortage.sh — did a run fail only because GitHub had no runner for some jobs?
#
# A job GitHub never starts ("The job was not acquired by Runner of type hosted even
# after multiple attempts", as when macOS arm64 runners are short) ends cancelled, and
# the jobs that need it are skipped. Re-running the failed jobs is then all it takes;
# the release of 2.0.1 needed five attempts for that alone. The runner-shortage
# workflow asks this script and re-runs only on "retry".
#
# Anything else keeps the run as it is: a job that failed, one cancelled by a newer
# push or by hand (no such annotation), and a branch run whose branch has moved on --
# re-running it would join the branch's concurrency group and cancel the newer run.
# A tag run is retried as long as the tag exists: tag runs are never cancelled.
#
#   .github/scripts/runner-shortage.sh OWNER/REPO RUN_ID [ATTEMPT]
#
# ATTEMPT is the run attempt to judge (default: the latest); the workflow passes the
# one that just completed.
#
# Prints "retry", or "keep: <reason>". Needs gh, authenticated for actions and checks.
set -euo pipefail

usage="usage: runner-shortage.sh OWNER/REPO RUN_ID [ATTEMPT]"
repo=${1:?$usage}
run=${2:?$usage}
attempt=${3:-}

# Each API answer is fetched into a variable first, so that a failing call stops the
# script (set -e) instead of reading as an empty answer.
info=$(gh api "repos/$repo/actions/runs/$run" --jq '"\(.head_branch) \(.head_sha)"')
read -r ref sha <<< "$info"
[ -n "$ref" ] && [ -n "$sha" ] || { echo "keep: run $run names no branch or commit"; exit 0; }
if ! gh api "repos/$repo/git/ref/tags/$ref" --silent 2>/dev/null; then
  head=$(gh api "repos/$repo/branches/$ref" --jq .commit.sha 2>/dev/null || true)
  if [ "$head" != "$sha" ]; then
    echo "keep: $ref has moved on from ${sha:0:12}"
    exit 0
  fi
fi

if [ -n "$attempt" ]; then
  jobs_url="repos/$repo/actions/runs/$run/attempts/$attempt/jobs?per_page=100"
else
  jobs_url="repos/$repo/actions/runs/$run/jobs?per_page=100"
fi
jobs=$(gh api --paginate "$jobs_url" --jq '.jobs[] | [.id, .name, .conclusion] | @tsv')
[ -n "$jobs" ] || { echo "keep: no jobs listed for run $run"; exit 0; }

shortage=0
while IFS=$'\t' read -r id name conclusion; do
  case "$conclusion" in
    success | skipped | neutral) ;;
    cancelled)
      why=$(gh api "repos/$repo/check-runs/$id/annotations" --jq '[.[].message] | join(" ")')
      # The hosted-runner wording exactly (as on v2.0.1's cancelled macOS job), so a
      # self-hosted runner that never picks up a job is not mistaken for a shortage.
      if [[ "$why" == *"was not acquired by Runner of type hosted"* ]]; then
        shortage=$((shortage + 1))
      else
        echo "keep: $name was cancelled for another reason"
        exit 0
      fi
      ;;
    *)
      echo "keep: $name ended ${conclusion:-without a conclusion}"
      exit 0
      ;;
  esac
done <<< "$jobs"

if [ "$shortage" -eq 0 ]; then
  echo "keep: no job lacked a runner"
else
  echo "retry"
fi
