#!/usr/bin/python

import argparse
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.node import Node
from mininet.cli import CLI
from mininet.link import TCLink
from mininet.log import setLogLevel

class LinuxRouter(Node):
    def config(self, **params):
        super(LinuxRouter, self).config(**params)
        self.cmd('sysctl -w net.ipv4.ip_forward=1')

    def terminate(self):
        self.cmd('sysctl -w net.ipv4.ip_forward=0')
        super(LinuxRouter, self).terminate()


class CustomTopo(Topo):
    def build(self, bw_h1, delay_h1, bw_h2, delay_h2):
        # add routers
        r1 = self.addNode('r1', cls=LinuxRouter)
        r2 = self.addNode('r2', cls=LinuxRouter)

        # add hosts
        h1 = self.addHost('h1', ip='10.0.1.101/24', defaultRoute='via 10.0.1.1')
        h2 = self.addHost('h2', ip='10.0.1.102/24', defaultRoute='via 10.0.1.2')
        h3 = self.addHost('h3', ip='10.0.2.101/24', defaultRoute='via 10.0.2.1')

        # calculate r1-r2 and r2-h3 bandwith
        bw_h3    = (bw_h1 + bw_h2) / 2
        delay_h3 = '1ms'

        # add links
        self.addLink(h1, r1, bw=bw_h1, delay=delay_h1, loss=0,
                     intfName1='h1-eth0', intfName2='r1-eth1')
        self.addLink(h2, r1, bw=bw_h2, delay=delay_h2, loss=0,
                     intfName1='h2-eth0', intfName2='r1-eth2')
        self.addLink(h3, r2, bw=bw_h3, delay=delay_h3, loss=0,
                     intfName1='h3-eth0', intfName2='r2-eth3')

        self.addLink(r1, r2, bw=bw_h3, delay=delay_h3, loss=0,
                     intfName1='r1-eth0', intfName2='r2-eth0')


def run(args):
    topo = CustomTopo(
        bw_h1=args.bw_h1, delay_h1=args.delay_h1,
        bw_h2=args.bw_h2, delay_h2=args.delay_h2
    )
    net = Mininet(topo=topo, link=TCLink)
    net.start()

    # get hosts and middleboxes
    h1 = net.get('h1')
    h2 = net.get('h2')
    h3 = net.get('h3')
    r1 = net.get('r1')
    r2 = net.get('r2')

    # set IPs on router interfaces
    r1.setIP('10.0.1.1/24', intf='r1-eth1')
    r1.setIP('10.0.1.2/24', intf='r1-eth2')
    r2.setIP('10.0.2.1/24', intf='r2-eth3')

    r1.setIP('10.0.3.1/24', intf='r1-eth0')
    r2.setIP('10.0.3.2/24', intf='r2-eth0')

    # set routes
    h1.cmd('ip route change default via 10.0.1.1 dev h1-eth0 initcwnd 10')
    h2.cmd('ip route change default via 10.0.1.2 dev h2-eth0 initcwnd 10')
    h3.cmd('ip route change default via 10.0.2.1 dev h3-eth0 initcwnd 10')

    r1.cmd('ip route add 10.0.2.0/24 via 10.0.3.2')
    r1.cmd('ip route add 10.0.1.101 dev r1-eth1')
    r1.cmd('ip route add 10.0.1.102 dev r1-eth2')
    r2.cmd('ip route add 10.0.1.0/24 via 10.0.3.1')

    # net.startTerms()  # Optional: can be commented out if you don't want GUI terms
    CLI(net)
    net.stop()


if __name__ == '__main__':
    setLogLevel('info')
    parser = argparse.ArgumentParser(description="Mininet topology with configurable h1 and h2 links")
    parser.add_argument('--bw-h1', type=int, default=10,
                help='Bandwidth for h1-r1 link (Mbps)')
    parser.add_argument('--delay-h1', type=str, default='100ms',
                help='Delay for h1-r1 link')
    parser.add_argument('--bw-h2', type=int, default=10,
                help='Bandwidth for h2-r1 link (Mbps)')
    parser.add_argument('--delay-h2', type=str, default='100ms',
                help='Delay for h2-r1 link')

    args = parser.parse_args()
    run(args)
