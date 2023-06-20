#include "frame_queue.hh"

#include "formats/h264.hh"
#include "formats/h265.hh"
#include "formats/h266.hh"

#include <condition_variable>

#include "rtp.hh"
#include "srtp/base.hh"
#include <netinet/in.h>
#include "global.hh"
#include "random.hh"
#include "debug.hh"
#include <thread>
#include <unistd.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <cstring>
#endif

extern int loadkbits;
extern int packetsInBlock;
extern long lastFeedbackReceivedUs;
extern bool linkCapacityLow;

extern std::condition_variable cv;
extern std::mutex cv_m;
long feedbackTimeoutUs;

uvgrtp::frame_queue::frame_queue(std::shared_ptr<uvgrtp::socket> socket, std::shared_ptr<uvgrtp::rtp> rtp, int rce_flags):
    active_(nullptr),
    dealloc_hook_(nullptr),
    max_mcount_(MAX_MSG_COUNT),
    max_ccount_(MAX_CHUNK_COUNT* max_mcount_),
    rtp_(rtp), 
    socket_(socket),
    rce_flags_(rce_flags),
    fps_(false),
    frame_interval_(),
    fps_sync_point_(),
    frames_since_sync_(0)
{}

uvgrtp::frame_queue::~frame_queue()
{
    if (active_)
    {
        (void)deinit_transaction();
    }
}

rtp_error_t uvgrtp::frame_queue::init_transaction()
{
    if (active_)
    {
        (void)deinit_transaction();
    }

    active_      = new transaction_t;

#ifndef _WIN32
    active_->headers     = new struct mmsghdr[max_mcount_];
    active_->chunks      = new struct iovec[max_ccount_];
#else
    active_->headers     = nullptr;
    active_->chunks      = nullptr;
#endif
    active_->rtp_headers = new uvgrtp::frame::rtp_ext_header[max_mcount_];

    switch (rtp_->get_payload()) {
        case RTP_FORMAT_H264:
            active_->media_headers = new uvgrtp::formats::h264_headers;
            break;

        case RTP_FORMAT_H265:
            active_->media_headers = new uvgrtp::formats::h265_headers;
            break;

        case RTP_FORMAT_H266:
            active_->media_headers = new uvgrtp::formats::h266_headers;
            break;


        default:
            break;
    }

    active_->hdr_ptr     = 0;
    active_->rtphdr_ptr  = 0;
    active_->rtpauth_ptr = 0;

    active_->data_raw     = nullptr;
    active_->data_smart   = nullptr;
    active_->dealloc_hook = dealloc_hook_;

    if (rce_flags_ & RCE_SRTP_AUTHENTICATE_RTP)
        active_->rtp_auth_tags = new uint8_t[10 * max_mcount_];
    else
        active_->rtp_auth_tags = nullptr;

    rtp_->fill_header((uint8_t *)&active_->rtp_common);
    active_->buffers.clear();

    return RTP_OK;
}

rtp_error_t uvgrtp::frame_queue::init_transaction(uint8_t *data)
{
    if (!data)
        return RTP_INVALID_VALUE;

    if (init_transaction() != RTP_OK) {
        UVG_LOG_ERROR("Failed to initialize transaction");
        return RTP_GENERIC_ERROR;
    }

    /* The transaction has been initialized to "active_" */
    active_->data_raw = data;

    return RTP_OK;
}

rtp_error_t uvgrtp::frame_queue::init_transaction(std::unique_ptr<uint8_t[]> data)
{
    if (!data)
        return RTP_INVALID_VALUE;

    if (init_transaction() != RTP_OK) {
        UVG_LOG_ERROR("Failed to initialize transaction");
        return RTP_GENERIC_ERROR;
    }

    /* The transaction has been initialized to "active_" */
    active_->data_smart = std::move(data);

    return RTP_OK;
}

