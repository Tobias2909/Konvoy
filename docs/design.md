# Konvoy design

Konvoy is one KDE window that runs several Claude Code sessions side by side
and shows at a glance which of them is waiting for you.

## Window

```
┌──────────────┬────────────────────────┬────────────────────────┐
│ sidebar      │ ▌A  ~/project  title ● │ ▌B  ~/other   title ▲  │
│              │                        │                        │
│ pane rows    │   Konsole terminal     │   Konsole terminal     │
│              │                        │                        │
│              ├────────────────────────┼────────────────────────┤
│ usage block  │ ▌C  ~/third   title ✓  │ ▌A  ~/fourth  title ○  │
└──────────────┴────────────────────────┴────────────────────────┘
```

* **Sidebar** on the left, 290 px by default. Its width can be dragged, and
  it collapses to a 44 px strip that only shows the state icons, account
  stripes and small usage bars. Top: buttons for new pane, overview/focus,
  equal sizes and collapse. Middle: one card per pane. Bottom: the usage
  block.
* **Pane cards**: a stripe in the state colour on the left, the state icon,
  the account badge, the folder, the state text with its duration and the
  `Alt` number on the first line, the chat title on the second. Panes that
  need you get a card tinted in the state colour. The focused pane gets an
  outline in the Plasma accent colour.
* **Panes** on the right. Each pane is a real Konsole terminal embedded
  through `konsolepart`, the component Yakuake and Kate's terminal panel use.
  Every pane uses the Konsole profile `Konvoy`, so its font and colours can
  differ from normal Konsole.
* **Header bar** on every pane: the account letter in the account's colour,
  the folder, the chat title and the state icon. It is also the drag handle.
* One global window. A second launch raises the running one.

## Accounts

Konvoy works with any number of Claude Code accounts, one or more. Nothing
in the code assumes a fixed count. Each account is an entry in the config
with a letter, a colour, the command that starts it and its config dir
(`CLAUDE_CONFIG_DIR`). Accounts without a colour get the next one from a
built in palette. The sidebar, the new pane dialog and the usage fetcher
all follow the list. A pane runs an interactive shell
that starts the account command, so shell functions and wrappers around
`claude` keep working. The account of a pane can change while it runs, the
hook reports the config dir it sees, and the pane follows.

## Panes and layout

* **Overview mode** shows all panes in an automatic grid: 1 pane fills the
  area, 2 sit side by side, 3 are 2+1, 4 are 2×2, 6 are 3×2. The column
  count can be changed.
* The dividers can be dragged to make panes bigger or smaller. **Equal
  sizes** (button, `Ctrl+Shift+0`) restores the default. Sizes belong to the
  grid slots and are saved with the layout. Adding or removing a pane or
  changing the column count resets them to equal.
* **Focus mode** shows one pane big. The others keep running, hidden.
* **Reordering** by dragging a header bar onto another pane, by dragging
  rows in the sidebar, or with `Ctrl+Shift+Left/Right`. All three change the
  same order. Reordering swaps panes between slots, the slot sizes stay.
* Clicking a sidebar row focuses that pane. In focus mode it also becomes
  the big one.

| Key | Action |
|---|---|
| `Alt+1` … `Alt+9` | jump to pane |
| `Ctrl+Shift+E` | toggle overview and focus |
| `Ctrl+Shift+T` | new pane |
| `Ctrl+Shift+W` | close pane |
| `Ctrl+Shift+Left/Right` | move pane |
| `Ctrl+Shift+0` | equal sizes |

Copy, paste and search come from the embedded Konsole terminal.

## New pane dialog

* **Account**: nothing is preselected. Each account shows its current 5 hour
  and 7 day usage next to its letter.
* **Folder**: pinned folders first (a star pins or unpins, they keep the
  order they were pinned in), then recent folders, then a browse button.
  The last used folder is selected.
