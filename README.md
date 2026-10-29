# Burrow

A small desktop hub for a team of Claude Code agents, built to spend **far fewer tokens** than a typical agent orchestrator.

Burrow only triggers and organises work. The work itself is done by your own, already installed [Claude Code](https://claude.com/claude-code) (`claude -p`), so it runs on your Claude subscription. No API key is needed or stored.

> Status: personal project, Windows only, early. Expect rough edges.

## What it does

- **Agents** with a role, a persona, a mascot, an optional boss, and wake-up schedules (`09:00` or every 30 minutes).
- **Tasks, skills, company rules and projects** kept as plain Markdown files in a folder you choose (a "vault", Obsidian style). You can edit them by hand or let an agent edit them.
- **Chats** with an agent, and **agent-to-agent messages** (an agent can hand work to another agent and get a report back).
- **Usage gate**: shows your 5-hour and 7-day Claude usage and skips background work when little is left.
- **Overview**: tokens per agent for today and the last 7 days, failures, latest runs. Computed from files, costs no tokens.
- **Per-agent daily token limit** for scheduled runs.
- Interface in **Turkish or English** (a vault setting).

## Why it is cheap

Every call uses a lean profile: its own short system prompt, only the tools the agent's skills list, no MCP servers or slash commands unless asked for. A default Claude Code call is around 25k tokens; a Burrow call is around 3.3k, and about 0.7k when the agent has no tools. Skills are the only way an agent gets tools, and each built-in tool costs roughly 0.9k tokens, so you only pay for what you grant.

## Requirements

- Windows 10/11 with the [WebView2 runtime](https://developer.microsoft.com/microsoft-edge/webview2/) (already present on current Windows).
- [Claude Code](https://claude.com/claude-code) installed and logged in (`claude` must work in a terminal).

## Build

Needs CMake 3.20+, Ninja and MinGW-w64 g++ (C++17). There are no other dependencies: the program uses only the standard library and the Win32 API, plus the WebView2 SDK files that are part of the repository.

```
cmake -S Project -B Project/Build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Project/Build
```

The result is `Project/Build/burrow.exe`, a static executable (the web interface and the WebView2 loader are embedded in it).

Self tests: add `-DBURROW_BUILD_TESTS=ON` and run `Project/Build/core_selftest.exe`. With `--live` it also sends a few tiny real requests to Claude Code (uses a little of your usage).

Start options: `--vault <folder>` (open that vault without touching the saved vault list) and `--page <name>` (start on a page: overview, agents, chats, projects, tasks, skills, rules, runs, settings).

## Notes and limits

- Windows only for now.
- The built-in agent prompts and skill templates are written in Turkish (they are text for the agent, not the interface). Translating them is a welcome contribution.
- Agents get file and tool access only through skills you give them. Skills such as `terminal_komutlari` (shell) are risky; read a skill before enabling it.
- Burrow runs your local `claude` program. Check Anthropic's current terms for how you may use your subscription with automation; that is your responsibility.
- Not affiliated with or endorsed by Anthropic. "Claude" and "Claude Code" belong to Anthropic.

Developer documentation is in [`Development/project_summary.md`](Development/project_summary.md).

## License

[MIT](LICENSE). Microsoft's WebView2 SDK files in `Project/Third_Party/Web_View2` keep their own license.
