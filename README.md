# Codex Companion

Phase 1 implementation of a Codex hook companion service and ESP32-style web dashboard.

## What is included

- Node.js companion service on HTTP `:3120` and WebSocket `:3121`
- Codex hook scripts for `SessionStart`, `UserPromptSubmit`, `PostToolUse`, `PermissionRequest`, and `Stop`
- Permission approval flow with WebSocket dashboard actions
- Fail-open observational hooks and fail-closed permission hook
- JSONL event logs per session
- mDNS advertisement for `_codex-companion._tcp`
- 480x480 AMOLED-style simulator that also works as a mobile dashboard

## Run

```powershell
cd companion-service
npm install
npm start
```

Open `http://localhost:3120/dashboard` and paste the auth token printed by the service.

## Install hooks

Copy scripts to your Codex hooks directory:

```powershell
New-Item -ItemType Directory -Force -Path "$env:USERPROFILE\.codex\hooks"
Copy-Item -Force companion-service\hooks\*.js "$env:USERPROFILE\.codex\hooks\"
Copy-Item -Force codex-hooks-config\hooks.json "$env:USERPROFILE\.codex\hooks.json"
```

Enable hooks in `%USERPROFILE%\.codex\config.toml`:

```toml
[features]
hooks = true
```

## Test examples

```powershell
'{"session_id":"test-001","cwd":"C:/tmp/project","model":"o4-mini","permission_mode":"default"}' | node $env:USERPROFILE\.codex\hooks\on-session-start.js
```

For a permission request:

```powershell
'{"session_id":"test-001","cwd":"C:/tmp/project","model":"o4-mini","tool_name":"Bash","tool_use_id":"tu-001","tool_input":{"command":"npm install express"}}' | node $env:USERPROFILE\.codex\hooks\on-permission.js
```

The permission hook exits `0` for allow and `2` for deny or timeout.