rtp_error_t uvgrtp::frame_queue::deinit_transaction()
{
    if (active_ == nullptr) {
        UVG_LOG_WARN("Trying to deinit transaction, no active transaction!");
        return RTP_INVALID_VALUE;
    }

    if (active_->headers)
        delete[] active_->headers;

    if (active_->chunks)
        delete[] active_->chunks;

    if (active_->rtp_headers)
        delete[] active_->rtp_headers;

    if (active_->rtp_auth_tags)
        delete[] active_->rtp_auth_tags;

    active_->headers = nullptr;
    active_->chunks = nullptr;
    active_->rtp_headers = nullptr;
    active_->rtp_auth_tags = nullptr;

    if (active_->media_headers)
    {
        switch (rtp_->get_payload()) {
        case RTP_FORMAT_H264:
            delete (uvgrtp::formats::h264_headers*)active_->media_headers;
            active_->media_headers = nullptr;
            break;

        case RTP_FORMAT_H265:
            delete (uvgrtp::formats::h265_headers*)active_->media_headers;
            active_->media_headers = nullptr;
            break;

        case RTP_FORMAT_H266:
            delete (uvgrtp::formats::h266_headers*)active_->media_headers;
            active_->media_headers = nullptr;
            break;

        default:
            break;
        }
    }

    delete active_;
    active_ = nullptr;

    return RTP_OK;
}

rtp_error_t uvgrtp::frame_queue::enqueue_message(uint8_t *message, size_t message_len, bool set_m_bit)
{
    if (message == nullptr || message_len == 0)
    {
        UVG_LOG_ERROR("Tried to enqueue invalid message");
      return RTP_INVALID_VALUE;
    }

    /* Create buffer vector where the full packet is constructed
     * and which is then pushed to "active_"'s pkt_vec structure */
    uvgrtp::buf_vec tmp;

    /* update the RTP header at "rtpheaders_ptr_" */
    update_rtp_header();

    if (set_m_bit)
        ((uint8_t *)&active_->rtp_headers[active_->rtphdr_ptr])[1] |= (1 << 7);

    /* Push RTP header first and then push all payload buffers */
    tmp.push_back({
        sizeof(active_->rtp_headers[active_->rtphdr_ptr]),
        (uint8_t *)&active_->rtp_headers[active_->rtphdr_ptr++]
    });

    tmp.push_back({ message_len, message });

    enqueue_finalize(tmp);
    return RTP_OK;
}

rtp_error_t uvgrtp::frame_queue::enqueue_message(uint8_t *message, size_t message_len)
{
    return enqueue_message(message, message_len, false);
}

rtp_error_t uvgrtp::frame_queue::enqueue_message(buf_vec& buffers)
{
    if (!buffers.size())
    {
        UVG_LOG_ERROR("Tried to enqueue an empty buffer");
        return RTP_INVALID_VALUE;
    }

    /* update the RTP header at "rtpheaders_ptr_" */
    update_rtp_header();

    /* Create buffer vector where the full packet is constructed
     * and which is then pushed to "active_"'s pkt_vec structure */
    uvgrtp::buf_vec tmp;

    /* Push RTP header first and then push all payload buffers */
    tmp.push_back({     sizeof(active_->rtp_headers[active_->rtphdr_ptr]), 
                   (uint8_t *)&active_->rtp_headers[active_->rtphdr_ptr++]});

    /* If SRTP with proper encryption is used and there are more than one buffer,
     * frame queue must be a copy of the input and ... */
    if ((rce_flags_ & RCE_SRTP) && !(rce_flags_ & RCE_SRTP_NULL_CIPHER) && buffers.size() > 1) {
        
        size_t total = 0;

        for (auto& buffer : buffers) {
            total += buffer.first;
        }

        uint8_t* mem = new uint8_t[total];
        uint8_t* ptr = mem;

        // copy buffers to a single pointer
        for (auto& buffer : buffers) {
            memcpy(ptr, buffer.second, buffer.first);
            ptr += buffer.first;
        }

        tmp.push_back({ total, mem });

    } else {
        for (auto& buffer : buffers) {
            tmp.push_back({ buffer.first, buffer.second });
        }
    }

    enqueue_finalize(tmp);
    return RTP_OK;
}

