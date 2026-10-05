
5 Oct:
-Set up CentOS in VMware, and create the GitHub repo.
-Added line framing: TCP is a byte stream, so one recv() can return half a command or two commands. read_line() keeps a per-client buffer and only returns a command once it sees \n; leftover bytes stay in the buffer.
-Decision: the SID tag is added in one function, reply(), so no response can miss it. 
-Decision: error codes: 001 AUTH_FAILED, 002 COMMAND_NOT_ALLOWED, 003 NOT_AUTHENTICATED, 004 FILE_TOO_LARGE, 005 FILE_NOT_FOUND, 006 UNKNOWN_COMMAND, 007 BAD_ARGUMENTS.
-Decision: the auth token is masked in the log file ("token hidden") so it is never written to disk.
-Tested all five commands and both framing cases.
