# Domain docs

QuickFlacRecorder uses a single-context layout:

- `GLOSSARY.md` at the repo root defines the domain vocabulary.
- `docs/adr/` holds architectural decisions.

## Before exploring

Read the root `GLOSSARY.md` and the ADRs that touch the area you will work on.

If these files or directories are absent, proceed silently.
The `/domain-modeling` skill creates domain docs when terms or decisions
are resolved. `/grill-with-docs` and `/improve-codebase-architecture`
also use that workflow.

## Use the glossary's vocabulary

Use glossary terms when naming domain concepts in issues, proposals,
hypotheses, tests, and code. Follow each term's listed synonyms to avoid.

If a needed concept is missing, first check whether the project already
uses another term. Record a real vocabulary gap for `/domain-modeling`.

## Flag ADR conflicts

If a proposal contradicts an existing ADR, name the conflicting ADR
and explain why the decision should be reconsidered.
