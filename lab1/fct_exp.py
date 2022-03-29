from mininet.net import Mininet
from mininet.topo import Topo
from mininet.cli import CLI
from mininet.link import TCLink
from mininet.node import OVSBridge

class MyTopo(Topo):
    def build(self):
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        #bw 带宽 单位Mbps
        #delay 延时
        self.addLink(h1, h2, bw=10, delay='10ms')

topo = MyTopo()
net = Mininet(topo = topo, switch = OVSBridge, link = TCLink, controller=None)

net.start()
h2 = net.get('h2')
#此处最好用python2.7 而不是python
h2.cmd('python2.7 -m SimpleHTTPServer 80 &')
CLI(net)
h2.cmd('kill %python')
net.stop()
