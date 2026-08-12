#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM_REMOTE="${OPENWRT_UPSTREAM_REMOTE:-upstream}"
UPSTREAM_URL="${OPENWRT_UPSTREAM_URL:-https://github.com/kanoqwq/unisoc-android-openwrt-kvm.git}"
UPSTREAM_BRANCH="${OPENWRT_UPSTREAM_BRANCH:-main}"
ORIGIN_REMOTE="${OPENWRT_ORIGIN_REMOTE:-origin}"

die() { echo "sync-upstream: $*" >&2; exit 1; }

cd "$PROJECT_DIR"
[[ $# -eq 0 ]] || die "usage: bash tools/sync-upstream.sh"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "not a Git worktree"

branch="$(git branch --show-current)"
[[ -n "$branch" ]] || die "check out the deployment branch first"
[[ -z "$(git status --porcelain)" ]] || die "commit or stash local changes first"
git remote get-url "$ORIGIN_REMOTE" >/dev/null 2>&1 || \
    die "missing push remote: $ORIGIN_REMOTE"

if git remote get-url "$UPSTREAM_REMOTE" >/dev/null 2>&1; then
    current_upstream_url="$(git remote get-url "$UPSTREAM_REMOTE")"
    [[ "$current_upstream_url" == "$UPSTREAM_URL" ]] || \
        die "$UPSTREAM_REMOTE points to $current_upstream_url; expected $UPSTREAM_URL"
else
    git remote add "$UPSTREAM_REMOTE" "$UPSTREAM_URL"
fi
# Keep upstream read-only even when a bare `git push upstream` is typed.
git remote set-url --push "$UPSTREAM_REMOTE" DISABLED

git fetch --prune "$ORIGIN_REMOTE"
git show-ref --verify --quiet "refs/remotes/$ORIGIN_REMOTE/$branch" || \
    die "$branch does not exist on $ORIGIN_REMOTE"
git merge --ff-only "$ORIGIN_REMOTE/$branch" || \
    die "local $branch diverged from $ORIGIN_REMOTE/$branch; reconcile it first"

git fetch --prune "$UPSTREAM_REMOTE"
if ! git merge --no-ff "$UPSTREAM_REMOTE/$UPSTREAM_BRANCH" \
        -m "Merge $UPSTREAM_REMOTE/$UPSTREAM_BRANCH into $branch"; then
    echo "sync-upstream: resolve the merge conflicts, commit them, then run this command again" >&2
    exit 1
fi
git push "$ORIGIN_REMOTE" "HEAD:refs/heads/$branch"
