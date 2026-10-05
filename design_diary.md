
5 Oct: Set up CentOS in VMware, and create the GitHub repo.

5 Oct: Added line framing: TCP is a byte stream, so one recv() can return half a command or two commands. read_line() keeps a per-client buffer and only returns a command once it sees \n; leftover bytes stay in the buffer.
-Decision: the SID tag is added in one function, reply(), so no response can miss it. 
-Decision: error codes: 001 AUTH_FAILED, 002 COMMAND_NOT_ALLOWED, 003 NOT_AUTHENTICATED, 004 FILE_TOO_LARGE, 005 FILE_NOT_FOUND, 006 UNKNOWN_COMMAND, 007 BAD_ARGUMENTS.
-Decision: the auth token is masked in the log file ("token hidden") so it is never written to disk.
-Tested all five commands and both framing cases.

5 Oct:
- SYSINFO reads real values: CPU load from /proc/loadavg, memory used = MemTotal - MemAvailable from /proc/meminfo (converted to MB), uptime from /proc/uptime. 
- LISTPROC runs a fixed command, ps -eo pid,comm --no-headers, through popen and joins the results as pid:name,pid:name on one line. Spaces in process names are replaced with underscores so the list stays parseable.
- EXEC uses a hard-coded mapping (DATE -> date, UPTIME -> uptime, DISKFREE -> df -h /, HOSTNAME -> hostname, WHOAMI -> whoami). The user's text is never put into a shell command, so "DATE; ls" can't run anything. Names are case-sensitive, so "date" is rejected.
- Command output is squashed onto one line (newlines become spaces) because the protocol allows one line per response.
- Tested all five allowed commands and the rejected ones; results as expected.

