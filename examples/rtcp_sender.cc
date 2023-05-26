#include <uvgrtp/lib.hh>

#include <cstring>
#include <iostream>
#include <thread>
#include <mutex>

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

constexpr uint16_t PAYLOAD_LEN = 200000;
constexpr uint16_t MIN_PAYLOAD_LEN_BYTE = 1400 * 3;
constexpr uint16_t MIN_BITRATE_KBITS = 500;

constexpr uint16_t FRAME_RATE = 30;
constexpr uint32_t EXAMPLE_RUN_TIME_S = 30;
constexpr int SEND_TEST_PACKETS = FRAME_RATE * EXAMPLE_RUN_TIME_S;
constexpr int PACKET_INTERVAL_MS = 1000 / FRAME_RATE;
bool linkCongested = false;
int capacityKbits = MIN_BITRATE_KBITS * 4;
float linkUsageScale = 0.6;

void wait_until_next_frame(std::chrono::steady_clock::time_point &start, int frame_index);

void cleanup(uvgrtp::context &ctx, uvgrtp::session *local_session, uvgrtp::media_stream *send);

void ecn_receiver_hook(void *arg, uvgrtp::frame::rtcp_ecn_report *frame) {
    printf("ECN Report from: %u packets: %i ecn-ce: %i capacity: %i kbits early_feedback_mode: %i\n", frame->ssrc,
           frame->packet_count_tw,
           frame->ect_ce_count_tw, frame->capacity_kbits, frame->early_feedback_mode);

    if (frame->capacity_kbits > 0)
        capacityKbits = frame->capacity_kbits;
    if (!linkCongested && frame->early_feedback_mode) {
        linkCongested = true;
        capacityKbits = MIN_BITRATE_KBITS;
        std::cout << "congestion experienced, use min bitrate" << MIN_BITRATE_KBITS << " bkits" << std::endl;
    } else if (linkCongested && !frame->early_feedback_mode) {
        linkCongested = false;
        std::cout << "congestion over, bitrate " << capacityKbits << " kbits " << std::endl;
    }

    delete frame;
}

int main(int argc, char *argv[]) {
    std::cout << "Starting uvgRTP RTCP hook example" << std::endl;
    if (argc != 4) {
        std::cerr << "Usage: <receiverIp> <link usage [0..1]> <test duration s>" << std::endl;
        return EXIT_FAILURE;
    }
    std::string receiverIp = argv[1];
    linkUsageScale = atof(argv[2]);
    int testDurationS = strtol(argv[3], NULL, 10);

    std::cout << "Starting RTCP ECN sending example receiverIp " << receiverIp << " test duration s " << testDurationS
              << " link usage scale " << linkUsageScale
              << std::endl;

    // Creation of RTP stream. See sending example for more details
    uvgrtp::context ctx;
    uvgrtp::session *local_session = ctx.create_session(receiverIp);

    int flags = RCE_RTCP | RCE_RTCP | RCE_ECN_TRAFFIC | RCE_FRAGMENT_GENERIC | RCE_SYSTEM_CALL_CLUSTERING;
    uvgrtp::media_stream *sender_stream = local_session->create_stream(LOCAL_PORT, REMOTE_PORT,
                                                                       RTP_FORMAT_GENERIC, flags);

    if (!sender_stream || sender_stream->get_rtcp()->install_ecn_hook(nullptr, ecn_receiver_hook) != RTP_OK) {
        std::cerr << "Failed to install ECN report hook" << std::endl;
        cleanup(ctx, local_session, sender_stream);
        return EXIT_FAILURE;
    }

    long startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    long endMs = startMs + testDurationS * 1e3;
    long nowMs = 0;
    int i = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    while (nowMs < endMs) {
        int frameSizeByte = (capacityKbits * 1000 * linkUsageScale) / (FRAME_RATE * 8);
        std::cout << "Sending RTP frame size " << frameSizeByte << " byte" << std::endl;

        uint8_t buffer[frameSizeByte];
        memset(buffer, 'a', frameSizeByte);

        memset(buffer, 0, 3);
        memset(buffer + 3, 1, 1);
        memset(buffer + 4, 1, (19 << 1)); // Intra frame

        //std::cout << "frame_size byte " << frameSizeByte << std::endl;

        sender_stream->push_frame((uint8_t *) buffer, frameSizeByte, RTP_NO_FLAGS);

        // send frames at constant interval to mimic a real camera stream
        wait_until_next_frame(start, i);

        nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        i++;
    }

    std::cout << "Sending finished " << std::endl;

    cleanup(ctx, local_session, sender_stream);
    return EXIT_SUCCESS;
}

void wait_until_next_frame(std::chrono::steady_clock::time_point &start, int frame_index) {
    // wait until it is time to send the next frame. Simulates a steady sending pace
    // and included only for demostration purposes since you can use uvgRTP to send
    // packets as fast as desired
    auto time_since_start = std::chrono::steady_clock::now() - start;
    auto next_frame_time = (frame_index + 1) * std::chrono::milliseconds(PACKET_INTERVAL_MS);
    if (next_frame_time > time_since_start) {
        std::this_thread::sleep_for(next_frame_time - time_since_start);
    }
}

void cleanup(uvgrtp::context &ctx, uvgrtp::session *local_session, uvgrtp::media_stream *send) {
    if (send) {
        local_session->destroy_stream(send);
    }

    if (local_session) {
        // Session must be destroyed manually
        ctx.destroy_session(local_session);
    }
}

