When reporting information, be extremely concise.

Avoid legacy/fallback behavior; this app has not shipped.

## Code Style

- Always strive for concise, simple solutions.
- If a problem can be solved in a simpler way, propose it.

## TypeScript

- Never use `any` unless 100% necessary or specifically instructed.

Use domain language from `CONTEXT.md` and ADRs in `docs/adr/`.

Data loading states:
- `undefined` = not loaded
- `[]` = loaded empty

Public API at top of file; private helpers below, in usage order.
