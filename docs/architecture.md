# Module map

| File | Responsibility |
|---|---|
| `src/main.c` | argument parsing; dispatch to GUI or headless frontend |
| `src/config.c` | key binding table, `~/.terminatorrc` parse/validate/persist |
| `src/headless.c` | GUI-less frontend driving one engine over the same IPC (tests) |
| `src/gui/window.c` | X connection, window, font, colours, resize, pixmap |
| `src/gui/event_loop.c` | `poll()` loop, X event dispatch, mouse handling |
| `src/gui/renderer.c` | tab bar, wrapped text rows, selection, cursor |
| `src/gui/input.c` | KeyPress → `Action` (decoding) and line editing, history browse/search, completion UI |
| `src/gui/tabs.c` | tab lifecycle, engine message handling, config reload |
| `src/gui/scrollback.c` | per-tab output buffer (ANSI stripping, `\r`, `\b`, tabs) |
| `src/gui/clipboard.c` | CLIPBOARD/PRIMARY ownership and paste |
| `src/shell/shell.c` | `ShellState`, variables, output routing, control messages, engine main loop |
| `src/shell/lexer.c` | tokenizer, completeness detection |
| `src/shell/parser.c` | tokens → pipelines / lists |
| `src/shell/expansion.c` | quotes, `$…`, substitution, splitting, globbing |
| `src/shell/executor.c` | planning and running pipelines; `$(…)` |
| `src/shell/builtins.c` | builtin table, `test`/`[` |
| `src/shell/script.c` | statements, `if` validation/execution, script scopes, `bash x.sh` detection |
| `src/process/pipeline.c` | pipes, fork, dup2, redirection in children, process groups |
| `src/process/jobs.c` | job table, reaping, `sh_pump`, fg/bg/stop/terminate |
| `src/process/redirection.c` | open helpers for engine-side redirection |
| `src/process/signals.c` | signal policy (GUI, engine process, child reset) |
| `src/process/process_utils.c` | wait status → exit code, `pipe2`, fd helpers |
| `src/concurrency/ipc.c` | message framing and reader |
| `src/concurrency/thread_mode.c`, `process_mode.c` | engine start/stop per mode |
| `src/concurrency/shared_state.c` | mode dispatch, shared history creation |
| `src/features/history.c` | shared persistent history + Ctrl+R search |
| `src/features/completion.c` | filename completion |
| `src/features/multiwatch.c` | multiWatch |
| `src/utils/*.c` | checked allocation + `Buf`, `StrList`, lexical paths, debug log + `write_all` |

Dependency direction: `gui → concurrency → shell → process → utils`; `features` are used by both `gui` (history,
completion) and `shell` (history, multiWatch). The shell never includes GUI headers, so the engine can run headless.
