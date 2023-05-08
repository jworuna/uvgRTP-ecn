#include <uvgrtp/lib.hh>

#include <iostream>
#include <cstring>
#include <chrono>

/* RTP is a protocol for real-time streaming. The simplest usage
 * scenario is sending one RTP stream and receiving it. This example
 * Shows how to send one RTP stream. These examples perform a simple
 * test if they are run. You may run the receiving examples at the same
 * time to see the whole demo. */

/* parameters of this example. You may change these to reflect
 * you network environment. */
// the parameters of demostration
constexpr size_t PAYLOAD_LEN = 20000;
constexpr auto END_WAIT = std::chrono::seconds(5);

void ecn_receiver_hook(void *arg, uvgrtp::frame::rtcp_ecn_report *frame) {
    printf("ECN Report from: %u, packets: %i, ecn-ce: %i\n", frame->ssrc, frame->packet_count_tw,
           frame->ect_ce_count_tw);

    delete frame;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: <receiverIp> <receiverPort> <test duration s>" << std::endl;
        return EXIT_FAILURE;
    }
    std::string receiverIp = argv[1];
    int receiverPort = strtol(argv[2], NULL, 10);
    int senderPort = receiverPort - 1;
    int testDurationS = strtol(argv[3], NULL, 10);
    std::cout << "Starting uvgRTP RTP sending example receiverIp " << receiverIp << " receiverPort " << receiverPort
              << " test duration s> " << testDurationS
              << std::endl;

    /* To use the library, one must create a global RTP context object */
    uvgrtp::context ctx;

    // A session represents
    uvgrtp::session *senderSession = ctx.create_session(receiverIp);

    /* Each RTP session has one or more media streams. These media streams are bidirectional
     * and they require both source and destination ports for the connection. One must also
     * specify the media format for the stream and any configuration flags if needed.
     *
     * See Configuration example for more details about configuration.
     *
     * First port is source port aka the port that we listen to and second port is the port
     * that remote listens to
     *
     * This same object is used for both sending and receiving media
     *
     * In this example, we have one media stream with the remote participant: H265 */

    int senderFlags = RCE_RTCP | RCE_ECN_TRAFFIC;
    uvgrtp::media_stream *senderStream = senderSession->create_stream(senderPort, receiverPort, RTP_FORMAT_H265, senderFlags);
    if (!senderStream || senderStream->get_rtcp()->install_ecn_hook(nullptr, ecn_receiver_hook) != RTP_OK) {
        std::cerr << "Failed to install ECN report hook" << std::endl;
        return EXIT_FAILURE;
    }

    if (senderStream) {
        /* In this example we send packets as fast as possible. The source can be
         * a file or a real-time encoded stream */
        long startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        long endMs = startMs + testDurationS * 1e3;
        long nowMs = 0;
        int i = 0;
        while (nowMs < endMs) {
            std::unique_ptr<uint8_t[]> dummy_frame = std::unique_ptr<uint8_t[]>(new uint8_t[PAYLOAD_LEN]);
            memset(dummy_frame.get(), 'a', PAYLOAD_LEN); // NAL payload
            memset(dummy_frame.get(), 0, 3);
            memset(dummy_frame.get() + 3, 1, 1);
            memset(dummy_frame.get() + 4, 1, (19 << 1)); // Intra frame NAL type

            if ((i + 1) % 10 == 0 || i == 0) // print every 10 frames and first
            {
                std::cout << "Sending frame " << i + 1 << std::endl;
                nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
            }

            if (senderStream->push_frame(std::move(dummy_frame), PAYLOAD_LEN, RTP_NO_FLAGS) != RTP_OK) {
                std::cout << "Failed to send RTP frame!" << std::endl;
            }
            i++;
        }

        std::cout << "Sending finished. Waiting " << END_WAIT.count()
                  << " seconds before exiting." << std::endl;

        // wait a little bit so pop-up console users have time to see the results
        std::this_thread::sleep_for(END_WAIT);

        senderSession->destroy_stream(senderStream);
    }

    if (senderSession) {
        /* Session must be destroyed manually */
        ctx.destroy_session(senderSession);
    }

    return EXIT_SUCCESS;
}
