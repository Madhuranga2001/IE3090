# Prompt log
- 5 Oct: Set up CentOS in VMware, and create the GitHub repo.
- 5 Oct: Created a minimal TCP listener (socket, bind, listen, accept, recv, send). Compiled it, changed PORT to 9410 and SID to 5962 (my values), and tested it with nc.
- 5 Oct: Created a threaded Agent with timestamped logging (log file remoteops_IT24102695.log and the agentfiles/IT24102695 folder). Compiled it and tested it with three nc clients, one of which disconnected. Checked the log with cat, ss -tlnp and ls -l, and took screenshots.
- 5 Oct: Add an agent version with line framing (read_line), a reply() helper that adds the SID tag, AUTH and QUIT. Tested five commands with nc (wrong token, correct token, unknown command, QUIT) and two framing cases (a command split in two pieces, two commands in one write). Took screenshots.
- 5 Oct: SYSINFO, LISTPROC and a whitelisted EXEC.Tested every command with nc, including rejected EXEC cases (ls, lowercase date, "DATE; ls", no argument). Took screenshots.

-6 Oct : UDP monitoring (MONITOR START/STOP).Applied it as five edits to agent_695.c.The first test showed uneven datagram timing because time(NULL) drops the fractional second; I changed it to clock_gettime. Tested START, STOP, QUIT, Ctrl+C and bad arguments with nc, and checked the stream arrives every 2 seconds.
-6 oct : 
- Controller (controller_695.c): an interactive client with PUT/GET and throughput, and a UDP listener thread for MONITOR.
- Tested all commands, a 5 MB PUT/GET checked with cmp, and five Controllers at once.

-7 Oct : final testing and report
- Whitespace and comment fixes (commits 7d21c2e, a151d17, 5658059). My first edit of line 439 removed the wrong spaces; I fixed it with sed.

