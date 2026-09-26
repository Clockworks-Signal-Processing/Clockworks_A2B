// A2B menu: a sidebar panel with Clean / Build / Debug / Release for each target.
// The sections come from the workspace setting "a2b.menu". Each item runs a task
// ("task:<label>" from tasks.json) or starts a debug session ("launch:<name>" from
// launch.json); items whose task or configuration doesn't exist show as "not set up yet".
// The panel's title bar also has Stop All: end every debug session and running task.
// Anything that needs an a2b.* setting that isn't set on this PC (such as the Pi's address)
// says so instead of running, whether started from the menu, F5 or Run Task.
'use strict';

const vscode = require('vscode');

const ACTIONS = ['Clean', 'Build', 'Debug', 'Release'];
const ICONS = { Clean: 'trash', Build: 'tools', Debug: 'debug-alt', Release: 'rocket' };

function parseRef(text) {
    const m = /^(task|launch):(.+)$/.exec(typeof text === 'string' ? text.trim() : '');
    return m ? { kind: m[1], name: m[2].trim() } : undefined;
}

function workspaceFolder() {
    const folders = vscode.workspace.workspaceFolders;
    return folders && folders.length ? folders[0] : undefined;
}

function menuSections() {
    const menu = vscode.workspace.getConfiguration('a2b').get('menu');
    return Array.isArray(menu) ? menu : [];
}

function launchConfigs() {
    const folder = workspaceFolder();
    const configs = vscode.workspace.getConfiguration('launch', folder && folder.uri).get('configurations');
    return (Array.isArray(configs) ? configs : []).filter(c => c && c.name);
}

function launchNames() {
    return new Set(launchConfigs().map(c => c.name));
}

// tasks.json tasks first, so a task from another provider with the same name can't win
async function findTask(name) {
    const tasks = await vscode.tasks.fetchTasks();
    return tasks.find(t => t.name === name && t.source === 'Workspace') || tasks.find(t => t.name === name);
}

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

// ---- running debug sessions, by launch configuration name ----

const debugSessions = new Map();    // name -> Set<vscode.DebugSession>

function sessionName(session) {
    return session.configuration && session.configuration.name;
}

function trackSession(session, running) {
    const name = sessionName(session);
    if (!name) {
        return;
    }
    const set = debugSessions.get(name) || new Set();
    if (running) {
        set.add(session);
    } else {
        set.delete(session);
    }
    if (set.size) {
        debugSessions.set(name, set);
    } else {
        debugSessions.delete(name);
    }
}

// Ask the sessions to stop and wait (up to timeoutMs) for them to end. VS Code ends a
// session whose debug adapter doesn't answer, e.g. when the Pi has gone away.
async function stopSessions(sessions, timeoutMs) {
    await Promise.all(sessions.map(s => Promise.resolve(vscode.debug.stopDebugging(s)).catch(() => undefined)));
    const deadline = Date.now() + timeoutMs;
    while (sessions.some(s => (debugSessions.get(sessionName(s)) || new Set()).has(s)) && Date.now() < deadline) {
        await sleep(200);
    }
    sessions.forEach(s => trackSession(s, false));
}

// A background pre-launch task (e.g. gdbserver over ssh) that is still running from an
// earlier session would make VS Code skip it, and the build and deploy it depends on,
// and connect to a debug server that may be gone. End it so the new session starts fresh.
async function endStalePreLaunchTask(configName) {
    const config = launchConfigs().find(c => c.name === configName);
    const pre = config && config.preLaunchTask;
    const stale = pre ? vscode.tasks.taskExecutions.filter(e => e.task.name === pre && e.task.isBackground) : [];
    if (!stale.length) {
        return;
    }
    stale.forEach(e => e.terminate());
    const deadline = Date.now() + 5000;
    while (vscode.tasks.taskExecutions.some(e => e.task.name === pre) && Date.now() < deadline) {
        await sleep(200);
    }
}

// ---- settings a task or launch configuration needs ----
// Settings such as the Pi's address differ from PC to PC, so they live in each PC's User
// settings. Before anything runs, say which are missing, instead of letting ssh fail on an
// empty address. Checked for menu items, F5 / Run and Debug, and Terminal → Run Task.

function taskDefinitions() {
    const folder = workspaceFolder();
    const tasks = vscode.workspace.getConfiguration('tasks', folder && folder.uri).get('tasks');
    return Array.isArray(tasks) ? tasks.filter(t => t && t.label) : [];
}

