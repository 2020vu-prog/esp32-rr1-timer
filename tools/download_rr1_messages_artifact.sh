#!/usr/bin/env bash
# Fetch the generated protobuf-c bindings published by the rr1-timer-protobufs
# release workflow (generate-c.yml) and install them into components/rr1_messages.
#
# Release assets do not expire, unlike Actions artifacts. By default this
# follows GitHub's "latest" release, which the proto repo moves to a new,
# already-verified bindings-<sha> release on every push to main (it never
# deletes or replaces the previous one in place). Set RR1_PROTO_TAG=vX.Y.Z to
# pin a reproducible build to a tagged release.
set -euo pipefail

repo="${RR1_PROTO_REPO:-2020vu-prog/rr1-timer-protobufs}"
tag="${RR1_PROTO_TAG:-latest}"
asset="${RR1_PROTO_ASSET:-rr1-messages-generated.zip}"
if [[ "$tag" == "latest" ]]; then
  base="https://github.com/${repo}/releases/latest/download"
else
  base="https://github.com/${repo}/releases/download/${tag}"
fi
# RR1_PROTO_ASSET_URL overrides the download location (e.g. a file:// URL).
url="${RR1_PROTO_ASSET_URL:-${base}/${asset}}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

echo "downloading ${url}" >&2
# The proto repo is public, so no token is needed. Fall back to gh for a
# private repo (needs GH_TOKEN or GITHUB_TOKEN).
if ! curl -fsSL --retry 3 -o "$tmpdir/$asset" "$url"; then
  if [[ -n "${RR1_PROTO_ASSET_URL:-}" ]] || ! command -v gh >/dev/null 2>&1; then
    echo "failed to download ${url}" >&2
    exit 1
  fi
  if [[ -z "${GH_TOKEN:-}" && -n "${GITHUB_TOKEN:-}" ]]; then
    export GH_TOKEN="$GITHUB_TOKEN"
  fi
  export GH_PROMPT_DISABLED=1
  tag_arg=()
  [[ "$tag" == "latest" ]] || tag_arg=("$tag")
  gh release download ${tag_arg[@]+"${tag_arg[@]}"} --repo "$repo" --pattern "$asset" \
    --dir "$tmpdir"
fi

unzip -q "$tmpdir/$asset" -d "$tmpdir/artifact"

"$repo_root/tools/update_rr1_messages_from_artifact.sh" "$tmpdir/artifact"
