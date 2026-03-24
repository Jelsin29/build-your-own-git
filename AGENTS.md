# Code Review Rules — Learning Project

## General
- This is a learning project built from scratch. No shortcuts.
- Code must compile/run without errors before committing.
- Every public function must have a clear purpose — no dead code.
- Commit messages must follow conventional commits format (feat:, fix:, refactor:, test:, docs:).

## Code Quality
- No TODO or FIXME comments left in committed code unless tracking a known limitation.
- Functions should do ONE thing. If a function is longer than 40 lines, it likely needs splitting.
- Variable and function names must be descriptive — no single-letter names except loop counters and math formulas.
- No hardcoded magic numbers — use named constants.

## Error Handling
- Every error path must be handled. No ignored return values, no empty catch blocks.
- Error messages must include context (what operation failed, with what input).

## Testing
- New functionality must have corresponding tests.
- Tests must be meaningful — not just asserting true == true.
- Test names must describe the scenario being tested.

## Security
- Never commit secrets, API keys, or credentials.
- Never log sensitive data (passwords, tokens, personal information).
- Validate all external inputs before processing.

## Forbidden
- No libraries that implement the core thing being built (defeats the learning purpose).
- No AI attribution in commit messages.
- No `--no-verify` to bypass this hook.