// A menu item's task or launch configuration, as written in tasks.json / launch.json
function definitionOf(ref) {
    return ref.kind === 'task' ? taskDefinitions().find(t => t.label === ref.name)
                               : launchConfigs().find(c => c.name === ref.name);
}

// The "a2b.*" settings a task or launch configuration uses through ${config:a2b.<name>}:
// in the definition itself, its preLaunchTask, and the tasks those depend on.
function settingsUsed(definition) {
    const tasks = taskDefinitions();
    const used = new Set();
    const visited = new Set();
    const visit = def => {
        for (const m of JSON.stringify(def).matchAll(/\$\{config:(a2b\.[\w.]+)\}/g)) {
            used.add(m[1]);
        }
        [].concat(def.preLaunchTask || [], def.dependsOn || [])
            .filter(label => typeof label === 'string' && !visited.has(label))
            .forEach(label => {
                visited.add(label);
                const task = tasks.find(t => t.label === label);
                if (task) {
                    visit(task);
                }
            });
    };
    if (definition) {
        visit(definition);
    }
    return [...used].sort();
}

let lastWarning = { text: '', time: 0 };

// True if every a2b.* setting the definition needs is set; otherwise shows which aren't.
// Doesn't wait for the message to be closed, so the item can be run again right away.
function settingsReady(definition) {
    const config = vscode.workspace.getConfiguration();
    const missing = settingsUsed(definition).filter(key => !String(config.get(key) || '').trim());
    if (!missing.length) {
        return true;
    }
    const text = `A2B: ${missing.join(' and ')} ${missing.length > 1 ? 'are' : 'is'} not set. ` +
        `Set ${missing.length > 1 ? 'them' : 'it'} in your VS Code User settings on this PC.`;
    // A task and the tasks it depends on can each report the same thing
    if (text === lastWarning.text && Date.now() - lastWarning.time < 5000) {
        return false;
    }
    lastWarning = { text, time: Date.now() };
    vscode.window.showErrorMessage(text, 'Open Settings').then(choice => {
        if (choice === 'Open Settings') {
            vscode.commands.executeCommand('workbench.action.openGlobalSettings', { query: 'a2b.' });
        }
    });
    return false;
}

// F5 / Run and Debug: runs before the pre-launch task, so nothing is built or deployed
const settingsCheckForDebug = {
    resolveDebugConfiguration(folder, config) {
        return settingsReady(config) ? config : undefined;
    }
};

// Terminal → Run Task: a task can't be stopped before it starts, so end it as soon as it
// does. Its own error (e.g. from ssh) may show as well.
function settingsCheckForTask(event) {
    const task = event.execution.task;
    const definition = task.source === 'Workspace' && taskDefinitions().find(t => t.label === task.name);
    if (definition && !settingsReady(definition)) {
        event.execution.terminate();
    }
}

// ---- running menu items ----

// Items being started. A tree item runs its command on every click, so a double-click
// would otherwise start the same task or debug session twice.
const starting = new Set();
const REPEAT_GUARD_MS = 1500;

async function startLaunch(name) {
    const running = [...(debugSessions.get(name) || [])];
    if (running.length) {
        // A second session would wait on the same debug server (e.g. gdbserver on the Pi)
        const choice = await vscode.window.showWarningMessage(
            `"${name}" is already running.`, 'Stop It and Start Again', 'Cancel');
        if (choice !== 'Stop It and Start Again') {
            return;
        }
        await stopSessions(running, 10000);
    }
    await endStalePreLaunchTask(name);
    await vscode.debug.startDebugging(workspaceFolder(), name);
}

async function run(text) {
    const ref = parseRef(text);
    if (!ref) {
        vscode.window.showErrorMessage(`A2B menu: "${text}" should start with task: or launch:`);
        return;
    }
    if (starting.has(text)) {
        return;
    }
    starting.add(text);
    try {
        if (!settingsReady(definitionOf(ref))) {
            return;
        }
        if (ref.kind === 'task') {
            const task = await findTask(ref.name);
            if (!task) {
                vscode.window.showInformationMessage(`A2B menu: the task "${ref.name}" isn't set up yet.`);
                return;
            }
            await vscode.tasks.executeTask(task);
        } else {
            if (!launchNames().has(ref.name)) {
                vscode.window.showInformationMessage(`A2B menu: the launch configuration "${ref.name}" isn't set up yet.`);
                return;
            }
            await startLaunch(ref.name);
        }
    } finally {
        setTimeout(() => starting.delete(text), REPEAT_GUARD_MS);
    }
}

