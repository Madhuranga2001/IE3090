# Prompt log
- 5 Oct: Set up CentOS in VMware, and create the GitHub repo.
- 5 Oct: Created a minimal TCP listener (socket, bind, listen, accept, recv, send). Compiled it, changed PORT to 9410 and SID to 5962 (my values), and tested it with nc.
- 5 Oct: Created a threaded Agent with timestamped logging (log file remoteops_IT24102695.log and the agentfiles/IT24102695 folder). Compiled it and tested it with three nc clients, one of which disconnected. Checked the log with cat, ss -tlnp and ls -l, and took screenshots.
