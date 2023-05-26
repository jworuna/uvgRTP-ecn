#include <uvgrtp/lib.hh>

#include <cstring>
#include <iostream>

/* RTCP (RTP Control Protocol) is used to monitor the quality
 * of the RTP stream. This example demonstrates the usage of
 * sender and receiver reports. RTCP also includes SDES, APP and BYE
 * packets which are not demostrated in this example.
 *
 * This example shows the usage of rtcp while also transmitting RTP
 * stream. The rtcp reports are sent only every 10 seconds and the
 * sender/receiver reports are printed.
*/

constexpr uint16_t LOCAL_PORT = 8888;
constexpr uint16_t REMOTE_PORT = 8890;

constexpr uint16_t PAYLOAD_LEN = 256;
constexpr uint16_t FRAME_RATE = 30;
constexpr uint32_t EXAMPLE_RUN_TIME_S = 30;
constexpr int SEND_TEST_PACKETS = FRAME_RATE*EXAMPLE_RUN_TIME_S;
constexpr int PACKET_INTERVAL_MS = 1000/FRAME_RATE;

void wait_until_next_frame(std::chrono::steady_clock::time_point& start, int frame_index);
void cleanup(uvgrtp::context& ctx, uvgrtp::session *session, uvgrtp::media_stream *stream);

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: <sender ip>" << std::endl;
        return EXIT_FAILURE;
    }
    std::string senderIp = argv[1];
    std::cout << "Starting uvgRTP RTCP hook senderIp " << senderIp << std::endl;

    // Creation of RTP stream. See sending example for more details
    uvgrtp::context ctx;
    uvgrtp::session *remote_session = ctx.create_session(senderIp);

    int flags = RCE_RTCP | RCE_RTCP | RCE_ECN_TRAFFIC;;
    uvgrtp::media_stream *remote_stream = remote_session->create_stream(REMOTE_PORT, LOCAL_PORT,
                                                                        RTP_FORMAT_GENERIC, flags);

    remote_stream->configure_ctx(RCC_ECN_AGGREGATION_TIME_WINDOW, 100);

    // TODO: There is a bug in uvgRTP in how sender reports are implemented and this text reflects
    // that wrong thinking. Sender reports are sent by the sender

    std::this_thread::sleep_for(std::chrono::seconds (600));

    cleanup(ctx,  remote_session,  remote_stream);
    return EXIT_SUCCESS;
}

void wait_until_next_frame(std::chrono::steady_clock::time_point &start, int frame_index)
{
  // wait until it is time to send the next frame. Simulates a steady sending pace
  // and included only for demostration purposes since you can use uvgRTP to send
  // packets as fast as desired
  auto time_since_start = std::chrono::steady_clock::now() - start;
  auto next_frame_time = (frame_index + 1)*std::chrono::milliseconds(PACKET_INTERVAL_MS);
  if (next_frame_time > time_since_start)
  {
      std::this_thread::sleep_for(next_frame_time - time_since_start);
  }
}

void cleanup(uvgrtp::context &ctx, uvgrtp::session *session,
             uvgrtp::media_stream *stream)
{
  if (stream)
  {
      session->destroy_stream(stream);
  }

  if (session)
  {
      // Session must be destroyed manually
      ctx.destroy_session(session);
  }
}

