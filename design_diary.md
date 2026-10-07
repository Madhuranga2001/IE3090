
5 Oct: Set up CentOS in VMware, and create the GitHub repo.

5 Oct: Added line framing: TCP is a byte stream, so one recv() can return half a command or two commands. read_line() keeps a per-client buffer and only returns a command once it sees \n; leftover bytes stay in the buffer.
- The SID tag is added in one function, reply(), so no response can miss it. 
- Error codes: 001 AUTH_FAILED, 002 COMMAND_NOT_ALLOWED, 003 NOT_AUTHENTICATED, 004 FILE_TOO_LARGE, 005 FILE_NOT_FOUND, 006 UNKNOWN_COMMAND, 007 BAD_ARGUMENTS.
- The auth token is masked in the log file ("token hidden") so it is never written to disk.
- Tested all five commands and both framing cases.

5 Oct:
- SYSINFO reads real values: CPU load from /proc/loadavg, memory used = MemTotal - MemAvailable from /proc/meminfo (converted to MB), uptime from /proc/uptime. 
- LISTPROC runs a fixed command, ps -eo pid,comm --no-headers, through popen and joins the results as pid:name,pid:name on one line. Spaces in process names are replaced with underscores so the list stays parseable.
- EXEC uses a hard-coded mapping (DATE -> date, UPTIME -> uptime, DISKFREE -> df -h /, HOSTNAME -> hostname, WHOAMI -> whoami). The user's text is never put into a shell command, so "DATE; ls" can't run anything. Names are case-sensitive, so "date" is rejected.
- Command output is squashed onto one line (newlines become spaces) because the protocol allows one line per response.
- Tested all five allowed commands and the rejected ones; results as expected.

5 Oct (evening):
Decisions:

- PUT/GET: after "PUT name size\n" the next <size> bytes are raw data. They can already be in the read buffer, so recv_file() uses those leftover bytes first, then reads the rest from the socket.
- 10 MB limit. A too-large upload is read and discarded (up to 100 MB) so the connection stays usable; beyond that the connection is closed.
- filenames may only use letters, digits, . _ - and cannot start with a dot, which blocks path traversal like ../evil.txt (ERR 009 BAD_FILENAME).
- Uploads go to a temporary file and are renamed on success, so a dropped connection never leaves a half-written file.
- PUT before AUTH gets ERR 003 and the connection is closed, because the raw bytes would otherwise be parsed as commands.

6 Oct:

- UDP monitoring: MONITOR START <port> starts one monitor thread per session. It sends "SYSINFO <cpu> <mem> <uptime> SID:5962" to the Controller's IP (taken from the TCP connection) on the port the Controller chose, every 2 seconds.
- The thread waits on a condition variable, so STOP wakes it immediately. The stream stops on STOP, QUIT and when the client disappears; all three go through mon_stop().
- UDP port must be 1024-65535 (ERR 007 otherwise). New error codes: 010 MONITOR_ALREADY_RUNNING, 011 MONITOR_NOT_RUNNING.
- Problem: the first datagrams were sometimes 1 second apart, because time(NULL) drops the fractional second. Switched to clock_gettime(CLOCK_REALTIME).

6 Oct :

- Controller: reads replies with the same buffered read_line() idea as the Agent, so partial lines and extra bytes are handled.
- MONITOR START opens the UDP socket and listener thread first, then sends the command, so no datagram is missed. The listener is closed if the Agent refuses, or after STOP or QUIT.
- Throughput (bytes/s) is measured on the Controller only, so the protocol stays exactly as specified. The loopback figures (113 and 133 MB/s) are not representative of a real network.
- Tested five Controllers at once; the Agent log shows five overlapping sessions and the Agent kept running.
- Final test pass on a clean clone of the repo (so no leftover files could hide a bug). All of T1-T18 passed. Two real problems found: unlimited AUTH attempts per connection (fixed with a 3-attempt lockout) and the Controller printing a speed after a rejected upload (fixed). Obstacle: terminal screenshots kept cutting off the commands; solved with a helper that prints the exact input sent.


7 Oct :

- Annotated code screenshots. Wrote the Implementation Report, reflection and these notes. Commit dates: the early commits show -0400 because the VM clock was on a US time zone until I changed it to Asia/Colombo; git stores the absolute time, so the history is correct.
