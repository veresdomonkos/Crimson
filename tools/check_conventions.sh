#!/bin/sh
# Checks the branch, commit and file conventions in CONTRIBUTING.md. The hooks in .githooks run it on each
# commit, and the CI conventions job on each pull request.
#
#   check_conventions.sh branch <name>
#   check_conventions.sh message <file> [<what the message is, for the errors>]
#   check_conventions.sh files --cached          the staged changes
#   check_conventions.sh files <base>...HEAD     a branch's changes
#
# It lists every problem it finds, and exits with 1 if there were any.

set -u

max_subject_length=100
failed=0

fail()
{
    printf '%s\n' "$@" >&2
    failed=1
}

check_branch()
{
    name=$1

    if [ "$name" = main ]; then
        fail "Commits don't go on main. Make a branch for the issue:" \
             "  gh issue develop <number> --name feature/<short-name> --base main --checkout"
        return
    fi

    if ! printf '%s\n' "$name" | grep -Eqx '(feature|fix|refactor|chore|docs)/[a-z0-9]+(-[a-z0-9]+)*'; then
        fail "Branch '$name' has to be feature/, fix/, refactor/, chore/ or docs/, then a lowercase name with hyphens." \
             "Rename it with: git branch -m <prefix>/<short-name>"
    fi
}

check_message()
{
    file=$1
    what=${2:-Commit message}

    # Without the comments git leaves out, and the blank lines before the subject
    message=$(grep -v '^#' "$file" | tr -d '\r' | sed '/./,$!d')
    subject=$(printf '%s\n' "$message" | sed -n 1p)
    second=$(printf '%s\n' "$message" | sed -n 2p)

    # Messages git writes itself
    case $subject in
        'Merge '* | 'Revert "'* | 'fixup! '* | 'squash! '* | 'amend! '*) return ;;
    esac

    if [ -z "$subject" ]; then
        fail "$what: the subject is empty."
        return
    fi

    case $subject in
        *.) fail "$what: the subject can't end with a period: $subject" ;;
    esac

    if [ "${#subject}" -gt "$max_subject_length" ]; then
        fail "$what: the subject is ${#subject} characters, at most $max_subject_length fit. Move the rest to the body: $subject"
    fi

    if [ -n "$second" ]; then
        fail "$what: leave a blank line between the subject and the body."
    fi
}

# The files that have to end with a newline. The editor's own files (.mat, .meta, .scene, ...) are left out,
# so that saving one never fails a commit.
needs_final_newline()
{
    case $1 in
        *.cpp | *.hpp | *.h | *.c | *.hlsl | *.hlsli | *.cmake | *.txt | *.md | *.yml | *.json | *.ps1 | *.sh | .githooks/*) return 0 ;;
        *) return 1 ;;
    esac
}

check_files()
{
    # The staged version with --cached, otherwise the branch's
    case ${1:-} in
        --cached) version=: ;;
        *) version=HEAD: ;;
    esac

    # Only added lines, by the whitespace rules in .gitattributes. The submodules and the code copied into
    # external/ keep their own style.
    problems=$(git -c core.quotepath=off diff --check "$@" -- . ':!external')
    if [ -n "$problems" ]; then
        fail "Added lines with trailing whitespace, a CR, or tabs for indentation:" "$problems" ""
    fi

    missing=$(git -c core.quotepath=off diff --name-only --diff-filter=AM "$@" -- . ':!external' | while IFS= read -r path; do
        needs_final_newline "$path" || continue
        [ "$(git cat-file -s "$version$path")" -gt 0 ] || continue
        [ "$(git cat-file -p "$version$path" | tail -c 1 | wc -l)" -eq 1 ] || printf '  %s\n' "$path"
    done)

    if [ -n "$missing" ]; then
        fail "Files that don't end with a newline:" "$missing"
    fi
}

command=${1:-}
[ $# -gt 0 ] && shift

case $command in
    branch) check_branch "${1:-}" ;;
    message) check_message "$@" ;;
    files) check_files "$@" ;;
    *)
        printf '%s\n' "Usage: $0 branch <name> | message <file> [<what>] | files --cached | files <base>...HEAD" >&2
        exit 2
        ;;
esac

exit $failed
