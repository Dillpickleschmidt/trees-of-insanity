# Personal Preferences

When reporting information to me, be extremely concise and sacrifice grammar for the sake of concision.

Avoid writing code to support legacy/fallback behavior since this app hasn't been released yet and there's no point bloating code to support something that no one uses anyawy.

Avoid writing test to confirm that something you removed during refactoring is truly gone. That just bloats the codebase for no reason and is a waste of time, especially since no version has even been released yet anyway.

In general, don't write code that is intended to handle previous behavior since nobody is using or will be expecting to use previous behavior since this hasn't even been released.

## TypeScript

- Never use `any` unless 100% necessary or specifically instructed.

## Commands

- Don't run dev server commands (e.g., `bun run dev`) - assume it's already running.
- Don't run build commands unless specifically told to.
- Focus on checking commands like `bun run typecheck`, `bun run lint`, etc.

## Code Style

- Always strive for concise, simple solutions.
- If a problem can be solved in a simpler way, propose it.

## General preferences

- If asked to do too much work at once, stop and state that clearly.
- If extra model work is useful, shell out to pi using gpt-5.5 with a self-contained prompt. For app verification, use this repo's JSONL automation/report seam and screenshot-coordinate agent-control commands.

## Picking the right models for pi-assisted work

Rankings, higher = better. Cost reflects what I actually pay (OpenAI is near-free for me due to a deal), not list price. Intelligence is how hard a problem you can hand the model unsupervised. Taste covers UI/UX, code quality, API design, and copy.

| model | cost | intelligence | taste |
| --- | --- | --- | --- |
| gpt-5.5 | 9 | 8 | 5 |
| sonnet-5 | 5 | 5 | 7 |
| opus-4.8 | 4 | 7 | 8 |
| fable-5 | 2 | 9 | 9 |

How to apply:
- These are defaults, not limits. You have standing permission to override them: if a cheaper model's output doesn't meet the bar, rerun or redo the work with a smarter model without asking. Judge the output, not the price tag. Escalating costs less than shipping mediocre work.
- Don't let cost prevent you from using the right model for the job. Instead, take advantage of cheaper options to get more information and try things before moving the work to a more expensive option.
- Bulk/mechanical work (clear-spec implementation, data analysis, migrations): gpt-5.5 - it's effectively free.
- Anything user-facing (UI, copy, API design) needs taste >= 7.
- Reviews of plans/implementations: fable-5 or opus-4.8; use the `review` skill (`/review`) for a gpt-5.5 independent perspective.
- Code-review merge gate: never merge code until gpt-5.5 (Pi) has reviewed and approved. Run Pi **from the repo/worktree directory** so it sees the diff: `cd <worktree> && pi -p --provider openai-codex --model gpt-5.5 --thinking high "review this branch vs main: <what to check>; end with APPROVE or REQUEST_CHANGES"`.
- Never use Haiku.
- Mechanics: gpt-5.5 is available in Pi as `openai-codex/gpt-5.5`; use the `review` skill (`/review`) for review work. For non-review one-off work, use `pi -p --provider openai-codex --model gpt-5.5 --thinking high "<prompt>"`, or `pi --mode json --provider openai-codex --model gpt-5.5 "<prompt>"` when a script needs machine-readable events.
- In pi, switch models with `/model` or Ctrl+L; for CLI runs, use `--provider` and `--model`.

Using gpt-5.5 from pi:
- Keep prompts self-contained: goal, repo path, relevant files, constraints, expected report/patch format.
- Use the `review` skill (`/review`) for independent reviews; use gpt-5.5 directly for investigations, data analysis, and clear-spec implementation help.
- For parallel implementation experiments, use separate worktrees/checkouts or request patches only; don't let multiple agents edit the same checkout.

## Repo Automation

- `bun run verify:shell` writes JSONL/log/screenshot artifacts and fails if the native viewport handle is not reported.
- Inspect `artifacts/automation/*.jsonl` and `.log` before screenshots; custom runs can set `TOI_AUTOMATION_REPORT=<path>`.
- App state/commands go through `src/shared/appCommands.ts` and `src/shared/shellRpc.ts`; use `scripts/agent screenshot|move|click|down|up|drag|scroll|key|type` only for real UI/viewport interaction. Coordinates are screenshot pixels.

## Domain Language

Use domain language from `CONTEXT.md` and ADRs in `docs/adr/`.

## Data Loading States

- `undefined` = not loaded
- `[]` = loaded empty

## File Organization

Public API at top of file; private helpers below, in usage order.