rtp_error_t uvgrtp::frame_queue::flush_queue() {
    if (active_->packets.empty()) {
        UVG_LOG_ERROR("Cannot send an empty packet!");
        (void) deinit_transaction();
        return RTP_INVALID_VALUE;
    }

    int packetIndexStart = 0;
    int packetIndexEnd = 0;

    int packetsTotal = (int) active_->packets.size();
    int packetsLeft = packetsTotal;

    while (packetsLeft > 0) {
        long startUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

        // check for bad link
        feedbackTimeoutUs = startUs - FEEDBACK_TIMEOUT_US;
        if ((lastFeedbackReceivedUs > 0 and lastFeedbackReceivedUs <= feedbackTimeoutUs) or linkCapacityLow)
        {
            std::unique_lock<std::mutex> lk(cv_m);
            UVG_LOG_DEBUG("Waiting for ecn feedback after %li us or linkCapacityLow %i", lastFeedbackReceivedUs, linkCapacityLow);
            cv.wait_for(lk,std::chrono::milliseconds(200), []{return (lastFeedbackReceivedUs > feedbackTimeoutUs) and !linkCapacityLow;});
        }

        if (packetsLeft > packetsInBlock) {
            packetIndexEnd = packetIndexStart + packetsInBlock;
            packetsLeft -= packetsInBlock;
            if (packetsLeft > 0 and packetsLeft < (packetsInBlock / 2)) {
                packetIndexEnd += packetsLeft;
                packetsLeft = 0;
            }
        } else {
            packetIndexEnd = packetIndexStart + packetsLeft;
            packetsLeft = 0;
        }
        ((uint16_t *) active_->packets[packetIndexStart][0].second)[6] = htons(PROBING_BLOCK_START);
        ((uint16_t *) active_->packets[packetIndexEnd - 1][0].second)[6] = htons(PROBING_BLOCK_END);

        // send packet fragment of constant size and wait for load controll
        UVG_LOG_DEBUG("send packetIndexStart %i packetIndexEnd %i", packetIndexStart, packetIndexEnd);
        for (int i = packetIndexStart; i < packetIndexEnd; ++i) {
            rtp_error_t result = socket_->sendto(active_->packets[i], 0);
            if (result != RTP_OK) {
                UVG_LOG_ERROR("Failed to send packet: %li", errno);
                (void) deinit_transaction();
                return RTP_SEND_ERROR;
            }
        }
        // wait for this block according to capacity
        int bytesInBlock = (packetIndexEnd - packetIndexStart) * MAX_IPV4_PAYLOAD;
        long blockTimeUs = (1e6 * bytesInBlock) / (loadkbits * 1000 / 8);
        packetIndexStart = packetIndexEnd;
        packetsLeft = (packetsTotal - packetIndexStart);

        long nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        long diffUs = nowUs - startUs;
        long waitUs = blockTimeUs - diffUs;
        //UVG_LOG_DEBUG("blockTimeUs %li us waitUs %li us", blockTimeUs, waitUs);
        if (waitUs > 0) {
            //UVG_LOG_DEBUG("sleep_for %li us", waitUs);
            usleep(waitUs);
        }
        //UVG_LOG_DEBUG("full message took %zu chunks and %zu messages", active_->chunk_ptr, active_->hdr_ptr);
    }
    return deinit_transaction();
}

inline std::chrono::high_resolution_clock::time_point uvgrtp::frame_queue::this_frame_time()
{
    return fps_sync_point_ +
        std::chrono::nanoseconds((uint64_t)(frames_since_sync_ * frame_interval_.count()));
}

void uvgrtp::frame_queue::update_rtp_header()
{
    memcpy(&active_->rtp_headers[active_->rtphdr_ptr], &active_->rtp_common, sizeof(active_->rtp_common));
    rtp_->update_sequence((uint8_t *)(&active_->rtp_headers[active_->rtphdr_ptr]));
}

uvgrtp::buf_vec* uvgrtp::frame_queue::get_buffer_vector()
{
    if (!active_)
    {
        UVG_LOG_ERROR("No active transaction");
        return nullptr;
    }
  
    return &active_->buffers;
}

void *uvgrtp::frame_queue::get_media_headers()
{
    return active_->media_headers;
}

uint8_t *uvgrtp::frame_queue::get_active_dataptr()
{
    if (!active_)
        return nullptr;

    if (active_->data_smart)
        return active_->data_smart.get();
    return active_->data_raw;
}

void uvgrtp::frame_queue::install_dealloc_hook(void (*dealloc_hook)(void *))
{
    if (!dealloc_hook)
        return;

    dealloc_hook_ = dealloc_hook;
}

void uvgrtp::frame_queue::enqueue_finalize(uvgrtp::buf_vec& tmp)
{
    if (rce_flags_ & RCE_SRTP_AUTHENTICATE_RTP) {
        tmp.push_back({
            UVG_AUTH_TAG_LENGTH,
            (uint8_t*)&active_->rtp_auth_tags[10 * active_->rtpauth_ptr++]
            });
    }

    active_->packets.push_back(tmp);
    rtp_->inc_sequence();
    rtp_->inc_sent_pkts();
}

inline void uvgrtp::frame_queue::update_sync_point()
{
    //UVG_LOG_DEBUG("Updating framerate sync point");
    frames_since_sync_ = 0;
    fps_sync_point_ = std::chrono::high_resolution_clock::now();
}