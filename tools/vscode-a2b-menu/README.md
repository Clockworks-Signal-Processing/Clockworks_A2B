# A2B Menu (VS Code extension)

Adds an **A2B** icon to the VS Code activity bar. It opens a panel with one section per
target (in this repository: the RP2040 fixed app, bench console and minimal example, the
Raspberry Pi bench and the Windows bench), each with **Clean**, **Build**, **Debug** and
**Release**:

| Item | Does |
|---|---|
| Clean | Removes the target's Debug and Release build folders |
| Build | Builds the Debug version |
| Debug | Builds, loads and starts the debugger |
| Release | Builds the Release version, loads it and runs it |

The **Stop All** button in the panel's title bar ends every debug session and running
task. Use it to recover when something is stuck, e.g. after the target lost power
mid-session. Clicking an action again while it's still starting does nothing, and Debug
offers to stop an already-running session and start again instead of starting a second
one. Before starting a debug session, the menu also ends any background pre-launch task
(such as gdbserver on the Pi) left over from an earlier session.

**Command lines.** VS Code prints a task's command line at the top of its terminal
("Executing task: ..."), but not a debug session's. The menu does: when any debug session
starts (from the menu or from **Run and Debug**), its program and arguments, with settings
such as `${config:a2b.windowsBoard}` filled in, are written to the **Debug Console** and to
the **A2B** output channel (**View: Output**, then **A2B**), for example:

```
A2B menu: Windows bench: Debug: <repo>/build/a2b-bench-windows/Debug/examples/bench/a2b_bench.exe --board AB0020 --ignore-eeprom start
  (in <repo>)
```

A session whose program runs on the target (the Pi's, through gdbserver) names the
pre-launch task whose terminal shows that command line instead.

The menu is only a front end. The sections come from the `a2b.menu` setting in
`.vscode/settings.json`, and each item names a task from `.vscode/tasks.json`
(`"task:<label>"`) or a launch configuration from `.vscode/launch.json`
(`"launch:<name>"`). Everything still works from **Run Task** and **Run and Debug**
without the extension. An item whose task or configuration doesn't exist yet shows as
"not set up yet".

**Settings.** The tasks and launch configurations take per-PC values from `a2b.*`
settings, which go in each PC's VS Code User settings (a value in `.vscode/settings.json`
would override every PC's). Before anything runs, the menu says which ones it needs are
empty. `a2b.windowsBenchArgs` may stay empty: it holds console commands the Windows bench
runs before `start`, separated by `;` (e.g. `ignoreEEPROM 1; link 2 custom`), passed as
`--commands=...`. The extension gives it its empty default, so a launch that uses it needs
version 0.5.3 or later installed, or the setting set.

## Install

Once per machine, and again after changing the extension:

```
powershell -ExecutionPolicy Bypass -File tools\vscode-a2b-menu\install.ps1
```

Then run **Developer: Reload Window** in VS Code. The script packages this folder as a
`.vsix` in `%TEMP%` and installs it with `code --install-extension`; it needs no Node.js
or `vsce`. The panel appears only in workspaces that have an `a2b.menu` setting.

To remove it: Extensions view, **A2B Menu**, Uninstall.
