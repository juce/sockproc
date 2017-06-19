## Introduction

Sockproc daemon is a simple server for executing shell commands or processes.
It can be useful in the situations, where a typical system call to launch 
a child process and wait for its completion is unacceptable, due to its 
blocking nature. Instead, a socket can be opened to sockproc, a command 
written to it, and then once child process completes, its exit code, 
output stream data and error stream data can be read back from the socket.


## Examples

### With UNIX domain socket

Launch sockproc on a UNIX domain socket:

    $ ./sockproc /tmp/shell.sock

On Mac telnet works with both tcp sockets and unix-domain sockets, but on
most Linux distributions, the **telnet** command is not as versatile. So we
can employ a **socat** utility instead, using "crlf" flag to enforce the
'\r\n' line-endings for standard input.

Connect to socket and type in a command line to execute, followed
by a line that contains the number 0:

    $ socat - /tmp/shell.sock,crlf
    uname -a
    0
    status:0
    98
    Linux a569cf4d3a74 4.9.27-moby #1 SMP Thu May 11 04:01:18 UTC 2017 x86_64 x86_64 x86_64 GNU/Linux
    0

### With TCP socket

Launch sockproc on a TCP socket:

    $ ./sockproc 13000

Connect to socket and type in a command line to execute, followed
by a line that contains the number 0:

    $ telnet 127.0.0.1 13000
    Trying 127.0.0.1...
    Connected to 127.0.0.1.
    Escape character is '^]'.
    uname -a
    0
    status:0
    98
    Linux a569cf4d3a74 4.9.27-moby #1 SMP Thu May 11 04:01:18 UTC 2017 x86_64 x86_64 x86_64 GNU/Linux
    0
    Connection closed by foreign host.

Execute a bad command:

    $ telnet 127.0.0.1 13000
    Trying 127.0.0.1...
    Connected to 127.0.0.1.
    Escape character is '^]'.
    foobar
    0
    status:32512
    0
    37
    /bin/bash: foobar: command not found
    Connection closed by foreign host.


## Wire protocol

The protocol is very simple, similar somewhat to HTTP:

### Request format:

    <command-line>\r\n
    <stdin-byte-count>\r\n
    <stdin-data>

The `<command-line>` length cannot exceed 2040 characters.

### Response format:

    status:<process-exit-code>\r\n
    <stdout-byte-count>\r\n
    <stdout-data>
    <stderr-byte-count>\r\n
    <stderr-data>


## License 
The MIT License (MIT)
