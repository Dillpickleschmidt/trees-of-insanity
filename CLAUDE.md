When reporting information to me, be extremely concise and sacrifice grammar for the sake of concision.

Avoid writing code to support legacy/fallback behavior since this app hasn't been released yet and there's no point bloating code to support something that noone uses anyawy.

Avoid writing test to confirm that something you removed during refactoring is truly gone. That just bloats the codebase for no reason and is a waste of time, especially since no version has even been released yet anyway.

In general, don't write code that is intended to handle previous behavior since nobody is using or will be expecting to use previous behavior since this hasn't even been released.

## Agent skills

### Domain docs

This repo uses a single-context domain-doc layout. See `docs/agents/domain.md`.

### Paper-to-code naming

Use developer-friendly variable names in code. When implementing equations from a paper, add a concise nearby comment mapping the paper symbol to the code name, for example: `// Paper: a_u, module physiological age.`
### File Organization

- Public API at top of file
- Private helpers below, in order of usage

```ts
export function mainFunction() {
  helperA()
  helperB()
}

function helperA() { ... }
function helperB() { ... }
```

### No Thin Wrappers

While thin wrappers may be useful for readability in highly repeated invocations, they should generally be avoided when it would be simpler to inline. An example of what NOT to do:

```
function isMarkerBlock(block: WorkerChunkBlock): block is MarkerChunkBlock {
  return "id" in block && "block_type" in block
}
```

### Avoid comments explaining "changes"

While I'm fine with comments and documentation, I don't want any comments that explain code "changes" made as that's useless for new developers.

### Data Loading States

- `undefined` = not yet loaded → show skeleton/loader
- `[]` = loaded but empty → show empty state

Avoid fallbacks like `?? []` that mask the difference. Derive loading state from the data itself or derived reactive data, not separate `isLoading` props.

```tsx
<Show when={data !== undefined} fallback={<Loader />}>
  <Show when={data.length} fallback={<EmptyState />}>
    <Content data={data} />
  </Show>
</Show>
