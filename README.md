Sometimes I just want to be able to count the number of packets that are
arriving at a host.  There might not be any instrumentation in the software
that is processing these packets, either.

One simple way to get some metrics is to add some counter rules to the
system firewall.

This repository provides a tool for exporting these counters.  There are also
some scripts for adding and removing these counter rules.

TODO:
- port this to nftables, currently it relies on iptables
