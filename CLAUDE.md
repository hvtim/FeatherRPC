# CLAUDE.md

Guidelines for working on this repo.

## Comments

Don't write comments unless the *why* is genuinely non-obvious - a
hidden constraint, a workaround for a specific bug, a subtle invariant.
Don't explain what code does; well-named identifiers already do that.
Don't reference the current task, issue, or PR number in a comment -
that belongs in the commit message.

## Docs

README.md and other docs are for users, not developers. State facts and
instructions plainly, one pass. Cut:

- rationale for why something works the way it does
- implementation detail the reader of that doc doesn't need
- restating something already said elsewhere in the same doc
- hedge/emphasis words ("real", "actual", "genuinely")

If detail is genuinely needed, link to the doc that owns it
(`docs/KnownIssues.md`, `CONTRIBUTING.md`, etc.) instead of inlining it.

## Output

Be concise. Don't pad responses to fill space.
