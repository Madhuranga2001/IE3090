# Reflection

In Part 1, I have used Claude, an AI chatbot of Anthropic.
For my first chat, Claude generated the Agent and Controller codes line by line, and I did compilation, testing, committing, and screenshotting of the code by myself. 
For my second chat, I asked Claude to become a strict mentor, who provided me with test commands one by one. 
Claude had a talent for breaking down a task into manageable parts, and it came up with edge cases that I would have overlooked, like receiving a request in two pieces, putting a wrong-sized PUT request, and using a file name like ../../etc/passwd. But it was wrong several times.
In my last test pass with a clean repository copy, there were two issues that needed fixing. The Agent permitted infinite numbers of invalid AUTH requests on one connection, so I put a lock after three failed attempts.
The Controller was printing a speed like 110 MB/s even if the Agent declined an 11 MB transfer, so I made it print the speed only after an OK response.
I also inserted logging of rejected GET filenames and cleaned up indentation and comments a bit.
I would go through every modification using git diff before building. If a single modification would be misplaced, git diff revealed it and I would fix it. 
I learned more from testing than reading.
The issues stated above came up when I performed test runs on a fresh copy of the code.
I also gained knowledge about the concept behind this task: TCP is just a stream of bytes and the program needs to distinguish between messages. That is the reason why my Agent uses a buffer and why the incorrect PUT should break the connection. 
