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

* **Sidebar** on the left, 290 px by default. Its width can be dragged
  (230 to 640 px), and it collapses to a 44 px strip that only shows the
  state icons, account stripes and small usage bars. Top: buttons for new
  pane, overview/focus, equal sizes and columns, and on the right collapse.
  Middle: one card per pane. Bottom: the usage block.
* **Pane cards**: a stripe in the state colour on the left, the state icon,
  the account badge, the folder, the state text with its duration and the
  `Alt` number on the first line, the chat title on the second. Panes that
  need you get a card tinted in the state colour. The focused pane gets an
  outline in the Plasma accent colour. A minimized pane's card stays in its
  place, faded: no fill, a dashed outline, dim text and a small bar; its state
  colours stay. The collapsed strip shows it with a hollow account stripe.
* **Panes** on the right. Each pane is a real Konsole terminal embedded
  through `konsolepart`, the component Yakuake and Kate's terminal panel use.
  Every pane uses the Konsole profile `Konvoy`, so its font and colours can
  differ from normal Konsole.
* **Header bar** on every pane: the account letter in the account's colour,
  the folder, the chat title, the state icon and a minimize button at the
  right end. It is also the drag handle.
* One global window. A second launch raises the running one.

## Accounts

Konvoy works with any number of Claude Code accounts, one or more. Nothing
in the code assumes a fixed count. Each account is an entry in the config
with a letter, a colour, the command that starts it and its config dir
(`CLAUDE_CONFIG_DIR`). Accounts without a colour get the next one from a
built in palette. The sidebar, the new pane dialog and the usage helper
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
  grid slots and are saved with the layout. Adding, removing, minimizing or
  bringing back a pane or changing the column count resets them to equal.
* **Focus mode** shows one pane big. The others keep running, hidden.
* **Minimize** (the header's button, `Ctrl+Shift+M`) takes a pane out of the
  grid. It keeps running and its card keeps its place in the sidebar. A click
  on the card, its `Alt` number or the Show button of its notification brings
  it back to its old place. Minimizing the selected pane hands the keys to the
  next pane on screen, as closing does. With every pane minimized the grid
  says so.
* **Reordering** by dragging a header bar onto another pane (the two swap),
  by pressing and holding a sidebar card until it lifts and dropping it at
  another place (dragging it right away works too), or with
  `Ctrl+Shift+arrows`. All of them change the same order. The slots keep
  their sizes, only the panes move between them. The arrows move a pane
  among the panes on screen; minimized ones keep their place.
* Focus mode keeps the overview sizes and gives them back when it ends.
* Clicking a sidebar row focuses that pane. In focus mode it also becomes
  the big one.
* **The wheel** over the sidebar steps through the panes on screen in the
  sidebar's order, one pane per notch, and stops at the first and the last.
  The pane it lands on gets the keys, and in focus mode it becomes the big
  one. Minimized panes are skipped. A touchpad's small steps add up to a notch
  first, so a swipe does not race through all panes. With none selected, the
  first notch selects the pane that was selected last. The cards scroll along
  to keep that card in view instead of scrolling by themselves.

| Key | Action |
|---|---|
| `Alt+1` … `Alt+9` | jump to pane (brings a minimized one back) |
| `Ctrl+Shift+E` | toggle overview and focus |
| `Ctrl+Shift+T` | new pane |
| `Ctrl+Shift+W` | close pane |
| `Ctrl+Shift+M` | minimize pane |
| `Ctrl+Shift+Left/Right` | move pane |
| `Ctrl+Shift+0` | equal sizes |

Copy, paste and search come from the embedded Konsole terminal.

## New pane dialog

* **Account**: nothing is preselected. Each account shows its current 5 hour
  and 7 day usage next to its letter.
* **Folder**: pinned folders first (a star pins or unpins, they keep the
  order they were pinned in), then recent folders, then a browse button.
  The last used folder is selected.
* **Conversation**: new chat (default), or continue one of the 15 latest
  chats in that folder, newest first, with their title and age. The list
  comes from the chosen account's `projects` folder (all accounts' before
  one is chosen). A chat's title is its `/rename` name, else Claude's own
  title, else its last prompt, read from the end of the chat file. Chats
  already open in a pane are listed but greyed out.

## States

