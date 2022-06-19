#!/usr/bin/python2

import os
import sys
import string
import socket
import struct
from time import sleep

def server(port):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    s.bind(('0.0.0.0', int(port)))
    s.listen(3)
    
    cs, addr = s.accept()
    print addr

    filename = 'server-output.dat'
    recv_size = 0

    with open(filename, 'wb') as f:
        while True:
            data = cs.recv(1000)
            if data:
                recv_size += sys.getsizeof(data)
                f.write(data)
                print 'recv %d Bytes' % (recv_size) 
            else:
                break    

    s.close()


def client(ip, port):
    s = socket.socket()
    s.connect((ip, int(port)))
    
    filename = 'client-input.dat'
    send_size = 0

    with open(filename, 'rb') as f:
        while True:
            data = f.read(1000)
            if data:
                send_size += s.send(data)
                print 'send %d Bytes' % (send_size)
            else:
                break
   
    s.close()

if __name__ == '__main__':
    if sys.argv[1] == 'server':
        server(sys.argv[2])
    elif sys.argv[1] == 'client':
        client(sys.argv[2], sys.argv[3])
