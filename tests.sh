#!/bin/bash -e
#
# Some unit-tests
# (require 'nc' to be installed)

port=${1:-12345}
pidfile=/tmp/sockproc-test.pid

./sockproc $port $pidfile

echo "========================="

# simple commands
echo -e "uname -a\r\n0\r\n" | nc 127.0.0.1 $port
echo -e "id\r\n0\r\n" | nc 127.0.0.1 $port

# commands with some input
echo -e "wc -l\r\n12\r\nline1\r\nline2" | nc 127.0.0.1 $port
echo -e "grep line\r\n20\r\nline1\r\nline2\r\nfoobar" | nc 127.0.0.1 $port

# bad command.expecting non-empty error stream
echo -e "thisshouldfail\r\n0\r\n" | nc 127.0.0.1 $port

# this should have data in both output and error streams
echo -e "echo hello output && echo hello error >&2\r\n0\r\n" | nc 127.0.0.1 $port

echo "========================="

kill `cat $pidfile` && rm -f $pidfile