| State | Icon | Meaning |
|---|---|---|
| working | animated | Claude is running |
| in background | blue, three dots | the turn is over, but background shells, monitors or subagents still run |
| needs permission | strong colour | a permission dialog is open |
| question | strong colour | Claude asks you something (question or plan approval) |
| done | green | Claude finished and you have not looked yet |
| idle | grey | done, and the pane has had keyboard focus since |
| exited | red | Claude is gone, the shell is still open, a restart button shows |

Each sidebar row shows the state icon, the `Alt` number, the account
letter, the folder, the chat title and how long the pane has been in its
state.

A pane looks selected (card outline, accent line under its header) only
while typing goes into it: the window is active and the pane has the keyboard
focus. Another window, a dialog or a click on the sidebar's empty space leaves
none selected, and the terminal cursor turns hollow. A pane that finishes
while selected skips done and goes straight to idle; every other finish shows
done, even for a single pane on screen. With none selected, the keys that
close, minimize or move the current pane do nothing; focus mode keeps showing
the pane that was selected last. Rearranging the grid (a pane opens, closes,
moves or is minimized) never passes the keys through another pane, so it
cannot take away that pane's done.

### Where the state comes from

1. **Claude's terminal title.** Claude Code puts a status symbol in front of
   its title: it alternates `◐` and `◑` while it works and shows `✳`
   otherwise. A spinning symbol means working, a still one means the turn is
   over or Claude waits for an answer. This also catches an interrupt with
   Esc, which no hook reports. Konsole reports title changes about 2 seconds
   late, so states follow with that delay.
2. **Hooks** say what Claude waits for. Konvoy's hook script appends one line
   per event to `$XDG_RUNTIME_DIR/konvoy/events/<pane>`, which Konvoy watches:

   | Hook | Meaning |
   |---|---|
   | `SessionStart` | session id (for restart and restore), idle; not after a compact |
   | `UserPromptSubmit` | a new turn starts |
   | `PermissionRequest` | needs permission (question for `AskUserQuestion` and `ExitPlanMode`) |
   | `PreToolUse` on `AskUserQuestion` or `ExitPlanMode` | question, unless it has an `agent_id` |
   | `Notification` | `permission_prompt` and MCP elicitation dialogs as a backup |
   | `Stop`, `StopFailure` | done, or in background while its `background_tasks` list is not empty |
   | `SessionEnd` | exited |

   The script also passes the session's `CLAUDE_CONFIG_DIR`, so the account
   badge follows `/switch` right away. A `PreToolUse` with an `agent_id` comes
   from a subagent or from a hidden copy of the chat that Claude Code runs by
   itself, such as the one that suggests the next prompt after a turn. Those
   copies refuse every tool after the hook ran, so their question never
   shows; the script drops the event. A question that does show also fires
   `PermissionRequest`. A compact fires `SessionStart` too, but the session
   goes on in the same process with its background tasks, so the script drops
   that one as well.
3. **The processes.** Every 2 seconds a pane checks whether its claude process
   is still the same. A new one means an account switch, none means exited.
   When a background task ends, Claude starts a turn by itself (it fires
   `UserPromptSubmit`), and that turn's `Stop` brings the new list. A shell
   stopped from Claude's task list may end without such a turn, so a pane in
   background whose list holds only shells turns done once no shell has run
   below claude for about 8 seconds. Command monitors count as shells there,
   because Claude Code lists them as such. Its own housekeeping tasks (dream,
   auto-mode scan, memory import) do not count.
4. **Declined dialogs.** Declining a permission fires no hook. When a dialog
   waits and the user presses Enter, Esc or a digit in that pane, and Claude
   neither starts working nor reports anything within 2 seconds, the dialog
   is taken as dismissed and the pane turns idle.

A dialog hook older than the last start of work is ignored, it was answered.
A pane that finishes while it has focus in the active window goes straight
to idle. A pane that shows `exited` offers a Restart button in its header,
which starts the account command again with `--resume` and the last session
id.

### Hooks setup

`konvoy --install-hooks` appends Konvoy's entries to every account's Claude
Code `settings.json` (symlinked files once) and shows what it adds first. It
never edits or removes other hooks, and Claude Code runs all hooks of an
event, so existing hooks keep working everywhere. `konvoy --remove-hooks`
removes exactly Konvoy's entries, `konvoy --hooks-status` lists them. The
hooks only observe. They print nothing and never return an allow or deny
decision. Outside a Konvoy pane (`KONVOY_PANE` unset) they exit at once.
They run synchronously, which keeps the events in order; each takes a few
milliseconds.

