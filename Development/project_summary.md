# Burrow: project summary

Read this instead of scanning the source. Details per file: `file_structure.md`. Keep this file current.

## What it is
A Paperclip-like agent hub that uses far fewer tokens. C++17, CMake + Ninja + MinGW gcc, no third party libraries: standard library (`std::string` UTF-8, `std::filesystem`) plus the Win32 API. The window is a plain Win32 window that hosts a **web interface** (HTML/CSS/JS in `Project/Web`) in WebView2. Windows only, open source (MIT).
The app only triggers and organises work; **Claude Code does the work** through the user's subscription (`claude -p`, no API key).
Data is flat files in a "vault" folder (Obsidian-like). UI language is Turkish or English (vault setting), dark theme is on.

## Build and test
- Build: `cmake -S Project -B Project/Build -G Ninja -DCMAKE_BUILD_TYPE=Release` then `cmake --build Project/Build`. Output `Project/Build/burrow.exe` (static, no DLLs).
- Tests: add `-DBURROW_BUILD_TESTS=ON`; `Build/core_selftest.exe` (offline, ~130 checks, includes a real child process), `--live` also runs real tiny Haiku requests (usage gate, runner, agent chain, cancel). Run `--live` after touching run_manager, claude_process, job_builder or conversation_router.
- App options: `--vault <folder>` (does not touch the saved vault list), `--page <name>` (overview, agents, chats, projects, tasks, skills, rules, runs, settings).
- Web files are compiled into the exe (RCDATA resources, generated `Build/embedded_web.rc`), so every Web edit needs a rebuild. For quick iteration set env `BURROW_WEB_DIR=<...>\Project\Web`: files are then read from disk and F5 / F12 work in the window. `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9222` lets a CDP client (screenshots, eval) attach to the page.

## Layers
- `Source/Core` (no UI): files <-> structs, job building, run queue, conversation routing, scheduler. Built as static lib `burrow_core`. All text is UTF-8 `std::string`; `text.h` (namespace `str`) has the string helpers, `platform.h` (namespaces `fsx`, `timeutil`, `winstr`) the files, folders, time and UTF-8 <-> UTF-16 conversion. Nothing in Core owns a timer or a thread loop: the owner calls `RunManager::Tick()` (~120 ms).
- `Source/Ui`: `burrow_app.cpp` (WinMain, command line), `web_frame` (the Win32 window; owns all state, the run machinery, the two `SetTimer` timers (run tick 120 ms, scheduler 15 s) and the snapshot; `Post()` runs a function from the message loop, outside the current callback), `web_api.cpp` (bridge commands, vault commands), `web_view_host` (WebView2 COM wrapper, serves `https://app.burrow/` from the embedded files), `embedded_web` (resource lookup), `folder_dialog` (Windows folder picker).
- `Web/` (no build step, plain ES modules): `app.js` shell, `pages/*` one module per tab, `agent_actions.js` (chat / run / agent editor), `forms.js`, `master_detail.js`, `mascot.js` (SVG mascots), `markdown.js` (escaping renderer), `api.js` (bridge), `ui.js` helpers, `app.css`.
- `Tests/core_selftest.cpp`.