* **Conversation**: new chat (default), or continue one of the recent chats
  in that folder.

## States

| State | Icon | Meaning |
|---|---|---|
| working | animated | Claude is running |
| needs permission | strong colour | a permission dialog is open |
| question | strong colour | Claude asks you something (question or plan approval) |
| done | green | Claude finished and you have not looked yet |
| idle | grey | done, and the pane has had keyboard focus since |
| exited | red | Claude is gone, the shell is still open, a restart button shows |

Each sidebar row shows the state icon, the `Alt` number, the account
letter, the folder, the chat title and how long the pane has been in its
state.

### Where the state comes from

1. **Claude's own status code.** Claude Code reports `working`, `blocked`
   (permission, question, auth) and `done` to terminals that support the
   OSC 7501 program status protocol. If Konvoy can read it from the
   embedded terminal, it is the primary source.
2. **Hooks.** Konvoy's hook script records events per pane:

   | Hook | State |
   |---|---|
   | `SessionStart` | registers the session id for restore |
   | `UserPromptSubmit` | working |
   | `PermissionRequest` | needs permission |
   | `PreToolUse` on `AskUserQuestion` or `ExitPlanMode` | question |
   | `PostToolUse`, `PostToolUseFailure` | working |
   | `Stop`, `StopFailure` | done |
   | `SessionEnd` | exited |

   No hook fires when you interrupt Claude with Esc, and probably none after
   a denied permission. Source 1 covers that. Without it a fallback applies:
   a pane in `working` that prints nothing for about 10 seconds becomes
   `done`, because Claude's spinner normally animates without pause.
3. **The terminal itself** reports when the Claude process ends.

### Hooks setup

`konvoy --install-hooks` appends Konvoy's entries to the Claude Code
`settings.json` and shows what it adds first. It never edits or removes
other hooks, and Claude Code runs all hooks of an event, so existing hooks
keep working everywhere. `konvoy --remove-hooks` removes exactly Konvoy's
entries. The hooks only observe. They run async and never return an allow
or deny decision. Outside a Konvoy pane (`KONVOY_PANE` unset) they exit at
once.

## Notifications

A KDE notification for needs permission, question and done, but only when
that pane is not on screen or the window is not focused. Clicking it jumps
to the pane. No sound.

## Usage block

For every account a block with the account badge and name, then one line
for the 5 hour and one for the 7 day limit. Each line has a bar in the
account colour, the percent (orange from 80, red from 95) and the reset
time. A warning line shows when the login has expired, and the bars are
dimmed then because the numbers are the last known ones.

* A small fetcher (Python, standard library only) reads the usage with each
  account's own login, the same numbers Claude Code's `/usage` shows. It
  writes them to `$XDG_RUNTIME_DIR/konvoy/usage.json`.
* It runs only while something shows the numbers, and fetches every 120
  seconds. The usage service rate limits bursts, and running sessions share
  that budget.
* It never refreshes a login itself. Claude Code rotates refresh tokens
  under its own lock, so a second refresher could log the account out.
* **The Konvoy window has no network code.** It only reads that file.
* An optional external command can add extra fields to the same file. The
  window shows them if present.

## Closing and restoring

* Closing the window while panes are working shows a warning that lists
  them.
* On the next start Konvoy restores the panes with their order, sizes,
  folders and accounts, and continues each chat with `claude --resume`.

## Build steps

| Step | Content |
|---|---|
| 0 | sidebar mockup at real size |
| 1 | window, panes, grid and focus mode, header bars, new pane dialog, shortcuts, close warning |
| 2 | state tracking, hooks setup, sidebar rows, notifications |
| 3 | usage fetcher and usage block |
| 4 | drag resize and equal sizes, drag reorder, sidebar width and collapse, restore, continue chat list |

## Technology

C++ with Qt 6 and KDE Frameworks 6, built with CMake and extra-cmake-modules.
The usage fetcher and the hook script are separate small programs.