## Notifications

A KDE notification for needs permission, question and done, but only when
that pane is not on screen (hidden by focus mode or minimized) or the window
is not focused. Clicking it jumps to the pane and brings a minimized one
back. No sound.

Konvoy's taskbar icon carries the number of panes that are done or wait for
a permission or an answer, sent with the `com.canonical.Unity.LauncherEntry`
signal that Plasma's task manager shows. At 0, and when Konvoy closes, the
number goes away.

## Usage block

For every account a block with the account badge and name, then one line
for the 5 hour and one for the 7 day limit. Each line has a bar in the
account colour, the percent (orange from 80, red from 95) and the reset
time. A click on any reset time switches all of them between the clock time
("04:30") and the time left ("2h 15m"), and the choice is kept for the next
start. When no pane of an account is open, its bars are dimmed and its name
row says from when the numbers are.

* A small helper, `konvoy-usage` (Python, standard library only), collects
  the numbers and writes them to `$XDG_RUNTIME_DIR/konvoy/usage.json`. **It
  sends no requests, and neither does the window.** Every number comes from
  Claude Code itself.
* Konvoy starts it and stops it again, so it runs only while Konvoy shows the
  numbers.
* **Status line handoff.** Claude Code hands its status line the current 5
  hour and 7 day numbers after each request. `konvoy-statusline` takes that
  same input, and in a Konvoy pane (`KONVOY_PANE` is set) it writes the
  numbers to `$XDG_RUNTIME_DIR/konvoy/statusline/$KONVOY_PANE.json`, only
  when they change:

  ```
  {"config_dir": "/home/me/.claude",
   "five_hour": {"percent": 32, "resets_at": 1791945000},
   "seven_day": {"percent": 68, "resets_at": 1792393200}}
  ```

  `config_dir` is the session's `CLAUDE_CONFIG_DIR`, or `~/.claude` when it is
  unset. Konvoy removes a pane's file when the pane closes, and all of them
  when it starts, so while one exists for an account a pane of it is open and
  its numbers count as live.
* **Saved numbers.** Without such a file the helper takes the numbers Claude
  Code saved in its own state file (`cachedUsageUtilization`), or the last
  ones it had, whichever are newer. They can be hours old, hence the dimmed
  bars and the time. Claude Code saves its numbers only when `/usage` is
  opened, so they can also be days old.
* **Kept on disk.** A reboot empties the runtime folder, so the helper also
  writes its file to `konvoy-usage.json` in the XDG state folder
  (`~/.local/state`), only when the numbers change, and starts from it when
  `usage.json` is gone. A config given with `--config` keeps it as
  `<file>.usage.json` instead.
* **Extra command.** `[Usage] ExtraCommand` runs every `ExtraInterval`
  seconds (900 by default). The file keeps when it last ran, so restarting
  Konvoy does not run it again early. It prints
  `{"accounts": {"<config dir>": {...}}}`, and those fields land under `extra`
  for that account. The window shows `"resets": {"available": N,
  "expires_at": EPOCH}` as "★ 1 reset · expires 22 Oct 18:00".
* The header says when the file itself has stopped updating.

## Closing and restoring

* Closing a pane or the window asks first only while Claude works, waits
  for an answer or runs background tasks there. Middle click on a card or a header closes a pane.
* On the next start Konvoy restores the panes with their order, slot sizes,
  folders and accounts, and continues each chat with `--resume` and the
  last session id the pane saw. Focus mode, the current pane, the minimized
  panes, the sidebar width and collapse and the window size come back too. The layout is saved
  a second after each change and on close, in `konvoystaterc` in the XDG
  state folder. A config given with `--config` keeps its own state next to
  it, so test runs never reopen the real panes. `--open` skips the restore.

## Build steps

| Step | Content |
|---|---|
| 0 | sidebar mockup at real size |
| 1 | window, panes, grid and focus mode, header bars, new pane dialog, shortcuts, close warning |
| 2 | state tracking, hooks setup, sidebar rows, notifications |
| 3 | usage block |
| 4 | drag resize and equal sizes, drag reorder, sidebar width and collapse, restore, continue chat list |

## Technology

C++ with Qt 6 and KDE Frameworks 6, built with CMake and extra-cmake-modules.
The usage helper and the hook script are separate small programs.
