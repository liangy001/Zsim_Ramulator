#!/usr/bin/python
# Produces a list of syscalls in the current system
import os
import re
syscallCmd = "gcc -E -dD /usr/include/asm-generic/unistd.h | grep __NR"
syscallDefs = os.popen(syscallCmd).read()
sysList = [(int(numStr), name) for (name, numStr)
           in re.findall("#define __NR_(.*?) (\d+)", syscallDefs)]
denseList = ["INVALID"]*(max([num for (num, name) in sysList]) + 1)
for (num, name) in sysList:
    denseList[num] = name
print '"' + '",\n"'.join(denseList) + '"'
