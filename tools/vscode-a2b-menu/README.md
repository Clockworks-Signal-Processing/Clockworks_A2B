# A2B Menu (VS Code extension)

Adds an **A2B** icon to the VS Code activity bar. It opens a panel with one section per
target (RP2040, Raspberry Pi, Windows), each with **Clean**, **Build**, **Debug** and
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

The menu is only a front end. The sections come from the `a2b.menu` setting in
`.vscode/settings.json`, and each item names a task from `.vscode/tasks.json`
(`"task:<label>"`) or a launch configuration from `.vscode/launch.json`
(`"launch:<name>"`). Everything still works from **Run Task** and **Run and Debug**
without the extension. An item whose task or configuration doesn't exist yet shows as
"not set up yet"; the Windows items fill in when the Windows port adds its tasks.

## Install

Once per machine, and again after changing the extension:

```
powershell -ExecutionPolicy Bypass -File tools\vscode-a2b-menu\install.ps1
```

Then run **Developer: Reload Window** in VS Code. The script packages this folder as a
`.vsix` in `%TEMP%` and installs it with `code --install-extension`; it needs no Node.js
or `vsce`. The panel appears only in workspaces that have an `a2b.menu` setting.

To remove it: Extensions view, **A2B Menu**, Uninstall.