## Vault on disk
`vault.ini`, `Agents/`, `Tasks/`, `Skills/`, `Rules/` (all `<id>.md`: `key: value` header between `---` lines, free text below), `Projects/<id>/project.md` + `Projects/<id>/Notes/<note_id>.md`, `Conversations/<id>.jsonl`, `Runs/<id>.md`.
The list of known vaults is `%APPDATA%\burrow\vaults.ini` (`VaultRegistry` copies it over from the old `%APPDATA%\otomation` on first start).
`vault.ini`: `Name=`, `Version=2`, `Language=tr|en` (missing = tr). `Vault::ReloadSettings()` re-reads it in `WebFrame::ReloadAll()`, so a change by an agent with the `kasa_ayarlari` skill (name, language) shows after its run.
- Agent: name, role, mascot (species/color/accessory), `reports_to` (optional boss), model, enabled, `skill:` ids, `wake:` lines (`09:00 | task_id` or `every 30 | task_id`), body = persona.
- Task: name, model, priority 0..2, `min_remaining_percent` (skipped if less Claude usage is left), body = instructions.
- Chrome: Claude in Chrome is not an MCP config, it needs `claude --chrome`. `RunManager::BuildCommand` adds that flag when a job's tools contain `mcp__claude-in-chrome`; the template `tarayici_chrome` (not installed by default) lists the core tools. `--allowedTools` only pre-approves: all ~22 Chrome tool definitions are still loaded (token cost); unlisted ones are denied by `--permission-prompts none`. Needs Chrome open with the extension.
- Project: folder with `project.md` (name + summary body) and `Notes/` (project notes = persistent memory, same file shape as a rule: name, enabled, body; type `ProjectNote` = `RuleDefinition`). A job gets a `project_id` (chat: from the conversation's `project` meta field; wake-up: third part `09:00 | task_id | project_id`; manual run: asked; agent messages/reports: inherited from the sender's job). `job_builder` then adds a `## Proje` section (summary + enabled notes) to the system prompt. `WebFrame::Team()` reloads projects from disk on every call, so note edits (by user or by an agent with the `proje_notlari` skill) apply to the next run at once. Unknown/deleted project id = ignored (scheduled wake-ups of a deleted project are skipped).
- Skill (UI word: "Yetenek"): tools list, optional `mcp_config` path, body = how to use it. Skills are the only way an agent gets tools.
- Skill templates are compiled into the program (`skill_templates.cpp`). `Vault::Create` copies the four setup skills (`kasa_ayarlari`, `ajan_yonetimi`, `kural_duzenleme`, `proje_notlari`) into `Skills/`; "Yetenek Ekle" (web: "Şablondan ekle") adds any template: `uygulama_bilgisi` (how the app works, no tools, ~300 tokens), `gorev_yonetimi`, `yetenek_yonetimi`, `kullanim_raporu`, `sohbet_arama`, `terminal_komutlari` (Bash, risky), `web_arastirma`, `tarayici_chrome`, `dosya_okuma`, `dosya_yazma` after letting the user edit it. Setup skills work by giving an agent `Read/Write/Edit/Glob` in the vault folder (the run's cwd) and describing the file formats; they cannot delete files (disable with `enabled: false`). After a run whose tools include Write/Edit, `WebFrame` reloads the vault. Change an on-disk format -> update the template bodies too.
- Web lists of projects, skills, tasks and rules are sorted alphabetically with Turkish rules in `Web/state.js` (`byName`, applied in `setSnapshot`); agents keep hierarchy order.
- Mascot (`Web/mascot.js`): species / color / accessory are indexes stored in the agent file, so the lists (`SPECIES`, `COLORS`, `ACCESSORIES`, `PALETTE`) only grow at the end. A `PALETTE` entry with 2-3 rgb stops is a gradient (SVG `linearGradient` with a unique id per drawing; swatches use CSS `linear-gradient`). Update the ranges in the `ajan_yonetimi` template and `i18n_en.js` when adding entries.
- Rule: name, enabled, body. Enabled rules are added to every agent's system prompt (token estimate shown in UI).

## How a run works (Core)
1. `job_builder` makes a `RunJob`: short system prompt (identity, a ~100 token "where you are" paragraph about the app and the vault layout, company rules, persona, skills, team list, `<send>` protocol), tools from skills.
2. `RunManager` queues by priority (max 1 concurrent), applies the usage gate, starts `claude -p` via `ClaudeProcess` (prompt on stdin, system prompt in a temp file), parses stream-json, writes `Runs/<id>.md`, reports `RunResult`.
Naming: the first message of a chat gets a short note appended to its prompt (`RunJob::name_chat`) asking the agent to start with `<title>2-4 words</title>`; the router renames the chat and the tag never reaches the transcript (`ParseReply` cuts it out). A conversation between agents is named by the sender through `<send to="x" title="Kısa ad">` (taught in the Ekip part of the system prompt). Both go through `CleanTitle` (one line, max 50 chars). No skill needed.
3. `ConversationRouter` handles each result: writes the answer to the conversation of the job's receiver, routes `<send to="agent_id">..</send>` blocks (`reply="no"` = information only) to other agents.
4. Replies use the job's `reply_stack`: user asks A, A sends to B -> B's stack is `[user, A]`. B's answer always goes back to A as a report job, A's answer goes to the user's chat (resuming the chat session). `kMaxAgentHops` = 8 is the safety net.
5. `Scheduler` (UI switch, default OFF) turns due wake-ups into jobs. Wake-ups missed while the app was closed are not replayed.

## Web bridge
Page -> program: `chrome.webview.postMessage({id, cmd, args})`, answered `{id, ok, data}` or `{id, ok:false, error}` (`api.js` `call()`). Program -> page: `{event:"changed", data:<snapshot>}` after every change (agents with state/usage, tasks, skills, rules, projects, usage gauges, queue); `{event:"vault"}` after a vault switch. Conversations, runs and templates are fetched with commands (`chats.list`, `chat.load`, `runs.list`, `run.get`, `templates.list`). All commands live in `Dispatch()` in `web_api.cpp`; user mistakes are thrown as `ApiError` and shown by the page. Vaults: `vault.list`, `vault.open`, `vault.create`, `vault.add`, `vault.remove`, `vault.browse` (native folder picker). `Web/vault_picker.js` is the dialog; without a vault (first start, or the last one is gone) the snapshot has `needs_vault: true`, only `vault.*` commands work, and the picker opens locked. A vault switch cancels running and queued jobs (the page asks first).

## Language and settings
- Web UI language is a vault setting (`Language=` in vault.ini, snapshot `vault.language`, command `settings.set`), so the `kasa_ayarlari` skill can change it. Settings page (`pages/settings.js`) has only the language so far; new settings become more cards there (and a line in vault.ini or a new file, plus the `kasa_ayarlari` template text).
- `tr('Türkçe metin', {x})` in Web code: Turkish text is the key, `i18n_en.js` has the English. `{x}` placeholders. New UI text = new line in `i18n_en.js` (a missing key just shows Turkish). Values stored in Turkish in the vault (run status `tamamlandı`, source `sohbet`) are compared as they are and shown through `tr(value)`. Error messages from `ApiError` are translated in `showError` / form dialogs the same way (add them to the dictionary).
- On a language change `app.js` reloads the page and returns to the same page (`sessionStorage burrow.page`). Not translated: program messages in C++ other than `ApiError`, agent prompts (they stay Turkish). Skill templates have English texts in `skill_templates_en.cpp`: `templates.list` returns them by the vault language (`LocalizedSkill`); a new template needs an entry there too (missing = stays Turkish). New vaults still copy the Turkish default skills.

## Overview, filters, budgets (all token-free, computed from files)
- Overview tab (`Web/pages/overview.js`): agent states, tokens today / last 7 days per agent, failures, latest runs. Numbers come from `Core/run_stats` over the cached `WebFrame::run_records_` (reloaded when a run ends).
- Per-agent `daily_tokens` (agent file, 0 = unlimited): scheduled wake-ups are skipped once today's tokens reach it. Chats and manual runs are never blocked.
- Typing indicator in the Chats tab: `RunManager::ConversationWriters(conv_id)` = agents whose queued/running job writes into that conversation (`job.ConversationId()` = `reply_stack.back()`). `chat.load` returns `writers` (`[{id,name}]`) and `busy`; the page shows the first writer's avatar and "{name} yazıyor". Agent-to-agent conversations are busy only when a writer targets that very conversation (not whenever a participant is busy elsewhere). A user chat stays busy (composer locked) while any frame of a job's reply stack is that chat; if no writer targets it (agent waits for a colleague) the page falls back to the chat's agent.
- Chats tab filters (kind: all / mine / agents only, agent, project, text search) live in `Web/pages/chats.js`, which receives all conversations and shows the ones that pass. Chats can be renamed (a `title` line is appended to the .jsonl, newest wins) and exported as Markdown.

## Token saving (the point of the project)
- Lean profile for every call: `--system-prompt-file`, `--tools <only skill tools>`, `--strict-mcp-config`, `--disable-slash-commands`, `--setting-sources project`, `--permission-prompts none`, `--max-turns`. Default call is ~25k tokens, lean ~3.3k, with no tools ~0.7k. Each built-in tool costs ~0.9k.
- Usage comes from the `rate_limit_event` line (5-hour and 7-day windows, `utilization` 0..1). A tiny Haiku probe measures it at startup and on "Yenile".
- Chats resume one Claude session; keep prompts, rules and persona short because they are paid on every run.

## Pitfalls (already hit once)
- WebView2 on MinGW: `__uuidof` does not work for the WebView2 interfaces (link error), so IIDs are copied by hand into `web_view_host.cpp`; callback objects answer only `IUnknown`. The static loader lib is MSVC-only, so `WebView2Loader.dll` is embedded and extracted to `%LOCALAPPDATA%\burrow\runtime`; browser data lives in `%LOCALAPPDATA%\burrow\webview_data` (the old `otomation` folder there is unused and can be deleted).
- Bash/Python heredocs run through the tool eat backslashes (`\n`, `\\`, `\]` in regexes, Windows paths in `sed`): for scripts with them use the Write tool and run the file.
- A lambda parameter named `t` would hide the translation function, so it is called `tr()` in Web code.
- RCDATA resource names must be plain identifiers (`WEB_PAGES_CHATS_JS`): quoted names let windres glue the next line's name onto the file path.
- `Node.append(array)` prints "[object ...]": in Web code spread/flatten arrays (`h()` does it for its children).
- A running `burrow.exe` locks the file: stop it before building.
- Strings are UTF-8 `std::string`; Windows APIs want UTF-16 (`winstr::Widen/Narrow`) and paths go through `fsx::` (never hand a UTF-8 path to `fopen`/`ifstream` directly).
- `windows.h` defines macros such as `CopyFile`, `DeleteFile`, `CreateDirectory`: do not give functions those names (it is `fsx::Copy`).
- A struct member called `text` hides a namespace of that name, so the string helper namespace is `str`.
- Compare run state with enums (`RunStatus`, `JobSource`), never with Turkish strings.
- `ClaudeProcess::Poll` calls `on_exit` last and the owner destroys the process inside it: nothing may touch the object after that call (`RunManager::Tick` re-checks every pointer before polling it). Stdin is written by a helper thread and closed afterwards; if it is never closed Claude Code never starts working.
- `claude` is started with `CreateProcessW` after `SearchPathW` found it (`claude.exe`); an npm `claude.cmd` shim goes through `cmd.exe /c`; anything not found fails at once.
- Page messages are handled from `Post()`, not inside the WebView2 callback, so a modal dialog (folder picker) is safe in a command.
- Use the Write tool for source files; bash heredocs with Turkish text broke.

## Not done yet
WhatsApp or other connectors (a skill needs a real MCP server), file/tool permission UI, more settings (concurrency, hop limit), installer, an app icon, light theme.
