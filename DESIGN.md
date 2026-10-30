# Terminator — design

## 1. One engine, two execution modes

```
            GUI process (only code that touches Xlib)
 ┌──────────────────────────────────────────────────────────┐
 │  poll( X connection , tab[0].out_r , tab[1].out_r , … )  │   src/gui/event_loop.c
 │  KeyPress → Action → line editing → M_LINE/M_INTR/…      │   input.c
 │  engine messages → scrollback → redraw                   │   tabs.c, renderer.c
 └───────┬──────────────────────────────▲───────────────────┘
   ctl pipe (GUI→engine)          out pipe (engine→GUI)
 ┌───────▼──────────────────────────────┴───────────────────┐
 │  engine_main()  =  ShellState + lexer/parser/expansion/  │   src/shell/*, src/process/*
 │  executor/jobs/builtins/script/multiWatch                │
 └──────────────────────────────────────────────────────────┘
   thread mode:  pthread_create(engine_thread_main)         src/concurrency/thread_mode.c
   process mode: fork() → close all other fds → engine_main src/concurrency/process_mode.c
```

The two modes differ **only** in how `engine_main` is started and how the shared history is locked. Everything between
`tab_engine_spawn()` and the pipes is identical, which is why one test suite verifies both.

### IPC protocol (`include/ipc.h`, `src/concurrency/ipc.c`)
Frames `[type:1][len:4][payload]` over two pipes per tab (created `O_CLOEXEC`).
GUI→engine: `L` line, `I` Ctrl+C, `Z` Ctrl+Z, `D` Ctrl+D, `Q` quit. Engine→GUI: `O` stdout text, `E` stderr text,
`P` ready for input (payload `1` = continuation prompt), `W` cwd, `X` exiting, `K` clear, `R` reload key bindings.
Frames ≤ 4 KiB are written with one `write()` (atomic). The reader reassembles partial reads. Closing the control pipe is
equivalent to `Q`, so a vanished GUI makes every engine exit and kill its jobs.

### GUI event loop
One `poll()` waits on the X connection fd and every tab's output pipe (500 ms timeout only for the cursor blink and for
reaping closing process-mode tabs). X events are drained with `XPending`/`XNextEvent`; at most 16×8 KiB is read from a tab
per iteration so a chatty program cannot starve the UI; the window is redrawn once per iteration when dirty. Nothing blocks
on a running command and nothing spins. Drawing goes to an off-screen pixmap, then one `XCopyArea`. Lines are wrapped
**at draw time**, so a resize never touches stored text. Ctrl+C/Ctrl+Z are ordinary key events that become `I`/`Z`
messages; the GUI never signals anything itself.

### Thread mode
Each tab = one pthread running `engine_main`. Per-tab state lives in a heap `ShellState`; the only shared object is the
history (mutex). Things that are process-global in POSIX are handled explicitly:
* **cwd**: `chdir()` is process-wide, so the engine *never* calls it. `cd` updates `ShellState.cwd`; every path is resolved
  lexically (`path_resolve`), globbing uses its own `opendir` walk, and each child `chdir()`s after `fork`.
* **environment**: exported variables are per tab; the child sets `environ` (child-only memory) before `execvp`, so PATH
  lookups use the tab's PATH.
* **signals**: SIGPIPE ignored; SIGINT/SIGTERM/SIGHUP set a flag in the GUI. Children reset all dispositions and the signal mask.
* **fork in a multithreaded process**: between `fork` and `exec` the child only calls `setpgid`, `dup2`, `open`, `chdir`,
  `execvp`, `write`, `_exit`. All fds are `O_CLOEXEC`, so concurrent forks in other tabs cannot leak pipe ends (which would
  prevent EOF).
* children are waited for **by pid** (`waitpid(pid, …)`), never `-1`, so tabs cannot steal each other's statuses.

