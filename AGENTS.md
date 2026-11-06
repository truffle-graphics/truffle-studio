# AGENTS.md

1. Read `PROJECT_CONTEXT.md`, `README.md` and `docs/handoff.md` first.
2. Accepted work becomes an issue (with context, outcome, scope, non-goals,
   architecture, acceptance and validation) on the Truffle delivery project
   before implementation. Keep parent/sub-issue links and Project status
   current.
3. Before implementing a plan, ask whether delivery is review-first
   (uncommitted) or commit-and-push. Commit-and-push means signed,
   issue-scoped commits pushed immediately, one batch branch/PR per related
   issue set into `develop`, merged with a normal merge commit. Never squash
   or rewrite published history.
4. Update `docs/handoff.md` before every AI-authored commit.
5. Respect the Studio/Truffle boundary in `PROJECT_CONTEXT.md`. Truffle
   changes go to `truffle-graphics/truffle` through its own process.
6. Validate locally: use the `debug` preset with warnings-as-errors enabled
   (`./build.sh debug --warnings-as-errors on`) and run `ctest --preset debug`.
   Hosted CI is not used.
