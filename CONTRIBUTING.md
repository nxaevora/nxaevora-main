<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->
# Contributing

Thank you for your interest. This file tells what a change must follow. For the state of the project, see
[ROADMAP.md](ROADMAP.md).

## Game data stays out

Nothing of the game goes into git, an issue, a pull request or a comment. This includes keys, dumps (NSP, XCI, NCA),
extracted files, the code that the recompiler makes, saves, logs that hold game data, and shader or texture dumps.

You can quote addresses, symbol names and short disassembly in commit messages and docs. You cannot attach extracted
files. The folder `game/` is in `.gitignore` for this reason. Keep your dumps and your build output there or outside
the repository.

## License

NXAevora is GPL-3.0-or-later. A new file starts with this line, in the comment syntax of its language, and the
copyright line of the project:

```
SPDX-License-Identifier: GPL-3.0-or-later
Copyright 2026 the NXAevora contributors
```

By contributing, you agree that your work is released under this license.

### Code from other projects

You can port code that is under GPL-3.0, GPL-2.0-or-later (for example yuzu, suyu, sudachi, Citron and Eden), LGPL,
MIT, BSD, ISC, zlib or Apache 2.0. Keep its copyright lines, say where it came from, and list it in the credits.

You cannot port code that is GPL-2.0-only. Atmosphere is GPL-2.0-only. You can read it to learn how Horizon behaves,
but do not port it. The main references are Switchbrew and the behavior of the SDK, which you can read from the
disassembly of the game.

## Changes to ScarletNX

`third_party/ScarletNX` is a submodule of the upstream project. Do not edit it in this repository unless a change is
necessary, and say so in the pull request. A fix that helps every game belongs in the upstream project first.

## Code

- Give each function one job. Extract a helper when it makes the flow clear or when the rule is reusable. Do not
  build speculative frameworks.
- Write ownership, lifetime, error handling and fallback behavior explicitly. On an error, keep the context, so that
  you can find the stage that failed.
- Use a named constant for each threshold or setting that has a meaning. Document why a workaround exists and when it
  applies.
- Prefer early checks and simple control flow. Avoid deeply nested branches, unrelated refactors and wide format
  changes.
- Comments tell the intent, the assumptions and the limits. A comment has 4 lines at most. Do not comment what the
  code, a name or a type already says.
- Code identifiers, comments and project documents are in English.

## Find the mechanism

A fix explains why the game does what it does. It tells what the game or SDK code expects, and what the runtime gave
it instead. Do not tune delays, add retries or add special cases to hide a problem.

## Testing

A build is not a test. To verify a change to the runtime, run the game through the scene that the change affects.
The pull request tells what you ran. If a change fixes a crash, run the game many times. One good run does not prove
the fix of an intermittent crash.

## Commits and documents

- A commit has a short subject in the imperative. The body says why, and how you verified the change.
- Commit messages, the README, the roadmap and other documents for users are plain prose. They have no em dash
  between clauses, no semicolon between sentences and no spaced hyphen used as a dash.

## Pull requests

Keep a pull request small and about one subject. Describe what it changes, why, and what you ran to check it.
Do not include game data (see above).
