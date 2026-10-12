# Issue tracker: GitHub

Issues and specs for QuickFlacRecorder live in GitHub Issues for
`reon-nynes/QuickFlacRecorder`. Use the `gh` CLI for issue operations.

Run commands from this repo so `gh` infers the repository from its Git remote.
Outside the repo, pass `--repo reon-nynes/QuickFlacRecorder`.

## Conventions

- Create an issue: `gh issue create --title "..." --body-file <file>`.
- Read an issue: `gh issue view <number> --json number,title,body,labels,comments`.
- List issues: `gh issue list --state open --json number,title,body,labels,comments`.
  Add `--label` and `--state` filters as needed.
- Update an issue body: `gh issue edit <number> --body-file <file>`.
- Create a sub-issue: `gh issue create --parent <parent> --title "..." --body-file <file>`.
- Link an existing child: `gh issue edit <parent> --add-sub-issue <child>`.
  If sub-issues are unavailable, put `Part of #<parent>` at the top of the child
  body and add the child to a task list in the parent body.
- Comment on an issue: `gh issue comment <number> --body-file <file>`.
- Apply a label: `gh issue edit <number> --add-label "..."`.
- Remove a label: `gh issue edit <number> --remove-label "..."`.
- Close an issue: `gh issue close <number>`. Post any explanation first.

For multiline bodies and comments, write the exact text to a temporary UTF-8
file and pass it with `--body-file`.

## Pull requests in triage

**PRs as a request surface: no.**

When this flag is set to `yes`, triage external pull requests using the same
labels and states as issues:

- Read a PR: `gh pr view <number> --comments` and `gh pr diff <number>`.
- List external PRs: use `gh api --paginate 'repos/{owner}/{repo}/pulls?state=open'`
  and keep authors whose `author_association` is neither `OWNER`, `MEMBER`,
  nor `COLLABORATOR`.
- Comment, label, and close with `gh pr comment`, `gh pr edit`, and `gh pr close`.

GitHub issues and PRs share a number space. For an ambiguous reference, try
`gh pr view <number>` and fall back to `gh issue view <number>`.

## Publishing and fetching tickets

When a skill says "publish to the issue tracker", create a GitHub issue.
When it says "fetch the relevant ticket", read the issue with its labels
and comments using the command above.

## Wayfinding operations

The `/wayfinder` skill uses one map issue with child issues as tickets.

- Map: create an issue labelled `wayfinder:map` with the Notes,
  Decisions-so-far, and Fog sections.
- Child ticket: link it as a sub-issue of the map. Use `wayfinder:<type>` labels
  for `research`, `prototype`, `grilling`, or `task`. If sub-issues are
  unavailable, use the parent task list and `Part of #<map>` convention above.
- Blocking: use GitHub's native issue dependencies.
  `gh issue edit <child> --add-blocked-by <blocker>` adds a dependency.
  If dependencies are unavailable, put `Blocked by: #<n>, #<n>` at the top of
  the child body. A ticket is unblocked when every blocker is closed.
- Frontier: list the map's open children and exclude assigned tickets and
  tickets with an open blocker. Use native `issue_dependencies_summary.blocked_by`
  when available, otherwise inspect the `Blocked by` references.
  Choose the first remaining ticket in map order.
- Claim: `gh issue edit <number> --add-assignee "@me"` before starting work.
- Resolve: comment with the answer, close the child, and append a brief result
  with a link to the map's Decisions-so-far section.

Create any missing `wayfinder:*` labels when that workflow is used.
