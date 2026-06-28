# Socket Protocol

## Transport

Unix domain socket at `/run/draind/draind.sock`, created by the daemon on startup.
Permissions: `0666` (any user can connect; identity is established by the first message).

All messages are newline-terminated JSON objects. Each line is one message.
There is no framing beyond the newline; messages must not contain embedded newlines.

## Connection types

The daemon distinguishes agents from ctl clients by the `type` field of the first message.

## Agent → Daemon messages

### HELLO
Sent once immediately after connecting.

```json
{"type":"hello","session_id":"3","uid":1000}
```

- `session_id`: logind session ID (from `$XDG_SESSION_ID` or `sd_pid_get_session`);
  may be empty if the agent process is not yet associated with a logind session
  (e.g. the service started before the compositor imported the session environment).
  The daemon routes lock/sleep signals to the sole connected agent when `session_id`
  is empty and only one agent is registered.
- `uid`: numeric UID of the connecting user

### IDLE_DIM
The session has been idle long enough to dim. Sent when the Wayland idle notification
for the dim timeout fires (or the fallback input timer expires).

```json
{"type":"idle_dim"}
```

### IDLE_SLEEP
The session has been idle long enough to sleep.

```json
{"type":"idle_sleep"}
```

### ACTIVE
User activity detected; session is no longer idle.

```json
{"type":"active"}
```

### INHIBIT
An application has requested that idle actions be suppressed (e.g. media is playing).

```json
{"type":"inhibit","reason":"VLC: Playing video"}
```

### UNINHIBIT
The inhibit reason is no longer active.

```json
{"type":"uninhibit","reason":"VLC: Playing video"}
```

## Daemon → Agent messages

### CONFIG
Sent to the agent immediately after a valid HELLO, and again after any config reload.
The agent uses these values to configure Wayland idle notification timeouts.

```json
{"type":"config","dim_timeout":180,"sleep_timeout":300}
```

### LOCK
Sent to the active session's agent when a lock is requested — either via
`draind-ctl lock` or automatically just before the daemon suspends. The agent runs
the user-configured `lock_cmd` as a background process (fire-and-forget).

```json
{"type":"lock"}
```

### PRE_SLEEP
Broadcast to **all** connected agents just before suspend. Each agent runs its
`before_sleep_cmd` synchronously, then replies with ACK. The daemon waits up to
5 seconds for all acks before proceeding to suspend.

```json
{"type":"pre_sleep"}
```

### ACK
Sent in response to INHIBIT / UNINHIBIT to confirm receipt.

```json
{"type":"ack"}
```

## Ctl → Daemon messages

### STATUS

```json
{"type":"ctl","cmd":"status"}
```

### SET_PROFILE

```json
{"type":"ctl","cmd":"set_profile","profile":"powersave"}
```

### LIST_PROFILES

```json
{"type":"ctl","cmd":"list_profiles"}
```

### RELOAD_CONFIG

```json
{"type":"ctl","cmd":"reload_config"}
```

### LIST_INHIBITORS

```json
{"type":"ctl","cmd":"list_inhibitors"}
```

### LOCK

```json
{"type":"ctl","cmd":"lock"}
```

Sends a LOCK message to the active session's agent. Returns `"ok": true` even if
no agent is currently connected (the lock is silently dropped).

## Daemon → Ctl messages

All ctl responses include `"ok": true|false`. On error, an `"error"` string is included.

```json
{"ok":true,"active_profile":"balanced","dimmed":false,"active_session":"3"}
{"ok":true,"profiles":["performance","balanced","powersave"]}
{"ok":true,"inhibitors":["3: VLC: Playing video"]}
{"ok":true}
{"ok":false,"error":"profile not found"}
```

## Error handling

- If the daemon receives a malformed message it closes the connection.
- If an agent disconnects unexpectedly the daemon drops all its inhibits and idle state.
- The daemon does not queue messages for reconnecting agents.
