# uvgRTP

uvgRTP is an RTP library written in C++ with a focus on usability and efficiency. It features a very intuitive and easy-to-use API, built-in support for HEVC, AVC, Opus, SRTP and ZRTP. In ideal conditions it is able to receive a goodput of 600 MB/s for HEVC stream.

uvgRTP is licensed under the permissive BSD 2-Clause License

For SRTP/ZRTP support, uvgRTP uses [Crypto++](https://www.cryptopp.com/)

Supported specifications:
   * [RFC 3350: RTP: A Transport Protocol for Real-Time Applications](https://tools.ietf.org/html/rfc3550)
   * [RFC 7798: RTP Payload Format for High Efficiency Video Coding (HEVC)](https://tools.ietf.org/html/rfc7798)
   * [RFC 6184: RTP Payload Format for H.264 Video](https://tools.ietf.org/html/rfc6184)
   * [RFC 7587: RTP Payload Format for the Opus Speech and Audio Codec](https://tools.ietf.org/html/rfc7587)
   * [RFC 3711: The Secure Real-time Transport Protocol (SRTP)](https://tools.ietf.org/html/rfc3711)
   * [RFC 6189: ZRTP: Media Path Key Agreement for Unicast Secure RTP](https://tools.ietf.org/html/rfc6189)

Based on Marko Viitanen's [fRTPlib](https://github.com/fador/fRTPlib)

## Notable features

* Builtin support for:
    * AVC
    * HEVC
    * Opus
    * SRTP/ZRTP
* Generic interface for custom media types
* UDP hole punching
* Simple API
* Permissive license

## Building and linking

See [BUILDING.md](BUILDING.md) for instructions on how to build and use uvgRTP

## Examples

See [examples](examples/) directory for different uvgRTP examples

## Paper

Please cite the following paper for uvgRTP:

```A. Altonen, J. Räsänen, J. Laitinen, M. Viitanen, and J. Vanne, “Open-source RTP library for high-speed 4K HEVC video streaming,” in Proc. IEEE Int. Workshop on Multimedia Signal Processing, Tampere, Finland, Sept. 2020.```
