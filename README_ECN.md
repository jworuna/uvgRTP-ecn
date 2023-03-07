# uvgRTP ECN

_All changes are made in the branch "ecn"_

The purpose of this fork is to enable uvgRTP to send and receive ECN-Bits to/from the IP-Header.
Furthermore, the possibility to accumulate the ECN-Bits for each packet in a certain time window and
a RTCP report to send the accumulated information back to the sender. The sender can then, create an
ECN based bitrate adaption for the encoder.

## ECN-Bits

Both IP (IPv6) and Legacy IP (IPv4) have a Traffic Class header field, eight bits in size. (This spans the second
and third nybble (half-octet) in IPv6; in IPv4 the whole second octet is comprised of it.) The upper six bits are
used for DiffServ (DSCP) while the lower two are used by ECN as follows:

* 00: NonECT -- nōn-ECN-capable transport
* 10: ECT(0) -- ECN-capable transport; L4S: legacy transport
* 01: ECT(1) -- ECN-capable transport; L4S: L4S-aware transport
* 11: CE     -- congestion experienced

The Traffic Class header used to be known as IPToS.

## Features

- [ ] Receive ECN-Bits from IP-Header
  - [x] Windows
  - [ ] Linux
- [ ] Set ECN-Bits to IP-Header
  - [x] Windows
  - [ ] Linux
- [ ] Accumulation of received bits for a configurable time window
- [ ] RTCP Report
  - [ ] Receiver side
  - [ ] Sender side