### Process mode
`fork()` per tab from the GUI process (which has no other threads). The child ignores SIGINT/SIGQUIT/SIGTSTP/SIGPIPE (the
launching terminal's Ctrl+C must not kill it), sets `PR_SET_PDEATHSIG(SIGTERM)`, closes every inherited fd except its two
pipe ends (this includes the X socket — only the parent touches Xlib), runs `engine_main`, and ends with `_exit`.
The parent detects the end of a tab by EOF on its output pipe: an `X` message first = normal exit; EOF without it = crash →
message in the tab, tab closes on next key, nothing else is affected. Closed tabs are reaped asynchronously (`dying` list),
with SIGKILL after 3 s at shutdown. Children of a crashed tab die through `PR_SET_PDEATHSIG(SIGHUP)` set in each stage.

## 2. Shared history (`src/features/history.c`)
One `History` struct: ring buffer of 10 000 × 1 KiB entries, absolute serial counter, file path, lock.
* thread mode: `malloc`'d, `pthread_mutex_t`.
* process mode: anonymous `mmap(MAP_SHARED)` created *before* any fork, process-shared `sem_t` (`sem_init(pshared=1)`).
  A tab killed while holding the lock would freeze everyone, so `lock()` uses `sem_timedwait` (2 s) and then assumes the holder
  died and posts on its behalf; critical sections are microseconds long.
* persistence: append-only file written under the same lock (`O_APPEND`, one `write`); trimmed to the last 10 000 lines
  (temp file + `rename`) when opened. File failures are non-fatal. Consecutive duplicates and blank lines are skipped.
* readers (Up/Down, Ctrl+R, `history`) all go through the locked API; the GUI reads the same object in both modes.

## 3. Shell pipeline: Input → Completeness → Lex/Parse → Expansion → Execution → Status → Prompt

1. **Input** (`shell.c: sh_process_line`): lines from the GUI are queued (type-ahead). A line is appended to
   `ShellState.partial`.
2. **Completeness** (`lexer.c: input_completeness`, `script.c: script_open_blocks`): open quotes, trailing `\`, open `$(`/backtick,
   trailing `| && ||`, or an unclosed `if` ⇒ send prompt `P` with `1` (GUI shows `>`), execute nothing.
   A Ctrl+C keeps its position behind queued lines (`INTR_MARK`) and discards the partial command.
3. **Lexing** (`lexer.c`): a state machine over quotes, escapes, nested `$( )` and backticks. Words keep their raw text,
   so an operator inside quotes or a substitution can never be mistaken for an operator.
4. **Parsing** (`parser.c`): tokens → `CmdList` of `Pipeline`s (`Cmd` = raw words + redirections), connectors `; & && ||`.
   Syntax errors give a message and status 2; nothing runs.
5. **Statements** (`script.c`): the same text is split into statements for `if/elif/else/fi`; the whole thing is validated
   (missing `fi`, stray `elif/else/then/fi`, command syntax per statement) **before the first command runs**.
6. **Expansion** (`expansion.c`): per word: quote removal, `~`, `$name`, `${name}`, `$? $# $$ $0-$9`, `$(…)`/backticks, then
   word splitting of *unquoted* results, then globbing of *unquoted* wildcard characters (a per-character quoted/unquoted
   mask is kept). `$(…)` re-enters the executor with stdout diverted into a buffer (`ShellState.capture`), so nested
   substitutions evaluate innermost first and use exactly the same parser and executor — no hidden shell.
7. **Execution** (`executor.c`): each stage is planned as *external*, *builtin* or *script*. Standalone builtins/scripts run
   inside the engine (this is what lets `cd`/`source` change the tab's state). In pipelines or the background they run in
   the engine first and a forked child replays their output into the pipe and exits with their status. Externals go through
   `pipeline_start`.
8. **Status**: `$?` = last pipeline's last command; 128+signal for signalled children; 127 not found; 126 not executable.

### Pipelines and redirection (`src/process/pipeline.c`)
N−1 pipes + one stdout pipe + one stderr pipe (+ a stdin pipe for foreground jobs) are created with `pipe2(O_CLOEXEC)`.
Child *i*: `setpgid`, reset signals, `PDEATHSIG`, `chdir`, then **opens its redirections itself** and wires
`dup2` — an input redirect that fails prints the error and `_exit(1)` *before* `exec`, so the command never runs.
Output files are opened with `O_TRUNC`/`O_APPEND` as appropriate. Because every stage opens its own file descriptor there is no
shared file offset between jobs reading the same file. The parent closes all pipe ends it does not need (the write ends of the
output pipes are closed right after the last fork, so EOF is delivered) and registers a `Job`.
`stderr` of all stages goes to one pipe shown in red; `stdout` of the last stage to another.

### Jobs and signals (`src/process/jobs.c`)
A `Job` has pgid, pids, per-process state, text, and the parent's pipe ends. `jobs_update` is a non-blocking
`waitpid(pid, WNOHANG|WUNTRACED|WCONTINUED)` sweep. **`sh_pump()`** is the engine's single wait primitive: `poll()` on the
control pipe + every job's stdout/stderr pipe, then a reap sweep. The foreground wait loop, the idle loop and multiWatch all
use it, so Ctrl+C (`kill(-pgid, SIGINT)`), Ctrl+Z (`SIGTSTP`), and `Ctrl+D` are handled immediately while a command runs, and
background output keeps flowing while you type. A stopped foreground job becomes a background job (`[1]+ Stopped`); `fg`
sends `SIGCONT` and waits; `bg` sends `SIGCONT`; finished background jobs are announced, drained, and their resources freed.
Signals are only ever sent to `-pgid` of a job owned by *this* tab's engine.

### multiWatch (`src/features/multiwatch.c`)
Parses `["cmd", …]` from the *raw* words (so quoting is preserved), pre-parses every command, then runs each as a hidden
background job with an `on_data` callback. The loop is `sh_pump(timeout = next restart time)`; finished jobs are drained,
freed and restarted 2 s later. Ctrl+C sets `interrupted`, the loop ends, and every job is terminated (SIGTERM → grace →
SIGKILL) and reaped before the prompt returns.

### Scripts (`src/shell/script.c`)
`./x.sh`, `bash x.sh`, `bash -c`, `source`/`.`. Execution of a script = `script_run_text(separate=1)`: a clone of the *exported*
variables, its own `$0-$9/$#`, a private cwd, restored afterwards. `source` = `separate=0`: same shell, persistent variables
and `cd`; positional parameters are replaced only if arguments were given and restored afterwards. Conditions are ordinary
commands (`[`/`test` are builtins; `if grep -q …; then` works too). `exit` inside a script sets `script_exit`.

## 4. Data structures and ownership (summary)

| Type | Defined in | Owner / lifetime |
|---|---|---|
| `Tab` (GUI view) | `tab.h` | GUI; freed in `tab_close`. Holds `TabHandle` (GUI-side pipe ends) |
| `ShellState` | `shell.h` | the engine thread/process; created in `engine_main`, freed at its end |
| `Cmd`/`Pipeline`/`CmdList` | `parser.h` | created by `parse_list`, freed by `cmdlist_free` by the caller |
| `Stage` | `pipeline.h` | built by the executor per pipeline, freed right after launch |
| `Job` | `jobs.h` | slot in `ShellState.jobs`; owns its parent-side fds until `job_free` |
| `History` | `history.h` | GUI creates (`shared_history_create`), engines only use; destroyed after all engines stopped |
| `Config` | `config.h` | GUI (value type); reloaded on the `R` message |
| `ScriptContext` | `script.c` | stack frame of `script_run_text`: `StmtList` + saved vars/args/cwd |

Descriptor ownership: GUI owns `ctl_w`/`out_r`; the engine owns `ctl_fd`/`out_fd` and closes them at exit; a job owns its
`out_fd/err_fd/in_fd` (parent side); children own nothing beyond 0/1/2 (everything else is `CLOEXEC` or explicitly closed).
Allocation failure aborts with a message (`xmalloc`); all syscalls are checked, `EINTR` and partial writes are handled
(`write_all`, `ipc_reader_fill`).

## 5. Decisions where the specification was silent
* Wildcards implemented with own directory walk + `fnmatch` (hidden files only when the pattern starts with `.`; no match
  ⇒ pattern stays literal, like bash).
* `&&`, `||`, `;` added because they are cheap with the pipeline parser and appear in real scripts.
* stdout/stderr are separate pipes so `$(cmd)` captures only stdout and the GUI can colour stderr.
* Builtins are normal pipeline citizens when they have no side effects on shell state (`echo`, `pwd`, `history`, `jobs`,
  `test`); the state-changing ones (`cd exit fg bg source export unset multiWatch bind clear`) refuse to run in a pipeline or
  in the background with a clear error.
* Directory layout deviations from the suggested tree: added `include/expansion.h`, `concurrency.h`, `process_utils.h`,
  `headless.h`, `src/headless.c`, `tests/gui/`, `tests/unit/test_config.c`, `tests/integration/test_features.sh`,
  `scripts/screenshots.sh`; `status.c` was merged into `process_utils.c`; `tests/fixtures/` was dropped because every test
  creates its fixtures in a temporary directory.
