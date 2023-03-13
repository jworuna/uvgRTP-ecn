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

## ECN Context configuration

`RCE_ECN_TRAFFIC` must be used in combination with `RCE_RTCP` to enable set/get ECN-Bits from the IP-Header.
By default, the receiver of the RTP traffic will generate every 10ms an ECN RTCP report.  

```session->create_stream(..., RCE_RTCP | RCE_ECN_TRAFFIC);```

### ECN RTP Context Configuration (RCC) flags

`RCC_ECN_AGGREGATION_TIME_WINDOW` is used to configure the ECN report interval time window in milliseconds. (default 10ms)

```stream->configure_ctx(RCC_ECN_AGGREGATION_TIME_WINDOW, 100);```

### ECN RTCP receiver hook

The following hook can be used to receive the ECN RTCP report on sender side.

```
void ecn_receiver_hook(void* arg, uvgrtp::frame::rtcp_ecn_report *frame);

//Register the hook
stream->get_rtcp()->install_ecn_hook(nullptr, ecn_receiver_hook)

void ecn_receiver_hook(void* arg, uvgrtp::frame::rtcp_ecn_report *frame)
{
    printf("ECN Report from: %u, packets: %i, ecn-ce: %i\n", 
            frame->ssrc, 
            frame->packet_count_tw, 
            frame->ect_ce_count_tw);

    delete frame;
}
```

### Usage of the ECN RTCP values

This report can be used to develop an ECN based bitrate adaption algorithm to optimize the encoder bitrate
and minimize the jitter. 

A sample will be provided soon...

## How to test ECN based functionality?

It's need a router that can mark the ECN enabled traffic "ECT(1)" with ECN-CE whenever 
congestion happens. The following tools and queues can be used:

- [AML JENS](https://github.com/telekom/aml-jens/tree/restructuring) 
  - Used, so called, datarate pattern to simulated specific network behaviours (bandwidth simulations)
  - A tool that works with a L4S enabled queue [sch_jenz](https://github.com/tarent/sch_jens)
  - The queue will begin mark packets with ECN-CE when a packet stays at least 4ms in the queue.
  - The probability that a packet gets marked rises linear within the 4-14ms.
  - Marking will be done between 4-14ms, packets that stayed longer then 14ms gets marked always with ECN-CE.