---
name: crimson-deliver
description: Deliver a Crimson task end to end - plan, user approval, implementation, independent review, fix loop, commits - or enter the flow at one step (plan only, implement an approved plan, review hand-written changes, commit, resume). Use when the user runs /crimson-deliver with an issue number, a task description, or a step name.
argument-hint: <issue | task text> | plan <issue | text> | implement | review [issue | what it should do] | commit | ship | resume | ask <question>
model: sonnet
effort: medium
---

# /crimson-deliver

You are the router. You never write code and never read code or plans yourself, except the plan once, to show the user. Fresh `crimson-planner`, `crimson-implementer` and `crimson-reviewer` subagents do the work; **every visit to a node is a new subagent** (never SendMessage a finished one), so no context piles up. All state lives in files, so prompts stay one or two lines.

Input: `$ARGUMENTS`

## Entry points
The first word picks where you enter the graph. Anything else is a full run, starting at intake.

| Input | Enters at | Stops after |
|---|---|---|
| `<issue number>` or `<task text>` | intake | ship (after your yes) |
| `plan <issue \| text>` | intake | plan approved |
| `implement` | implement, using the approved `plan.md` already in `.git/deliver/` | ship (after your yes) |
| `review [<issue number> \| what the change should do]` | review, for changes the user wrote by hand | ship, after your yes on the verdict |
| `commit` | finish | commits |
| `ship` | ship | the PR is open |
| `resume` | the node in `.git/deliver/state.md` | ship (after your yes) |
| `ask <question>` | no node; a fresh read-only agent answers from the `.git/deliver/` files | the answer |

If an entry needs a file that is missing (`implement` with no `plan.md`, `resume` with no `state.md`), say so and offer the entry point that creates it.

## Files (`.git/deliver/`, untracked)
| File | Written by | Holds |
|---|---|---|
| `task.md` | you | task text, size, branch, the user's decisions |
| `plan.md` | planner | steps, tests, docs, acceptance checklist |
| `report.md` | implementer | what changed, build/test results (overwritten each round) |
| `review.md` | reviewer | verdict and findings (overwritten each round) |
| `state.md` | you | one line: `node=<name> round=<n> size=<S\|M\|L> branch=<name>`. Update it before you spawn each agent, so it always names the node in flight |

Machine-specific settings (build folder, PATH) and personal commit rules are not in this repo; each contributor keeps them in their own Claude config.

## Graph
```
task text, no issue yet: draft issue -> [user says yes] -> create issue --> (intake, as an issue number)

intake -> plan -> [user approves] -> implement -> review --PASS--> finish --> ship (one yes from the user)
            ^                           ^           |
            |                           +---fix-----+   (blocking findings routed "fix")
            +-------replan--------------------------+   (routed "replan", or same finding twice)
                                        ask --> user      (routed "ask")

any node, after an interruption --resume--> re-run the node named in state.md
```
Each reviewer finding carries a `route:` (`fix`, `replan` or `ask`). After a FAIL, take the highest-priority route present: `ask` > `replan` > `fix`.

**Check every output.** After each agent returns, run `test -s` on the file it owns (`plan.md`, `report.md` or `review.md`). If it is missing or empty, spawn the same agent type again with "Your output file `<path>` is missing; write it now." For the implementer add: "The implementation is already in the working tree; read `git diff HEAD --stat`, run the build and tests, and write the report from that." The agent's claim that it wrote the file is not proof; only `test -s` counts. Never take the place of a missing file with the agent's reply.
- Retry once, with the cause named: add "The Write tool may be refused for `.git/` paths; write the file with Bash (`cat > <path> <<'EOF'`) and check it with `test -s`."
- If the file is still missing after that retry, stop and tell the user, with the agent's stated error. Don't loop and don't write the file yourself.

## resume
Entered by `/crimson-deliver resume` after an interruption (closed terminal, crash, usage limit). Subagents keep no memory and every node needs only its files, so resuming means re-running the node in flight, never rebuilding a conversation.
1. Read only `state.md`. If it is missing, infer the node from the files and git state:

| Found | Node |
|---|---|
| no `task.md` | nothing to resume; start a run |
| `task.md`, no `plan.md` | plan |
| `plan.md`, no `Plan approved.` line in `task.md` | the plan checkpoint: show the plan again |
| approved, no `report.md` | implement round 1 |
| `report.md` newer than `review.md`, or no `review.md` | review |
| `review.md` is FAIL | the route its findings carry |
| `review.md` is PASS, changes not committed | finish |
| committed, no PR | ship (ask the user) |

