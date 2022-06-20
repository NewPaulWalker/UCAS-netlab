#!/usr/bin/python

import os
import sys
import glob

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.cli import CLI
from mininet.link import TCLink

script_deps = [ 'ethtool', 'arptables', 'iptables' ]

def check_scripts():
    dir = os.path.abspath(os.path.dirname(sys.argv[0]))

    script_dir = dir + '/scripts'
    if not os.path.exists(script_dir) or os.path.isfile(script_dir):
        print 'dir "%s" does not exist.' % (script_dir)
        sys.exit(1)
    
    for fname in glob.glob(script_dir + '/*.sh'):
        if not os.access(fname, os.X_OK):
            print '%s should be set executable by using `chmod +x $script_name`' % (fname)
            sys.exit(2)

    for program in script_deps:
        found = False
        for path in os.environ['PATH'].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
                found = True
                break
        if not found:
            print '`%s` is required but missing, which could be installed via `apt` or `aptitude`' % (program)
            sys.exit(3)
            
# Mininet will assign an IP address for each interface of a node 
# automatically, but hub or switch does not need IP address.
def clearIP(n):
    for iface in n.intfList():
        n.cmd('ifconfig %s 0.0.0.0' % (iface))

class MyTopo(Topo):
    def build(self):
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        s1 = self.addHost('s1')
        r1 = self.addHost('r1')
        
        self.addLink(h1, s1, loss=2)
        self.addLink(s1, r1)
        self.addLink(r1, h2)
        
if __name__ == '__main__':
    check_scripts()
    topo = MyTopo()
    net = Mininet(topo = topo, link = TCLink, controller = None)
    h1, h2, s1, r1 = net.get('h1', 'h2', 's1', 'r1')
    h1.cmd('ifconfig h1-eth0 10.0.0.1/24')
    h2.cmd('ifconfig h2-eth0 10.0.1.1/24')
    
    clearIP(s1)
    
    h1.cmd('route add default gw 10.0.0.2')
    h2.cmd('route add default gw 10.0.1.2')
    
    r1.cmd('ifconfig r1-eth0 10.0.0.2/24')
    r1.cmd('ifconfig r1-eth1 10.0.1.2/24')


    for h in (h1, h2, s1, r1):
        h.cmd('scripts/disable_ipv6.sh')
        h.cmd('scripts/disable_offloading.sh')
        h.cmd('scripts/disable_tcp_rst.sh')
        
    r1.cmd('./scripts/disable_arp.sh')
    r1.cmd('./scripts/disable_icmp.sh')
    r1.cmd('./scripts/disable_ip_forward.sh')

    net.start()
    CLI(net)
    net.stop()