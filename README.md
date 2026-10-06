# IE3090 Assignment: RemoteOps (Part 1)

Registration number: IT24102695

## Personalised values

| Item | Value | How it was calculated |
|---|---|---|
| Agent TCP port | 9410 | 7000 + 2410 (first four digits of 24102695) |
| SID tag | SID:5962 | last four digits 2695, reversed |
| Auth token | OPS-2695 | "OPS-" + last four digits |
| Source files | agent_695.c, controller_695.c, Makefile_695 | last three digits 695 |
| Log file | remoteops_IT24102695.log | remoteops_ + registration number |
| Storage path | ./agentfiles/IT24102695/<filename> | ./agentfiles/ + registration number |
| ZIP archive | IE3090_IT24102695.zip | |

## Build (Linux, gcc and make)

    make -f Makefile_695

## Run

    ./agent                      (listens on TCP port 9410)
    ./controller [ip] [port]     (defaults to 127.0.0.1 9410)

Controller commands (one per line): AUTH <token>, SYSINFO, LISTPROC, EXEC <name>,
PUT <local file>, GET <name>, MONITOR START <udp port>, MONITOR STOP, QUIT.
EXEC names: DATE, UPTIME, DISKFREE, HOSTNAME, WHOAMI (case-sensitive).
Downloaded files are saved in ./downloads/.

## Design summary

- Concurrency: one pthread per client connection, plus one monitor thread per session that sends UDP datagrams every 2 seconds.
- Framing: a per-connection buffer; a command is processed only when its newline has arrived. PUT/GET transfer exactly <filesize> bytes.
- Every OK and ERR line, and every UDP datagram, ends with SID:5962.
- Upload limit 10 MB (ERR 004). File names may only use letters, digits, . _ - and must not start with a dot.
- EXEC uses a fixed table of five commands. The user's text is never passed to a shell.
- Optional extension: throughput (bytes/s) is printed by the Controller after PUT and GET.

## Error codes

| Code | Reason |
|---|---|
| 001 | AUTH_FAILED |
| 002 | COMMAND_NOT_ALLOWED |
| 003 | NOT_AUTHENTICATED |
| 004 | FILE_TOO_LARGE |
| 005 | FILE_NOT_FOUND |
| 006 | UNKNOWN_COMMAND |
| 007 | BAD_ARGUMENTS |
| 008 | INTERNAL_ERROR |
| 009 | BAD_FILENAME |
| 010 | MONITOR_ALREADY_RUNNING |
| 011 | MONITOR_NOT_RUNNING |

## Repository contents

agent_695.c, controller_695.c, Makefile_695, README.md, design_diary.md, prompt_log.md
