## Introduction

Sockproc daemon is a simple server for executing shell commands or processes.
It can be useful in the situations, where a typical system call to launch 
a child process and wait for its completion is unacceptable, due to its 
blocking nature. Instead, a socket can be opened to sockproc, a command 
written to it, and then once child process completes, its exit code, 
output stream data and error stream data can be read back from the socket.


## Example

Launch sockproc on a UNIX domain socket:

    $ ./sockproc /tmp/shell.sock

Connect to socket and type in a command line to execute, followed
by a line that contains the number 0:

    $ telnet /tmp/shell.sock
    Trying /tmp/shell.sock...
    Connected to (null).
    Escape character is '^]'.
    find /usr/local/include | grep lua
    0
    status:0
    109
    /usr/local/include/lua.h
    /usr/local/include/lua.hpp
    /usr/local/include/luaconf.h
    /usr/local/include/lualib.h
    0
    Connection closed by foreign host.

Execute a bad command:

    $ telnet /tmp/shell.sock
    Trying /tmp/shell.sock...
    Connected to (null).
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

### Response format:

    status:<process-exit-code>\r\n
    <stdout-byte-count>\r\n
    <stdout-data>
    <stderr-byte-count>\r\n
    <stderr-data>


## License 
The MIT License (MIT)