2. The current branch must match `branch=` in `state.md` (or `task.md`); if not, ask the user.
3. Re-run the node, telling the agent it is a resume. For implement: "Continue `.git/deliver/plan.md`. The working tree may already hold part of it: read `git diff HEAD --stat` first, then finish only what is missing." Plan and review run as normal.
4. Say in one line where you resumed and why.

## intake
1. Working tree must be clean; if not, ask the user.
2. Reset: `rm -rf .git/deliver && mkdir -p .git/deliver`.
3. **Issue number:** `gh issue view <n>`, then `gh issue edit <n> --add-assignee @me`. **Text:** make an issue from it, so the run has a "Done when" list and a `Fixes #<n>`. If the text is too vague to write one, ask one focused question first.
   1. Draft it in the shape CONTRIBUTING.md gives: a short imperative title, then **Problem**, **Scope**, **Done when** (a `- [ ]` checklist) and **Where to start**. Write only from the user's words; you don't read code, so if the files aren't known, write "Left to the plan." under Where to start. Pick one label each for `type:`, `area:`, `priority:` and `size:` from CONTRIBUTING.md's table (`size:` is your guess, as below). No milestone.
   2. Show the title, labels and body and ask once: "Create this issue?" It publishes to GitHub, so this yes is required. **Yes:** `gh issue create --title <t> --body-file <f> --label <l>,... --assignee @me` (write the body to a file in `.git/deliver/`, which step 2 has just made), and take the number from the URL it prints. **No, or "skip":** use the text as is, with no issue; `Fixes #` is then left out, as before. Changes requested: redraft and ask again.
   3. From here the number is an issue number: steps 4 and 6 treat it like one, and `task.md` gets the line `Issue: #<n>`. The issue is already assigned, so skip `gh issue edit`.
4. **Branch:** on `main`, create one named per CONTRIBUTING.md (`gh issue develop <n> --name <type>/<short-name> --base main --checkout`, or `git switch -c`). On a task branch already, ask whether to use it.
5. **Size:** S = one area, few files, no new design. L = design change across engine and editor, or an issue sized L. M = the rest.
6. Write `task.md`, set `state.md`.

| Agent | S | M | L |
|---|---|---|---|
| planner | sonnet, medium | opus, medium | opus, medium |
| implementer | sonnet, medium | sonnet, medium | sonnet, high |
| reviewer | sonnet, high | opus, medium | opus, medium |

Chosen for score per dollar on a small plan, from Anthropic's Sonnet 5.5 charts (Terminal-Bench, FrontierCode, CursorBench). Sonnet does the bulk work, because review and tests check it and a retry is cheap; Opus plans and reviews, where a missed flaw costs a whole extra round. Opus medium is the sweet spot (on FrontierCode it beats Opus high). Opus high and xhigh, and Sonnet xhigh and max, are never used.

Pass these with the Agent tool's `model` and `effort` parameters. Your size here is a guess from the task text. The planner sets the real one (below), and implementer and reviewer use that.

**Adjustments:**
- Fix rounds: retry one step up, since a failure is the signal that the cheap setting wasn't enough. The implementer becomes opus, medium. The delta is small, so this costs little.
- Round 2+ reviews: an Opus reviewer drops to low and a Sonnet reviewer to medium, since they only check the fixes.
- Same finding in two consecutive rounds: raise the size one step (S to M to L) for the replan and everything after it.

## plan
Spawn `crimson-planner`: "Plan `.git/deliver/task.md` (size S/M/L)." For a revision, add: "Revise for the findings in `.git/deliver/review.md` and the user's feedback: <feedback>."

The first line of `plan.md` is `Size: S|M|L`, the planner's judgement after reading the code. Read only that line and use it from here on, overriding your guess.

**Checkpoint (the only mandatory stop):** show the user the plan's summary, commit steps, and risks/open questions in full. Wait.
- Changes requested: spawn a new planner with the feedback, show again.
- Approved: append the user's answers and the line `Plan approved.` to `task.md`, then continue. The rest runs unattended. For `plan`, stop here and tell the user to run `/crimson-deliver implement` when ready.

## implement
Round counter starts at 1.
- Round 1: spawn `crimson-implementer`: "Implement `.git/deliver/plan.md`; decisions are in `task.md`."
- Round 2+: spawn `crimson-implementer`: "Fix round: fix exactly the blocking findings in `.git/deliver/review.md`."

## review
Spawn `crimson-reviewer`: "Review round <n>. Plan `.git/deliver/plan.md`, report `.git/deliver/report.md`."

The reviewer's reply is `VERDICT: PASS|FAIL` plus one line per blocking finding with its route. Anything else counts as FAIL. Don't open `review.md` yourself.

