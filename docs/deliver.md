# Delivering a task with /crimson-deliver

`/crimson-deliver` takes an issue, or a task you describe, from a plan to reviewed commits and an open pull request, following [CONTRIBUTING.md](../CONTRIBUTING.md). It is a [Claude Code](https://claude.com/claude-code) skill in [`.claude/skills/crimson-deliver/`](../.claude/skills/crimson-deliver/SKILL.md), and it is optional: everything it does can be done by hand.

## How it works

The session you type in is the router. It never writes code. Three subagents do the work, `crimson-planner`, `crimson-implementer` and `crimson-reviewer` (in [`.claude/agents/`](../.claude/agents/)), and each step is a new one, so nothing piles up between rounds. They pass the plan, the report and the review through files in `.git/deliver/`, which are never committed.

1. **Intake:** reads the issue, assigns it to you, creates the branch named per CONTRIBUTING, and sizes the task S, M or L. Given a short description instead of an issue number, it first drafts the issue (problem, scope, done when, labels), shows it, and creates it on GitHub after your yes. Say no and it carries on from the text alone, without an issue.
2. **Plan:** the planner writes the plan, and you approve it. This is the one stop in the loop.
3. **Implement and review:** the implementer writes the code and the tests and builds. The reviewer rebuilds, reruns the tests and checks the change against the plan. A failed review sends each blocking finding back as a **fix** (the plan is right, the code is wrong), a **replan** (the plan is flawed) or an **ask** (it needs your decision). After three failed rounds it stops and asks you.
4. **Finish:** commits in the chunks the plan set, with `Fixes #<n>` on the last one.
5. **Ship:** shows the pull request, asks once, then pushes and opens it. It never merges.

## Starting it

Type `/crimson-deliver` followed by one of these:

| Input | Starts at | Needs |
|:------|:----------|:------|
| `<issue number>` or `<task text>` | intake, the full run | a clean working tree |
| `plan <issue or text>` | intake, stops once you approve the plan | a clean working tree |
| `implement` | implement | an approved plan already in `.git/deliver/` |
| `review [issue number or what the change should do]` | review | nothing. For code you wrote by hand, and it works on a dirty tree. With an issue number, the reviewer checks the issue's "Done when" list, and the commits end with `Fixes #<n>` |
| `commit` | finish | changes to commit. It proposes the chunks and asks |
| `ship` | ship | commits on a branch other than `main`, and a passing review |
| `resume` | the step that was running | `.git/deliver/state.md`, or it works the step out from the files |
| `ask <question>` | no step | the files in `.git/deliver/` |

## Resuming and asking

Every decision you give is written to `.git/deliver/task.md` and every step's state to `state.md`, so the chat itself can be thrown away. If the session gets long or is interrupted, `/clear` and then `/crimson-deliver resume` continue where it stopped. A question about the plan or a finding goes to a fresh read-only agent, so the router doesn't have to read it.

## Models and cost

The size picks the model and the effort for each agent: Sonnet does most of the work, and Opus plans and reviews the larger tasks. The table is in [`SKILL.md`](../.claude/skills/crimson-deliver/SKILL.md). Failed rounds retry one step up.

## Setup

- Claude Code, and the GitHub CLI (`gh`) signed in.
- Where you build, and anything else that is yours, goes in your own `~/.claude/CLAUDE.md`, not in the repo: the build folder, a PATH setup, and rules for commits such as attribution. The agents build in `build` when it says nothing.

## What it does not replace

The reviewer is another model and can miss things, so read the diff before you merge. CI still has to pass on all five jobs.
