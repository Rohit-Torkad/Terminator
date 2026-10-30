# Testing

`make test` (or `./scripts/run_tests.sh`) runs everything; `SKIP_GUI=1` skips the Xvfb suite.

| Suite | Kind | What it covers |
|---|---|---|
| `tests/unit/test_lexer.c` | automated | tokens, quoting, operators in quotes/substitutions, completeness detection |
| `test_parser.c` | automated | pipelines, redirections, `; & && \|\|`, syntax errors |
| `test_expansion.c` | automated | quotes, `$?` `$#` `$1`, splitting, nested `$()`, backticks, globbing |
| `test_history.c` | automated | add/dup/blank, search algorithm, LCS, persistence, trimming, **4 threads** (mutex) and **4 forked processes** (shared memory + semaphore) |
| `test_completion.c` | automated | unique/ambiguous/none, dirs, hidden files, quotes, escapes |
| `test_config.c` | automated | bindings, invalid keys/actions, conflicts, persistence |
| `tests/integration/test_*.sh` | automated, **both modes** | commands, per-tab cwd, pipelines (2..5 stages, 100k-line data), redirection incl. failures, job control incl. process groups and orphan checks, multi-line input, scripts incl. malformed ones, multiWatch, history limits, `bind` |
| `tests/gui/gui_test.sh` | automated, needs Xvfb + xdotool, **both modes** | real key/mouse events: tabs, per-tab cwd, shared history, every editing shortcut, Ctrl+R, Tab completion (incl. list+select), clipboard (mouse selection and external X client), Ctrl+C/Ctrl+Z/fg/bg, multiWatch, GUI responsiveness during a long command, resize, tab close, **tab process crash**, clean shutdown without orphans, persistent and live-reloaded key bindings |

The engine is exercised headlessly through `./terminator --headless` (commands on stdin; directives `@@INT@@`, `@@SUSP@@`,
`@@EOF@@`, `@@WAIT ms@@`, `@@LINE text`); this uses the real engine, IPC protocol and both execution modes.

Sanitizers: `make debug` builds `./terminator-debug` (ASan + UBSan, leak detection on). Run the integration suites against it with
`TERMINATOR_BIN=$PWD/terminator-debug ./tests/integration/test_commands.sh` etc. Set `ASAN_OPTIONS=log_path=/tmp/asan` to capture
reports of the engine's stderr; the reference run produced none.

## Manual GUI checklist (what the automated GUI test cannot judge: appearance)
Run `./scripts/screenshots.sh` or check by hand in a real desktop session (VirtualBox included):
1. Window opens; dark background, tab bar with `1 ~/dir`, `x` and `+`; active tab underlined.
2. Typing shows a block cursor that blinks; moving with Left/Right/Ctrl+A/E redraws correctly.
3. Resize the window with the mouse: text re-wraps, nothing is garbled; scroll back with the wheel.
4. `ls /usr/bin` scrolls; `cat nosuchfile` prints in red; `^C`/`^Z` are shown in amber.
5. Open 3 tabs, `cd` differently in each; the tab bar shows each directory.
6. Select text with the mouse (highlight), `Ctrl+Shift+C`, then `Ctrl+Shift+V` in another tab or application.
7. `multiWatch ["date +%T", "ls | wc -l"]`: both update every 2 s, the window stays responsive; `Ctrl+C` returns to the prompt.
8. Close the window with the window-manager button: `pgrep sleep` shows no leftover background jobs.
Reference screenshots: `docs/screenshots/` (thread and process mode).
