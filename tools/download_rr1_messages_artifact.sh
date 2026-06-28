#!/usr/bin/env bash
set -euo pipefail

repo="${RR1_PROTO_REPO:-2020vu-prog/rr1-timer-protos}"
workflow="${RR1_PROTO_WORKFLOW:-generate-c.yml}"
branch="${RR1_PROTO_BRANCH:-main}"
artifact="${RR1_PROTO_ARTIFACT:-rr1-messages-generated}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -z "${GH_TOKEN:-}" && -n "${GITHUB_TOKEN:-}" ]]; then
  export GH_TOKEN="$GITHUB_TOKEN"
fi

export GH_PROMPT_DISABLED=1

if ! command -v gh >/dev/null 2>&1; then
  echo "gh is required to download the generated protobuf artifact" >&2
  exit 1
fi

if [[ -z "${GH_TOKEN:-}" ]]; then
  echo "GH_TOKEN or GITHUB_TOKEN is required to download GitHub Actions artifacts" >&2
  echo "CI provides this automatically; locally, run 'gh auth login' and export GH_TOKEN=\$(gh auth token)." >&2
  exit 1
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

run_id="$(
  gh run list \
    --repo "$repo" \
    --workflow "$workflow" \
    --branch "$branch" \
    --status success \
    --json databaseId \
    --jq '.[0].databaseId'
)"

if [[ -z "$run_id" || "$run_id" == "null" ]]; then
  echo "no successful ${workflow} run found for ${repo}@${branch}" >&2
  exit 1
fi

gh run download "$run_id" --repo "$repo" --name "$artifact" --dir "$tmpdir"

artifact_dir="$tmpdir"
if [[ ! -f "$artifact_dir/timer.pb-c.c" && -d "$tmpdir/$artifact" ]]; then
  artifact_dir="$tmpdir/$artifact"
fi

"$repo_root/tools/update_rr1_messages_from_artifact.sh" "$artifact_dir"