// Stop All: every debug session and every running task, then forget what the menu
// thought was running.
async function stopAll() {
    const sessions = [].concat(...[...debugSessions.values()].map(set => [...set]));
    const tasks = vscode.tasks.taskExecutions.slice();
    await Promise.resolve(vscode.debug.stopDebugging()).catch(() => undefined);
    tasks.forEach(e => e.terminate());
    const deadline = Date.now() + 10000;
    while ((debugSessions.size || vscode.tasks.taskExecutions.length) && Date.now() < deadline) {
        await sleep(200);
    }
    debugSessions.clear();
    starting.clear();
    const left = vscode.tasks.taskExecutions.length;
    vscode.window.showInformationMessage(
        `A2B menu: stopped ${sessions.length} debug session(s) and ${tasks.length} task(s)` +
        (left ? `; ${left} task(s) still ending.` : '.'));
}

// ---- the tree ----

class MenuProvider {
    constructor() {
        this._onDidChange = new vscode.EventEmitter();
        this.onDidChangeTreeData = this._onDidChange.event;
        this.taskNames = new Set();
    }

    async refresh() {
        try {
            this.taskNames = new Set((await vscode.tasks.fetchTasks()).map(t => t.name));
        } catch (err) {
            this.taskNames = new Set();
        }
        this._onDidChange.fire();
    }

    exists(ref) {
        if (!ref) {
            return false;
        }
        return ref.kind === 'task' ? this.taskNames.has(ref.name) : launchNames().has(ref.name);
    }

    getTreeItem(element) {
        return element;
    }

    getChildren(element) {
        if (!element) {
            return menuSections().map((section, i) => {
                const item = new vscode.TreeItem(String(section.target || `Target ${i + 1}`),
                    vscode.TreeItemCollapsibleState.Expanded);
                item.id = `target:${i}`;
                item.section = section;
                return item;
            });
        }
        const items = (element.section && element.section.items) || {};
        const names = ACTIONS.filter(a => a in items).concat(Object.keys(items).filter(a => !ACTIONS.includes(a)));
        return names.map(action => {
            const ref = parseRef(items[action]);
            const ok = this.exists(ref);
            const item = new vscode.TreeItem(action, vscode.TreeItemCollapsibleState.None);
            item.id = `${element.id}:${action}`;
            item.iconPath = new vscode.ThemeIcon(ICONS[action] || 'play',
                ok ? undefined : new vscode.ThemeColor('disabledForeground'));
            if (ok) {
                item.tooltip = `${ref.kind === 'task' ? 'Task' : 'Launch configuration'}: ${ref.name}`;
            } else {
                item.description = 'not set up yet';
                item.tooltip = ref ? `No ${ref.kind === 'task' ? 'task' : 'launch configuration'} named "${ref.name}" yet`
                                   : `"${items[action]}" should start with task: or launch:`;
            }
            item.command = { command: 'a2bMenu.run', title: action, arguments: [items[action]] };
            return item;
        });
    }
}

function updateContext() {
    vscode.commands.executeCommand('setContext', 'a2bMenu.hasMenu', menuSections().length > 0);
}

function activate(context) {
    const provider = new MenuProvider();
    context.subscriptions.push(
        vscode.window.registerTreeDataProvider('a2bTargets', provider),
        vscode.commands.registerCommand('a2bMenu.refresh', () => provider.refresh()),
        vscode.commands.registerCommand('a2bMenu.run', text => run(text)),
        vscode.commands.registerCommand('a2bMenu.stopAll', () => stopAll()),
        vscode.debug.registerDebugConfigurationProvider('*', settingsCheckForDebug),
        vscode.tasks.onDidStartTask(settingsCheckForTask),
        vscode.debug.onDidStartDebugSession(session => trackSession(session, true)),
        vscode.debug.onDidTerminateDebugSession(session => trackSession(session, false)),
        vscode.workspace.onDidChangeConfiguration(e => {
            if (e.affectsConfiguration('a2b') || e.affectsConfiguration('tasks') || e.affectsConfiguration('launch')) {
                updateContext();
                provider.refresh();
            }
        }),
        vscode.workspace.onDidChangeWorkspaceFolders(() => {
            updateContext();
            provider.refresh();
        })
    );
    // Sessions that were already running when the extension started
    if (vscode.debug.activeDebugSession) {
        trackSession(vscode.debug.activeDebugSession, true);
    }
    updateContext();
    provider.refresh();
}

function deactivate() {}

module.exports = { activate, deactivate };