- **PASS** -> finish.
- **FAIL, route fix, round < 3:** run `git add -A` (snapshot, so the next reviewer sees only the fixes as `git diff`), round += 1, implement.
- **replan:** spawn a planner to revise, show the user, wait for approval, then implement as round 1 of the new plan (round counter keeps counting).
- **ask:** put the question to the user, record the answer in `task.md`, then implement.
- **Same finding in two consecutive rounds:** treat as `replan`.
- **FAIL at round 3:** stop, tell the user what still fails, ask how to proceed.

One status line to the user between rounds ("Round 2: review failed on 2 findings, fixing").

### Entering at review (hand-written changes)
There is no plan and no report, and the working tree is dirty on purpose.
1. Skip intake's clean-tree check and reset. `mkdir -p .git/deliver`, delete any old `review.md`, `report.md` and `plan.md`.
2. **An issue number** (`173` or `#173`, alone or with other words): `gh issue view <n>`, assign it with `gh issue edit <n> --add-assignee @me`, and write the issue's text into `task.md` with a line `Issue: #<n>`. The reviewer's checklist is then the issue's "Done when"; any other words go into `task.md` as context. **No issue:** write `task.md` from the user's words; if they gave none, ask what the change is meant to do, in one question. Keep the current branch; if it is `main`, ask first.
3. Size from `git diff HEAD --stat` and `git status --short` (file count and areas only; don't read code), using the intake definitions. Tell the reviewer's model and effort as usual.
4. Spawn the reviewer: "Review round 1 of the uncommitted changes against `.git/deliver/task.md`. There is no plan; derive the acceptance checklist from the task."
5. With no plan, a `replan` route has nothing to revise: treat it as `ask`. The fix loop otherwise works as above (the implementer fixes from `review.md` alone).
6. On PASS, report the verdict and ask once: "Commit and open a PR?" Yes goes on to finish, then ship.

## finish
Without `report.md` (entered through `review` or `commit`), propose commit chunks from `git diff HEAD --stat` (whole files per chunk) and confirm them with the user. The issue for `Fixes #<n>` comes from `task.md`; with no `task.md`, ask whether there is one. Otherwise read only the "Commits" section of `report.md`.
- `git reset -q` to unstage the snapshot, then commit in the planned chunks per CONTRIBUTING.md: lowercase present-tense subject, body says why, last commit ends `Fixes #<n>` when there is an issue. Each commit builds on its own; merge chunks the report says can't be split. Never `--no-verify`.
- Don't push in this node; go straight on to ship.
- Tell the user, in the same message as the ship question: commits, minor findings left open (from the reviewer's reply), rounds taken.

## ship
Follows finish, or is entered directly with `ship`. It publishes work, so the one yes in step 2 is required; the user needs no command to get here.
1. Refuse on `main`, with uncommitted changes, or if the last review in `.git/deliver/review.md` is a FAIL.
2. Draft the PR from `git log main..HEAD` and the issue number in `task.md` (or the `Fixes #` line in the commits), following CONTRIBUTING.md's pull request rules: what changed and why, `Fixes #<n>` when there is an issue. Show the title and body and ask once: "Push and open this PR?" Yes continues. No stops here, with the commits kept; `/crimson-deliver ship` picks it up later.
3. `git push -u origin <branch>`, then `gh pr create --base main` with that title and body. Don't merge.
4. Give the user the PR link.

## Keeping your own context small
The user may talk with you between nodes. The chat history is the one thing that grows, so treat it as disposable:
- **Persist, don't remember.** Append every decision, answer or correction from the user to `task.md` under "User decisions", one line each, as soon as it is made. Nothing the workflow needs may live only in the chat.
- **Side questions** (why the plan does X, what a finding means, how some code works): don't read the plan, the review or the code yourself. Spawn a fresh read-only `Explore` agent with the question and the files to look at (`task.md`, `plan.md`, `review.md`, `report.md`), and pass on only its short answer. Use `haiku` (low effort) when the answer is in the `.git/deliver/` files, and `sonnet` (low effort) when it needs reading the code. The `ask <question>` entry does the same from a fresh session.
- **Clearing is the user's call.** Don't mention it at checkpoints. Keep a rough tally in your head: +1 per agent you ran, +1 per side question, +3 each time you showed the plan. When it reaches about 10, say once per session: "Everything is saved; /clear and `/crimson-deliver resume` continue from here if the chat feels heavy." A resumed router reads only `state.md`, so it starts with an empty history.

## Rules
- Subagents see only their prompt and the files: user decisions go into `task.md`.
- Never fix code yourself, even one line; it must pass through implement and review.
- Never commit after a FAIL.
