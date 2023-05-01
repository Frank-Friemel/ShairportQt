#include "HairTunes.h"
#include "Trim.h"
#include "definitions.h"
#include <spdlog/spdlog.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "audio/PlaySound.h"
#include "audio/WaveHeader.h"
#include "SuspendInhibitor.h"

using namespace std;
using namespace string_literals;

namespace alac
{
    typedef struct alac_file alac_file;

    alac_file *create_alac(int samplesize, int numchannels);
    void destroy_alac(alac_file* alac) noexcept;

    void decode_frame(alac_file *alac,
                    unsigned char *inbuffer,
                    void *outbuffer, int *outputsize);
    void alac_set_info(alac_file *alac, char *inputbuffer);
    void allocate_buffers(alac_file *alac);

    struct alac_file
    {
        unsigned char *input_buffer;
        int input_buffer_bitaccumulator; /* used so we can do arbitary
                                            bit reads */

        int samplesize;
        int numchannels;
        int bytespersample;

        /* buffers */
        int32_t *predicterror_buffer_a;
        int32_t *predicterror_buffer_b;

        int32_t *outputsamples_buffer_a;
        int32_t *outputsamples_buffer_b;

        int32_t *uncompressed_bytes_buffer_a;
        int32_t *uncompressed_bytes_buffer_b;

        /* stuff from setinfo */
        uint32_t setinfo_max_samples_per_frame; /* 0x1000 = 4096 */    /* max samples per frame? */
        uint8_t setinfo_7a; /* 0x00 */
        uint8_t setinfo_sample_size; /* 0x10 */
        uint8_t setinfo_rice_historymult; /* 0x28 */
        uint8_t setinfo_rice_initialhistory; /* 0x0a */
        uint8_t setinfo_rice_kmodifier; /* 0x0e */
        uint8_t setinfo_7f; /* 0x02 */
        uint16_t setinfo_80; /* 0x00ff */
        uint32_t setinfo_82; /* 0x000020e7 */ /* max sample size?? */
        uint32_t setinfo_86; /* 0x00069fe4 */ /* bit rate (avarge)?? */
        uint32_t setinfo_8a_rate; /* 0x0000ac44 */
        /* end setinfo stuff */
    };
} // namespace alac

static int16_t ApplyVolumeToChannel(const int16_t in, const double lfVolume, double& e)
{
    if (0 == in)
    {
        e = 0;
        return 0;
    }
    const double qOut = (static_cast<double>(in) * lfVolume) + e;
    const int16_t out = static_cast<int16_t>(floor(qOut+0.5));
    e = qOut - static_cast<double>(out);
    return out;
}

HairTunes::HairTunes(const SharedPtr<IValueCollection> config, const SharedPtr<IValueCollection>& client)
    : m_config{ move(config) }
    , m_client{ client }
    , m_lowLevelQueue{ VariantValue::Key("LowLevelRTP").Get<size_t>(config) } 
    , m_highLevelQueue{ VariantValue::Key("LowLevelRTP").Get<size_t>(config) +
                        VariantValue::Key("LevelOffsetRTP").Get<size_t>(config) } 
    , m_remoteControlPort{ VariantValue::Key("control_port").Get<int>(client) }
    , m_clientID{ VariantValue::Key("ID").Get<string>(client) }
    , m_decoder{ nullptr }
    , m_stopThread{ false }
    , m_flush{ 0 }
    , m_aes{ VariantValue::Key("rsaaeskey").Get<vector<uint8_t>>(client) }
    , m_iv{ VariantValue::Key("aesiv").Get<vector<uint8_t>>(client) }
    , m_progressData{ 0 }
    , m_pendingData{ 0 }
{
    if (m_iv.size() != 16)
    {
        throw runtime_error("intialization vector has wrong format");
    }
    assert(m_lowLevelQueue);
    assert(m_lowLevelQueue < m_highLevelQueue);

    assert(m_remoteControlPort);

    const auto fmtp = VariantValue::Key("fmtp").Get<string>(client);
    assert(!fmtp.empty());

    vector<int> fmtpList;
    fmtpList.reserve(12);

    ParseRegEx(fmtp, "[^[:space:]]+"s, [&fmtpList](string s)
    {
        fmtpList.push_back(stoi(s));
        return true;
    });

    if (fmtpList.size() != 12)
    {
        assert(false);
        fmtpList.resize(12);
    }

    if (fmtpList[3] != SAMPLE_SIZE)
	{
		throw runtime_error("only 16-bit samples supported");
	}
    assert(fmtpList[7] == NUM_CHANNELS);

    m_frameBytes	= fmtpList[1] << 2; 
    m_samplingRate  = fmtpList[11];
    m_mute          = false;

    m_decoder = alac::create_alac(SAMPLE_SIZE, NUM_CHANNELS);

    m_decoder->setinfo_max_samples_per_frame = fmtpList[1];
    m_decoder->setinfo_7a					= fmtpList[2];
    m_decoder->setinfo_sample_size			= SAMPLE_SIZE;
    m_decoder->setinfo_rice_historymult		= fmtpList[4];
    m_decoder->setinfo_rice_initialhistory	= fmtpList[5];
    m_decoder->setinfo_rice_kmodifier		= fmtpList[6];
    m_decoder->setinfo_7f					= fmtpList[7];
    m_decoder->setinfo_80					= fmtpList[8];
    m_decoder->setinfo_82					= fmtpList[9];
    m_decoder->setinfo_86					= fmtpList[10];
    m_decoder->setinfo_8a_rate				= m_samplingRate;

    alac::allocate_buffers(m_decoder);	

    m_queueThread = make_unique<thread>([this]()
    {
        const SuspendInhibitor suspendInhibitor{ "ShairportQt", "Playing" };
        RunQueue();
    });
    
    // the client ID equals the "peer" address
    assert(!m_clientID.empty());

    m_controlEndpoint   = make_unique<RtpEndpoint>(this, m_clientID);
    m_dataEndpoint      = make_unique<RtpEndpoint>(this, m_clientID);
    m_timingEndpoint    = make_unique<RtpEndpoint>(this, m_clientID);
}

HairTunes::~HairTunes()
{
    m_dataEndpoint.reset();
    m_controlEndpoint.reset();
    m_timingEndpoint.reset();

    ++m_flush;
    m_stopThread = true;

    m_condQueue.NotifyAll();

    if (m_queueThread)
    {
        if (m_queueThread->joinable())
        {
            try
            {
                m_queueThread->join();
            }
            catch (...)
            {
                assert(false);
            }
        }
        m_queueThread.reset();
    }
    if (m_decoder)
    {
        alac::destroy_alac(m_decoder);
        m_decoder = nullptr;
    }
}

void HairTunes::AlacDecode(unique_ptr<RtpPacket>& packet)
{
    const unsigned char* pBuf = packet->getData();
    const int len = packet->getDataLen();

	unsigned char dest[RAOP_PACKET_MAX_SIZE];

	assert (len <= RAOP_PACKET_MAX_SIZE);
    const int aeslen = len & ~0xf;

    uint8_t iv[16];

    memcpy(iv, m_iv.data(), 16);
    m_aes.Decrypt(pBuf, dest, aeslen, iv, 16);

    memcpy(dest+aeslen, pBuf+aeslen, len-aeslen);  

    packet->resize(m_frameBytes);

	int outsize = 0;
    alac::decode_frame(m_decoder, dest, packet->data(), &outsize);

    assert(outsize <= m_frameBytes);
    packet->resize(outsize);
}

void HairTunes::ResetProgess() noexcept
{
    m_progressData = 0;
}

bool HairTunes::IsPlaying() const noexcept
{
    return m_isPlaying;
}

int HairTunes::GetProgressTime() const noexcept
{
    const int64_t d = m_progressData.load() - m_pendingData.load();

    if (d <= 0)
    {
        return 0;
    }
    return (int)(d / (m_samplingRate * SAMPLE_FACTOR));
}

uint64_t HairTunes::GetSamplingFreq() const noexcept
{
    return m_samplingRate;
}

bool HairTunes::AsyncRequestResend(const unique_lock<mutex>& sync, const USHORT nSeq, const short nCount) noexcept
{
    if (sync.owns_lock())
    {
        try
        {
            for (const auto& item : m_asyncResend)
            {
                if (item->seq == nSeq && item->count == nCount)
                {
                    return false;
                }
            }

            m_asyncResend.emplace_back(make_unique<ResendRequest>(nSeq, nCount,
                async(launch::async, [this](const USHORT seq, const short n)
                    {
                        RequestResend(seq, n);
                    }, nSeq, nCount)));
            return true;
        }
        catch(...)
        {
        }
    }
    else
    {
        assert(false);
    }
    return false;
}

void HairTunes::RunQueue() noexcept
{
    // start fill in [ms]
    const size_t msStartFill = VariantValue::Key("StartFill").Get<size_t>(m_config);
    const auto audioDevice = VariantValue::Key("AudioDevice").TryGet<string>(m_config).value_or("default"s);

    spdlog::debug("starting Hairtunes with a buffer of {} ms and output to \"{}\"", msStartFill, audioDevice);

    const VariantValue::Key keyVolume("Volume");

    // volume db (factored 1000)
    int64_t volumeDb = keyVolume.Get<int64_t>(m_config);
   
    double lfVolume = pow(10.0, volumeDb * 0.00005);
    assert(lfVolume > 0. && lfVolume <= 1.);
    double eChannelOne = 0.;
    double eChannelTwo = 0.;

    future<int> playAudio;
    SharedPtr<BlobStream> streamPCM;

    try
    {
        streamPCM = MakeShared<BlobStream>();

        streamPCM->SetMode(BlobStream::Mode::pipeOpen);

        AlsaAudio::WaveHeader hdrWav;

        // prepare the audio paramaters
        hdrWav.init(m_samplingRate, SAMPLE_SIZE, NUM_CHANNELS);
        streamPCM->Write(&hdrWav, hdrWav.mySize(), nullptr);
    }
    catch(...)
    {
        spdlog::error("failed to set up Audio");
        return;
    }

    unique_lock<mutex> sync(m_mtxQueue);

    while (!m_stopThread)
    {
        m_condQueue.WaitAndLock(sync);

        if (m_packetQueue.empty())
        {
            continue;
        }
        
        do
        {
            if (!m_flush && m_packetQueue.size() > 1)
            {
                // navigate to 2nd item in queue
                auto secondItem = m_packetQueue.begin();
                ++secondItem;

                const short diffSeq = (secondItem->get()->getSeqNo() - m_packetQueue.front()->getSeqNo());

                // try to request packets again, if lost
                if (diffSeq > 1)
                {
                    if (AsyncRequestResend(sync, m_packetQueue.front()->getSeqNo() + 1, diffSeq - 1))
                    {
                        spdlog::debug("wait until packet {} will arrive", m_packetQueue.front()->getSeqNo() + 1);
                        break;
                    }
                }
            }
            // dequeue packet
            auto packet = move(m_packetQueue.front());
            m_packetQueue.pop_front();
    	    
            // unlock the queue
            sync.unlock();

            try
            {
                AlacDecode(packet);

                if (packet->size() >= 4 && packet->size() <= m_frameBytes)
                {
                    bool mute = m_mute;

                    if (!playAudio.valid() && !mute)
                    {
                        // optimistic mute as long as we're not playing
                        mute = true;

                        // empty packets will be muted in this case
                        static_assert(4 / sizeof(uint32_t) == 4 >> NUM_CHANNELS);
                        const size_t sampleCount = packet->size() >> NUM_CHANNELS;

                        const uint32_t* inptr = (const uint32_t*) packet->data();

                        for (size_t i = 0; i < sampleCount; i++) 
                        {
                            if (*inptr++)
                            {
                                // we got some sound -> unmute
                                mute = false;
                                break;
                            }
                        }
                    }
                    bool hasSoundData = false;

                    const size_t sampleCount = packet->size() >> NUM_CHANNELS;
                    const int16_t* inptr = (const int16_t*) packet->data();

                    // 0 db doesn't need to be applied
                    if (volumeDb != 0)
                    {
                        int16_t* outptr = (int16_t*)packet->data();

                        for (size_t i = 0; i < sampleCount; i++)
                        {
                            if (!hasSoundData)
                            {
                                if (*inptr || *(inptr+1))
                                {
                                    hasSoundData = true;
                                }
                            }
                            *outptr++ = ApplyVolumeToChannel(*inptr++, lfVolume, eChannelOne);
                            *outptr++ = ApplyVolumeToChannel(*inptr++, lfVolume, eChannelTwo);
                        }
                    }
                    else
                    {
                        for (size_t i = 0; i < sampleCount; i++)
                        {
                            if (*inptr++ || *inptr++)
                            {
                                hasSoundData = true;
                                break;
                            }
                        }
                    }

                    ULONG written = static_cast<ULONG>(packet->size());

                    if (!mute)
                    {
                        // write PCM data to sound-buffer, finally
                        streamPCM->Write(packet->data(), written, &written);
                    }
                    if (hasSoundData)
                    {
                        // update the progress information
                        // (we need to do this even during mute)
                        m_progressData += written;

                        if (!m_isPlaying)
                        {
                            m_isPlaying = true;
                        }
                    }
                    else if (m_isPlaying)
                    {
                        m_isPlaying = false;
                    }
                }
                else
                {
                    // unexpected
                    assert(false);
                }
                PutPacketToPool(move(packet));

                const size_t sizeStreamPCM = streamPCM->GetSize();
                m_pendingData = sizeStreamPCM;

                // wait for PCM buffer to fill [ms] before we start playing
                if (!playAudio.valid() && 
                    ((sizeStreamPCM > ((msStartFill * m_samplingRate * SAMPLE_FACTOR) / 1000)) || m_stopThread))
                {
                    // start playing after the sound buffer had been filled
                    playAudio = AlsaAudio::Play(streamPCM, audioDevice);
                }
            }
            catch(...)
            {
            }

            // calculate volume as long as we're unlocked
            const int64_t volumeDbNew = keyVolume.Get<int64_t>(m_config);

            if (volumeDb != volumeDbNew)
            {
                volumeDb = volumeDbNew;
                lfVolume = pow(10.0, volumeDb * 0.00005);
                assert(lfVolume > 0. && lfVolume <= 1.);
            }

            // lock before we loop
            sync.lock();
        } while (m_flush ? !m_packetQueue.empty() : m_packetQueue.size() > m_lowLevelQueue);

        // garbage out the asynchronous resend requests
        for (auto asyncResend = m_asyncResend.begin(); asyncResend != m_asyncResend.end(); )
        {
            if ((*asyncResend)->async.wait_for(0ms) == future_status::ready)
            {
                asyncResend = m_asyncResend.erase(asyncResend);
            }
            else
            {
                ++asyncResend;
            }
        }
    }
    assert(m_packetQueue.empty());

    streamPCM->SetMode(BlobStream::Mode::pipeClosed);
    m_asyncResend.clear();

    if (playAudio.valid())
    {
        while (streamPCM->GetSize())
        {
            this_thread::sleep_for(5ms);
        }
    }
}

void HairTunes::RequestResend(const USHORT nSeq, const short nCount) noexcept
{
	// *not* a standard RTCP NACK
	unsigned char req[8] = { 0 };						

	// Apple 'resend'
	req[0]				= 0x80;
	req[1]				= PAYLOAD_TYPE_RESEND_REQUEST | 0x80;	

	// our seqnum
	*(uint16_t*)(req+2)	= SWAP16(1);		
	// missed seqnum
	*(uint16_t*)(req+4)	= SWAP16(nSeq); 
	// count
	*(uint16_t*)(req+6)	= SWAP16(nCount);  

    spdlog::debug("try to send resend request for seq: {} -> {}", nSeq, nSeq+nCount-1);
	
    if (!m_controlEndpoint->SendTo(req, sizeof(req), m_remoteControlPort))
    {
        spdlog::debug("failed to send resend request for seq: {} -> {}", nSeq, nSeq+nCount-1);
    }
}

void HairTunes::Flush()
{
    unique_lock<mutex> sync(m_mtxQueue);
    ++m_flush;
    spdlog::info("flushing while {} packets are queued", m_packetQueue.size());

    while(!m_packetQueue.empty())
    {
        m_condQueue.NotifyAndUnlock(sync);
        this_thread::sleep_for(5ms);
        sync.lock();
    }
    --m_flush;
    spdlog::info("flushing done: {}", m_flush.load());
}

const std::string& HairTunes::GetClientID() const noexcept
{
    return m_clientID;
}

void HairTunes::QueuePacket(unique_ptr<RtpPacket>&& p, bool isResendPacket)
{
    // the endpoints are being destroyed already
    // so there must no new packets arrive
    // after stop-request
    assert(!m_stopThread);

	if (p->getDataLen() >= 16)
	{
		const USHORT nCurSeq = p->getSeqNo();

		unique_lock<mutex> sync(m_mtxQueue);

		if (!m_packetQueue.empty())
		{
			// check for lacking packets
            const USHORT backSeqNo = m_packetQueue.back()->getSeqNo();
			short nSeqDiff = nCurSeq - backSeqNo;

			switch(nSeqDiff)
			{
				case 0:
				{
					spdlog::debug("packet {} already queued\n", (long)nCurSeq);
				}
				break;

				case 1:
				{
                    if (!isResendPacket)
                    {
                        // expected sequence
                        m_packetQueue.emplace_back(move(p));

                        if (m_packetQueue.size() > m_highLevelQueue)
                        {
                            m_condQueue.NotifyAndUnlock(sync);
                        }
                    }
                    else
                    {
                        spdlog::debug("resend packet {} arrived too late and is being discarded\n", (long)nCurSeq);
                    }
                    return;
				}
				break;

				default:
				{
					if (nSeqDiff > 1)
					{
                        if (!isResendPacket)
                        {
                            AsyncRequestResend(sync, backSeqNo+1, nSeqDiff-1);

                            m_packetQueue.emplace_back(move(p));
                            spdlog::debug("requested resend {} -> {}", backSeqNo+1, nCurSeq-1);
                            
                            if (m_packetQueue.size() > m_highLevelQueue)
                            {
                                m_condQueue.NotifyAndUnlock(sync);
                            }                        
                        }
                        else
                        {
                            spdlog::debug("resend packet {} arrived too late and is being discarded\n", (long)nCurSeq);
                        }
                        return;
					}
					else
					{
						assert(nSeqDiff < 0);

						for (auto i = m_packetQueue.begin(); i != m_packetQueue.end(); ++i)
						{
                            nSeqDiff = nCurSeq - i->get()->getSeqNo();

							if (nSeqDiff > 0)
                            {
                                continue;
                            }
							if (nSeqDiff == 0)
							{
								return;
							}
                            spdlog::debug("insert seq {} before seq {}", nCurSeq, i->get()->getSeqNo());
                            m_packetQueue.emplace(i, move(p));
                            
                            if (m_packetQueue.size() > m_highLevelQueue)
                            {
                                m_condQueue.NotifyAndUnlock(sync);
                            }
                            return;
						}

                        // we must not get here because
                        // the initial nSeqDiff proved
                        // that there has to be at least
                        // the back item which has a later
                        // sequence
                        assert(false);
					}
				}
				break;
			}
		}
		else
		{
			// expected sequence (initial packet)
            // we don't notify the worker thread yet
            // because the queue has to be filled 
            // with more than one packet
			m_packetQueue.emplace_back(move(p));
		}
	}
    else
    {
        // unexpected data size
        assert(false);
    }
}

void HairTunes::OnRequest(RtpEndpoint*, unique_ptr<RtpPacket>&& packet)
{
	const uint8_t type = packet->getPayloadType();

	switch (type)
	{
		case PAYLOAD_TYPE_RESEND_RESPONSE:
		{
            if (packet->size() > RTP_BASE_HEADER_SIZE)
            {
                const size_t szNew = packet->size() - RTP_BASE_HEADER_SIZE;

                // shift data 4 bytes left
                memmove(packet->data(), packet->data() + RTP_BASE_HEADER_SIZE, szNew);
                
                // resize to new size
                packet->resize(szNew);

                spdlog::debug("got resend response {}", packet->getSeqNo());
                QueuePacket(move(packet), true);
            }
            else
            {
                assert(false);
            }
		}
		break;

		case PAYLOAD_TYPE_STREAM_DATA:
		{
			QueuePacket(move(packet), false);
		}
		break;

		case PAYLOAD_TYPE_TIMING_RESPONSE:
		case PAYLOAD_TYPE_TIMING_REQUEST:
		{
			assert(packet->size() == 32);
		}
		break;

		case PAYLOAD_TYPE_STREAM_SYNC:
		{
		}
		break;

		default:
		{
		}
		break;
	}
    if (packet)
    {
	    PutPacketToPool(move(packet));
    }
}

unsigned int HairTunes::GetServerPort() const noexcept
{
    return m_dataEndpoint->GetPort();
}
    
unsigned int HairTunes::GetControlPort() const noexcept
{
    return m_controlEndpoint->GetPort();
}

unsigned int HairTunes::GetTimingPort() const noexcept
{
     return m_timingEndpoint->GetPort();
}

namespace alac
{
/*
 * ALAC (Apple Lossless Audio Codec) decoder
 * Copyright (c) 2005 David Hammerton
 * All rights reserved.
 *
 * This is the actual decoder.
 *
 * http://crazney.net/programs/itunes/alac.html
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

static const int host_bigendian = 0;

#define _Swap32(v) do { \
                   v = (((v) & 0x000000FF) << 0x18) | \
                       (((v) & 0x0000FF00) << 0x08) | \
                       (((v) & 0x00FF0000) >> 0x08) | \
                       (((v) & 0xFF000000) >> 0x18); } while(0)

#define _Swap16(v) do { \
                   v = (((v) & 0x00FF) << 0x08) | \
                       (((v) & 0xFF00) >> 0x08); } while (0)

struct {signed int x:24;} se_struct_24;
#define SignExtend24(val) (se_struct_24.x = val)

void allocate_buffers(alac_file *alac)
{
    alac->predicterror_buffer_a = (int32_t *)malloc(alac->setinfo_max_samples_per_frame * 4);
    alac->predicterror_buffer_b = (int32_t *)malloc(alac->setinfo_max_samples_per_frame * 4);

    alac->outputsamples_buffer_a = (int32_t *)malloc(alac->setinfo_max_samples_per_frame * 4);
    alac->outputsamples_buffer_b = (int32_t *)malloc(alac->setinfo_max_samples_per_frame * 4);

    alac->uncompressed_bytes_buffer_a = (int32_t *)malloc(alac->setinfo_max_samples_per_frame * 4);
    alac->uncompressed_bytes_buffer_b = (int32_t *)malloc(alac->setinfo_max_samples_per_frame * 4);
}

static void deallocate_buffers(alac_file *alac) noexcept
{
    free(alac->predicterror_buffer_a);
    free(alac->predicterror_buffer_b);

    free(alac->outputsamples_buffer_a);
    free(alac->outputsamples_buffer_b);

    free(alac->uncompressed_bytes_buffer_a);
    free(alac->uncompressed_bytes_buffer_b);
}

void alac_set_info(alac_file *alac, char *inputbuffer)
{
  char *ptr = inputbuffer;
  ptr += 4; /* size */
  ptr += 4; /* frma */
  ptr += 4; /* alac */
  ptr += 4; /* size */
  ptr += 4; /* alac */

  ptr += 4; /* 0 ? */

  alac->setinfo_max_samples_per_frame = *(uint32_t*)ptr; /* buffer size / 2 ? */
  if (!host_bigendian)
      _Swap32(alac->setinfo_max_samples_per_frame);
  ptr += 4;
  alac->setinfo_7a = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_sample_size = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_rice_historymult = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_rice_initialhistory = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_rice_kmodifier = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_7f = *(uint8_t*)ptr;
  ptr += 1;
  alac->setinfo_80 = *(uint16_t*)ptr;
  if (!host_bigendian)
      _Swap16(alac->setinfo_80);
  ptr += 2;
  alac->setinfo_82 = *(uint32_t*)ptr;
  if (!host_bigendian)
      _Swap32(alac->setinfo_82);
  ptr += 4;
  alac->setinfo_86 = *(uint32_t*)ptr;
  if (!host_bigendian)
      _Swap32(alac->setinfo_86);
  ptr += 4;
  alac->setinfo_8a_rate = *(uint32_t*)ptr;
  if (!host_bigendian)
      _Swap32(alac->setinfo_8a_rate);

  allocate_buffers(alac);

}

/* stream reading */

/* supports reading 1 to 16 bits, in big endian format */
static uint32_t readbits_16(alac_file *alac, int bits)
{
    uint32_t result;
    int new_accumulator;

    result = (alac->input_buffer[0] << 16) |
             (alac->input_buffer[1] << 8) |
             (alac->input_buffer[2]);

    /* shift left by the number of bits we've already read,
     * so that the top 'n' bits of the 24 bits we read will
     * be the return bits */
    result = result << alac->input_buffer_bitaccumulator;

    result = result & 0x00ffffff;

    /* and then only want the top 'n' bits from that, where
     * n is 'bits' */
    result = result >> (24 - bits);

    new_accumulator = (alac->input_buffer_bitaccumulator + bits);

    /* increase the buffer pointer if we've read over n bytes. */
    alac->input_buffer += (new_accumulator >> 3);

    /* and the remainder goes back into the bit accumulator */
    alac->input_buffer_bitaccumulator = (new_accumulator & 7);

    return result;
}

/* supports reading 1 to 32 bits, in big endian format */
static uint32_t readbits(alac_file *alac, int bits)
{
    int32_t result = 0;

    if (bits > 16)
    {
        bits -= 16;
        result = readbits_16(alac, 16) << bits;
    }

    result |= readbits_16(alac, bits);

    return result;
}

/* reads a single bit */
static int readbit(alac_file *alac)
{
    int result;
    int new_accumulator;

    result = alac->input_buffer[0];

    result = result << alac->input_buffer_bitaccumulator;

    result = result >> 7 & 1;

    new_accumulator = (alac->input_buffer_bitaccumulator + 1);

    alac->input_buffer += (new_accumulator / 8);

    alac->input_buffer_bitaccumulator = (new_accumulator % 8);

    return result;
}

static void unreadbits(alac_file *alac, int bits)
{
    int new_accumulator = (alac->input_buffer_bitaccumulator - bits);

    alac->input_buffer += (new_accumulator >> 3);

    alac->input_buffer_bitaccumulator = (new_accumulator & 7);
    if (alac->input_buffer_bitaccumulator < 0)
        alac->input_buffer_bitaccumulator *= -1;
}

/* various implementations of count_leading_zero:
 * the first one is the original one, the simplest and most
 * obvious for what it's doing. never use this.
 * then there are the asm ones. fill in as necessary
 * and finally an unrolled and optimised c version
 * to fall back to
 */
#if 0
/* hideously inefficient. could use a bitmask search,
 * alternatively bsr on x86,
 */
static int count_leading_zeros(int32_t input)
{
    int i = 0;
    while (!(0x80000000 & input) && i < 32)
    {
        i++;
        input = input << 1;
    }
    return i;
}
#elif defined(__GNUC__)
/* for some reason the unrolled version (below) is
 * actually faster than this. yay intel!
 */
static int count_leading_zeros(int input)
{
    return __builtin_clz(input);
}
#elif defined(_MSC_VER) && defined(_M_IX86)
static int count_leading_zeros(int input)
{
    int output = 0;
    if (!input) return 32;
    __asm
    {
        mov eax, input;
        mov edx, 0x1f;
        bsr ecx, eax;
        sub edx, ecx;
        mov output, edx;
    }
    return output;
}
#else

static int count_leading_zeros(int input)
{
    int output = 0;
    int curbyte = 0;

    curbyte = input >> 24;
    if (curbyte) goto found;
    output += 8;

    curbyte = input >> 16;
    if (curbyte & 0xff) goto found;
    output += 8;

    curbyte = input >> 8;
    if (curbyte & 0xff) goto found;
    output += 8;

    curbyte = input;
    if (curbyte & 0xff) goto found;
    output += 8;

    return output;

found:
    if (!(curbyte & 0xf0))
    {
        output += 4;
    }
    else
        curbyte >>= 4;

    if (curbyte & 0x8)
        return output;
    if (curbyte & 0x4)
        return output + 1;
    if (curbyte & 0x2)
        return output + 2;
    if (curbyte & 0x1)
        return output + 3;

    /* shouldn't get here: */
    return output + 4;
}
#endif

#define RICE_THRESHOLD 8 // maximum number of bits for a rice prefix.

static int32_t entropy_decode_value(alac_file* alac,
                             int readSampleSize,
                             int k,
                             int rice_kmodifier_mask)
{
    int32_t x = 0; // decoded value

    // read x, number of 1s before 0 represent the rice value.
    while (x <= RICE_THRESHOLD && readbit(alac))
    {
        x++;
    }

    if (x > RICE_THRESHOLD)
    {
        // read the number from the bit stream (raw value)
        int32_t value;

        value = readbits(alac, readSampleSize);

        // mask value
        value &= (((uint32_t)0xffffffff) >> (32 - readSampleSize));

        x = value;
    }
    else
    {
        if (k != 1)
        {
            int extraBits = readbits(alac, k);

            // x = x * (2^k - 1)
            x *= (((1 << k) - 1) & rice_kmodifier_mask);

            if (extraBits > 1)
                x += extraBits - 1;
            else
                unreadbits(alac, 1);
        }
    }

    return x;
}

static void entropy_rice_decode(alac_file* alac,
                         int32_t* outputBuffer,
                         int outputSize,
                         int readSampleSize,
                         int rice_initialhistory,
                         int rice_kmodifier,
                         int rice_historymult,
                         int rice_kmodifier_mask)
{
    int             outputCount;
    int             history = rice_initialhistory;
    int             signModifier = 0;

    for (outputCount = 0; outputCount < outputSize; outputCount++)
    {
        int32_t     decodedValue;
        int32_t     finalValue;
        int32_t     k;

        k = 31 - rice_kmodifier - count_leading_zeros((history >> 9) + 3);

        if (k < 0) k += rice_kmodifier;
        else k = rice_kmodifier;

        // note: don't use rice_kmodifier_mask here (set mask to 0xFFFFFFFF)
        decodedValue = entropy_decode_value(alac, readSampleSize, k, 0xFFFFFFFF);

        decodedValue += signModifier;
        finalValue = (decodedValue + 1) / 2; // inc by 1 and shift out sign bit
        if (decodedValue & 1) // the sign is stored in the low bit
            finalValue *= -1;

        outputBuffer[outputCount] = finalValue;

        signModifier = 0;

        // update history
        history += (decodedValue * rice_historymult)
                - ((history * rice_historymult) >> 9);

        if (decodedValue > 0xFFFF)
            history = 0xFFFF;

        // special case, for compressed blocks of 0
        if ((history < 128) && (outputCount + 1 < outputSize))
        {
            int32_t     blockSize;

            signModifier = 1;

            k = count_leading_zeros(history) + ((history + 16) / 64) - 24;

            // note: blockSize is always 16bit
            blockSize = entropy_decode_value(alac, 16, k, rice_kmodifier_mask);

            // got blockSize 0s
            if (blockSize > 0)
            {
                memset(&outputBuffer[outputCount + 1], 0, blockSize * sizeof(*outputBuffer));
                outputCount += blockSize;
            }

            if (blockSize > 0xFFFF)
                signModifier = 0;

            history = 0;
        }
    }
}

#define SIGN_EXTENDED32(val, bits) ((val << (32 - bits)) >> (32 - bits))

#define SIGN_ONLY(v) \
                     ((v < 0) ? (-1) : \
                                ((v > 0) ? (1) : \
                                           (0)))

static void predictor_decompress_fir_adapt(int32_t *error_buffer,
                                           int32_t *buffer_out,
                                           int output_size,
                                           int readsamplesize,
                                           int16_t *predictor_coef_table,
                                           int predictor_coef_num,
                                           int predictor_quantitization)
{
    int i;

    /* first sample always copies */
    *buffer_out = *error_buffer;

    if (!predictor_coef_num)
    {
        if (output_size <= 1) return;
        memcpy(buffer_out+1, error_buffer+1, (output_size-1) * 4);
        return;
    }

    if (predictor_coef_num == 0x1f) /* 11111 - max value of predictor_coef_num */
    { /* second-best case scenario for fir decompression,
       * error describes a small difference from the previous sample only
       */
        if (output_size <= 1) return;
        for (i = 0; i < output_size - 1; i++)
        {
            int32_t prev_value;
            int32_t error_value;

            prev_value = buffer_out[i];
            error_value = error_buffer[i+1];
            buffer_out[i+1] = SIGN_EXTENDED32((prev_value + error_value), readsamplesize);
        }
        return;
    }

    /* read warm-up samples */
    if (predictor_coef_num > 0)
    {
        int i;
        for (i = 0; i < predictor_coef_num; i++)
        {
            int32_t val;

            val = buffer_out[i] + error_buffer[i+1];

            val = SIGN_EXTENDED32(val, readsamplesize);

            buffer_out[i+1] = val;
        }
    }

#if 0
    /* 4 and 8 are very common cases (the only ones i've seen). these
     * should be unrolled and optimised
     */
    if (predictor_coef_num == 4)
    {
        /* FIXME: optimised general case */
        return;
    }

    if (predictor_coef_table == 8)
    {
        /* FIXME: optimised general case */
        return;
    }
#endif


    /* general case */
    if (predictor_coef_num > 0)
    {
        for (i = predictor_coef_num + 1;
             i < output_size;
             i++)
        {
            int j;
            int sum = 0;
            int outval;
            int error_val = error_buffer[i];

            for (j = 0; j < predictor_coef_num; j++)
            {
                sum += (buffer_out[predictor_coef_num-j] - buffer_out[0]) *
                       predictor_coef_table[j];
            }

            outval = (1 << (predictor_quantitization-1)) + sum;
            outval = outval >> predictor_quantitization;
            outval = outval + buffer_out[0] + error_val;
            outval = SIGN_EXTENDED32(outval, readsamplesize);

            buffer_out[predictor_coef_num+1] = outval;

            if (error_val > 0)
            {
                int predictor_num = predictor_coef_num - 1;

                while (predictor_num >= 0 && error_val > 0)
                {
                    int val = buffer_out[0] - buffer_out[predictor_coef_num - predictor_num];
                    int sign = SIGN_ONLY(val);

                    predictor_coef_table[predictor_num] -= sign;

                    val *= sign; /* absolute value */

                    error_val -= ((val >> predictor_quantitization) *
                                  (predictor_coef_num - predictor_num));

                    predictor_num--;
                }
            }
            else if (error_val < 0)
            {
                int predictor_num = predictor_coef_num - 1;

                while (predictor_num >= 0 && error_val < 0)
                {
                    int val = buffer_out[0] - buffer_out[predictor_coef_num - predictor_num];
                    int sign = - SIGN_ONLY(val);

                    predictor_coef_table[predictor_num] -= sign;

                    val *= sign; /* neg value */

                    error_val -= ((val >> predictor_quantitization) *
                                  (predictor_coef_num - predictor_num));

                    predictor_num--;
                }
            }

            buffer_out++;
        }
    }
}

static void deinterlace_16(int32_t *buffer_a, int32_t *buffer_b,
                    int16_t *buffer_out,
                    int numchannels, int numsamples,
                    uint8_t interlacing_shift,
                    uint8_t interlacing_leftweight)
{
    int i;
    if (numsamples <= 0) return;

    /* weighted interlacing */
    if (interlacing_leftweight)
    {
        for (i = 0; i < numsamples; i++)
        {
            int32_t difference, midright;
            int16_t left;
            int16_t right;

            midright = buffer_a[i];
            difference = buffer_b[i];


            right = midright - ((difference * interlacing_leftweight) >> interlacing_shift);
            left = right + difference;

            /* output is always little endian */
            if (host_bigendian)
            {
                _Swap16(left);
                _Swap16(right);
            }

            buffer_out[i*numchannels] = left;
            buffer_out[i*numchannels + 1] = right;
        }

        return;
    }

    /* otherwise basic interlacing took place */
    for (i = 0; i < numsamples; i++)
    {
        int16_t left, right;

        left = buffer_a[i];
        right = buffer_b[i];

        /* output is always little endian */
        if (host_bigendian)
        {
            _Swap16(left);
            _Swap16(right);
        }

        buffer_out[i*numchannels] = left;
        buffer_out[i*numchannels + 1] = right;
    }
}

static void deinterlace_24(int32_t *buffer_a, int32_t *buffer_b,
                    int uncompressed_bytes,
                    int32_t *uncompressed_bytes_buffer_a, int32_t *uncompressed_bytes_buffer_b,
                    void *buffer_out,
                    int numchannels, int numsamples,
                    uint8_t interlacing_shift,
                    uint8_t interlacing_leftweight)
{
    int i;
    if (numsamples <= 0) return;

    /* weighted interlacing */
    if (interlacing_leftweight)
    {
        for (i = 0; i < numsamples; i++)
        {
            int32_t difference, midright;
            int32_t left;
            int32_t right;

            midright = buffer_a[i];
            difference = buffer_b[i];

            right = midright - ((difference * interlacing_leftweight) >> interlacing_shift);
            left = right + difference;

            if (uncompressed_bytes)
            {
                uint32_t mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
                left <<= (uncompressed_bytes * 8);
                right <<= (uncompressed_bytes * 8);

                left |= uncompressed_bytes_buffer_a[i] & mask;
                right |= uncompressed_bytes_buffer_b[i] & mask;
            }

            ((uint8_t*)buffer_out)[i * numchannels * 3] = (left) & 0xFF;
            ((uint8_t*)buffer_out)[i * numchannels * 3 + 1] = (left >> 8) & 0xFF;
            ((uint8_t*)buffer_out)[i * numchannels * 3 + 2] = (left >> 16) & 0xFF;

            ((uint8_t*)buffer_out)[i * numchannels * 3 + 3] = (right) & 0xFF;
            ((uint8_t*)buffer_out)[i * numchannels * 3 + 4] = (right >> 8) & 0xFF;
            ((uint8_t*)buffer_out)[i * numchannels * 3 + 5] = (right >> 16) & 0xFF;
        }

        return;
    }

    /* otherwise basic interlacing took place */
    for (i = 0; i < numsamples; i++)
    {
        int32_t left, right;

        left = buffer_a[i];
        right = buffer_b[i];

        if (uncompressed_bytes)
        {
            uint32_t mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
            left <<= (uncompressed_bytes * 8);
            right <<= (uncompressed_bytes * 8);

            left |= uncompressed_bytes_buffer_a[i] & mask;
            right |= uncompressed_bytes_buffer_b[i] & mask;
        }

        ((uint8_t*)buffer_out)[i * numchannels * 3] = (left) & 0xFF;
        ((uint8_t*)buffer_out)[i * numchannels * 3 + 1] = (left >> 8) & 0xFF;
        ((uint8_t*)buffer_out)[i * numchannels * 3 + 2] = (left >> 16) & 0xFF;

        ((uint8_t*)buffer_out)[i * numchannels * 3 + 3] = (right) & 0xFF;
        ((uint8_t*)buffer_out)[i * numchannels * 3 + 4] = (right >> 8) & 0xFF;
        ((uint8_t*)buffer_out)[i * numchannels * 3 + 5] = (right >> 16) & 0xFF;

    }

}

void decode_frame(alac_file *alac,
                  unsigned char *inbuffer,
                  void *outbuffer, int *outputsize)
{
    int channels;
    int32_t outputsamples = alac->setinfo_max_samples_per_frame;

    /* setup the stream */
    alac->input_buffer = inbuffer;
    alac->input_buffer_bitaccumulator = 0;

    channels = readbits(alac, 3);

    *outputsize = outputsamples * alac->bytespersample;

    switch(channels)
    {
    case 0: /* 1 channel */
    {
        int hassize;
        int isnotcompressed;
        int readsamplesize;

        int uncompressed_bytes;
        int ricemodifier;

        /* 2^result = something to do with output waiting.
         * perhaps matters if we read > 1 frame in a pass?
         */
        readbits(alac, 4);

        readbits(alac, 12); /* unknown, skip 12 bits */

        hassize = readbits(alac, 1); /* the output sample size is stored soon */

        uncompressed_bytes = readbits(alac, 2); /* number of bytes in the (compressed) stream that are not compressed */

        isnotcompressed = readbits(alac, 1); /* whether the frame is compressed */

        if (hassize)
        {
            /* now read the number of samples,
             * as a 32bit integer */
            outputsamples = readbits(alac, 32);
            *outputsize = outputsamples * alac->bytespersample;
        }

        readsamplesize = alac->setinfo_sample_size - (uncompressed_bytes * 8);

        if (!isnotcompressed)
        { /* so it is compressed */
            int16_t predictor_coef_table[32];
            int predictor_coef_num;
            int prediction_type;
            int prediction_quantitization;
            int i;

            /* skip 16 bits, not sure what they are. seem to be used in
             * two channel case */
            readbits(alac, 8);
            readbits(alac, 8);

            prediction_type = readbits(alac, 4);
            prediction_quantitization = readbits(alac, 4);

            ricemodifier = readbits(alac, 3);
            predictor_coef_num = readbits(alac, 5);

            /* read the predictor table */
            for (i = 0; i < predictor_coef_num; i++)
            {
                predictor_coef_table[i] = (int16_t)readbits(alac, 16);
            }

            if (uncompressed_bytes)
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    alac->uncompressed_bytes_buffer_a[i] = readbits(alac, uncompressed_bytes * 8);
                }
            }

            entropy_rice_decode(alac,
                                alac->predicterror_buffer_a,
                                outputsamples,
                                readsamplesize,
                                alac->setinfo_rice_initialhistory,
                                alac->setinfo_rice_kmodifier,
                                ricemodifier * alac->setinfo_rice_historymult / 4,
                                (1 << alac->setinfo_rice_kmodifier) - 1);

            if (prediction_type == 0)
            { /* adaptive fir */
                predictor_decompress_fir_adapt(alac->predicterror_buffer_a,
                                               alac->outputsamples_buffer_a,
                                               outputsamples,
                                               readsamplesize,
                                               predictor_coef_table,
                                               predictor_coef_num,
                                               prediction_quantitization);
            }
            else
            {
                spdlog::debug( "FIXME: unhandled predicition type: %i\n", prediction_type);
                /* i think the only other prediction type (or perhaps this is just a
                 * boolean?) runs adaptive fir twice.. like:
                 * predictor_decompress_fir_adapt(predictor_error, tempout, ...)
                 * predictor_decompress_fir_adapt(predictor_error, outputsamples ...)
                 * little strange..
                 */
            }

        }
        else
        { /* not compressed, easy case */
            if (alac->setinfo_sample_size <= 16)
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    int32_t audiobits = readbits(alac, alac->setinfo_sample_size);

                    audiobits = SIGN_EXTENDED32(audiobits, alac->setinfo_sample_size);

                    alac->outputsamples_buffer_a[i] = audiobits;
                }
            }
            else
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    int32_t audiobits;

                    audiobits = readbits(alac, 16);
                    /* special case of sign extension..
                     * as we'll be ORing the low 16bits into this */
                    audiobits = audiobits << (alac->setinfo_sample_size - 16);
                    audiobits |= readbits(alac, alac->setinfo_sample_size - 16);
                    audiobits = SignExtend24(audiobits);

                    alac->outputsamples_buffer_a[i] = audiobits;
                }
            }
            uncompressed_bytes = 0; // always 0 for uncompressed
        }

        switch(alac->setinfo_sample_size)
        {
        case 16:
        {
            int i;
            for (i = 0; i < outputsamples; i++)
            {
                int16_t sample = alac->outputsamples_buffer_a[i];
                if (host_bigendian)
                    _Swap16(sample);
                ((int16_t*)outbuffer)[i * alac->numchannels] = sample;
            }
            break;
        }
        case 24:
        {
            int i;
            for (i = 0; i < outputsamples; i++)
            {
                int32_t sample = alac->outputsamples_buffer_a[i];

                if (uncompressed_bytes)
                {
                    uint32_t mask;
                    sample = sample << (uncompressed_bytes * 8);
                    mask = ~(0xFFFFFFFF << (uncompressed_bytes * 8));
                    sample |= alac->uncompressed_bytes_buffer_a[i] & mask;
                }

                ((uint8_t*)outbuffer)[i * alac->numchannels * 3] = (sample) & 0xFF;
                ((uint8_t*)outbuffer)[i * alac->numchannels * 3 + 1] = (sample >> 8) & 0xFF;
                ((uint8_t*)outbuffer)[i * alac->numchannels * 3 + 2] = (sample >> 16) & 0xFF;
            }
            break;
        }
        case 20:
        case 32:
            spdlog::debug( "FIXME: unimplemented sample size %i\n", alac->setinfo_sample_size);
            break;
        default:
            break;
        }
        break;
    }
    case 1: /* 2 channels */
    {
        int hassize;
        int isnotcompressed;
        int readsamplesize;

        int uncompressed_bytes;

        uint8_t interlacing_shift;
        uint8_t interlacing_leftweight;

        /* 2^result = something to do with output waiting.
         * perhaps matters if we read > 1 frame in a pass?
         */
        readbits(alac, 4);

        readbits(alac, 12); /* unknown, skip 12 bits */

        hassize = readbits(alac, 1); /* the output sample size is stored soon */

        uncompressed_bytes = readbits(alac, 2); /* the number of bytes in the (compressed) stream that are not compressed */

        isnotcompressed = readbits(alac, 1); /* whether the frame is compressed */

        if (hassize)
        {
            /* now read the number of samples,
             * as a 32bit integer */
            outputsamples = readbits(alac, 32);
            *outputsize = outputsamples * alac->bytespersample;
        }

        readsamplesize = alac->setinfo_sample_size - (uncompressed_bytes * 8) + 1;

        if (!isnotcompressed)
        { /* compressed */
            int16_t predictor_coef_table_a[32];
            int predictor_coef_num_a;
            int prediction_type_a;
            int prediction_quantitization_a;
            int ricemodifier_a;

            int16_t predictor_coef_table_b[32];
            int predictor_coef_num_b;
            int prediction_type_b;
            int prediction_quantitization_b;
            int ricemodifier_b;

            int i;

            interlacing_shift = readbits(alac, 8);
            interlacing_leftweight = readbits(alac, 8);

            /******** channel 1 ***********/
            prediction_type_a = readbits(alac, 4);
            prediction_quantitization_a = readbits(alac, 4);

            ricemodifier_a = readbits(alac, 3);
            predictor_coef_num_a = readbits(alac, 5);

            /* read the predictor table */
            for (i = 0; i < predictor_coef_num_a; i++)
            {
                predictor_coef_table_a[i] = (int16_t)readbits(alac, 16);
            }

            /******** channel 2 *********/
            prediction_type_b = readbits(alac, 4);
            prediction_quantitization_b = readbits(alac, 4);

            ricemodifier_b = readbits(alac, 3);
            predictor_coef_num_b = readbits(alac, 5);

            /* read the predictor table */
            for (i = 0; i < predictor_coef_num_b; i++)
            {
                predictor_coef_table_b[i] = (int16_t)readbits(alac, 16);
            }

            /*********************/
            if (uncompressed_bytes)
            { /* see mono case */
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    alac->uncompressed_bytes_buffer_a[i] = readbits(alac, uncompressed_bytes * 8);
                    alac->uncompressed_bytes_buffer_b[i] = readbits(alac, uncompressed_bytes * 8);
                }
            }

            /* channel 1 */
            entropy_rice_decode(alac,
                                alac->predicterror_buffer_a,
                                outputsamples,
                                readsamplesize,
                                alac->setinfo_rice_initialhistory,
                                alac->setinfo_rice_kmodifier,
                                ricemodifier_a * alac->setinfo_rice_historymult / 4,
                                (1 << alac->setinfo_rice_kmodifier) - 1);

            if (prediction_type_a == 0)
            { /* adaptive fir */
                predictor_decompress_fir_adapt(alac->predicterror_buffer_a,
                                               alac->outputsamples_buffer_a,
                                               outputsamples,
                                               readsamplesize,
                                               predictor_coef_table_a,
                                               predictor_coef_num_a,
                                               prediction_quantitization_a);
            }
            else
            { /* see mono case */
                spdlog::debug( "FIXME: unhandled predicition type: %i\n", prediction_type_a);
            }

            /* channel 2 */
            entropy_rice_decode(alac,
                                alac->predicterror_buffer_b,
                                outputsamples,
                                readsamplesize,
                                alac->setinfo_rice_initialhistory,
                                alac->setinfo_rice_kmodifier,
                                ricemodifier_b * alac->setinfo_rice_historymult / 4,
                                (1 << alac->setinfo_rice_kmodifier) - 1);

            if (prediction_type_b == 0)
            { /* adaptive fir */
                predictor_decompress_fir_adapt(alac->predicterror_buffer_b,
                                               alac->outputsamples_buffer_b,
                                               outputsamples,
                                               readsamplesize,
                                               predictor_coef_table_b,
                                               predictor_coef_num_b,
                                               prediction_quantitization_b);
            }
            else
            {
                spdlog::debug( "FIXME: unhandled predicition type: %i\n", prediction_type_b);
            }
        }
        else
        { /* not compressed, easy case */
            if (alac->setinfo_sample_size <= 16)
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    int32_t audiobits_a, audiobits_b;

                    audiobits_a = readbits(alac, alac->setinfo_sample_size);
                    audiobits_b = readbits(alac, alac->setinfo_sample_size);

                    audiobits_a = SIGN_EXTENDED32(audiobits_a, alac->setinfo_sample_size);
                    audiobits_b = SIGN_EXTENDED32(audiobits_b, alac->setinfo_sample_size);

                    alac->outputsamples_buffer_a[i] = audiobits_a;
                    alac->outputsamples_buffer_b[i] = audiobits_b;
                }
            }
            else
            {
                int i;
                for (i = 0; i < outputsamples; i++)
                {
                    int32_t audiobits_a, audiobits_b;

                    audiobits_a = readbits(alac, 16);
                    audiobits_a = audiobits_a << (alac->setinfo_sample_size - 16);
                    audiobits_a |= readbits(alac, alac->setinfo_sample_size - 16);
                    audiobits_a = SignExtend24(audiobits_a);

                    audiobits_b = readbits(alac, 16);
                    audiobits_b = audiobits_b << (alac->setinfo_sample_size - 16);
                    audiobits_b |= readbits(alac, alac->setinfo_sample_size - 16);
                    audiobits_b = SignExtend24(audiobits_b);

                    alac->outputsamples_buffer_a[i] = audiobits_a;
                    alac->outputsamples_buffer_b[i] = audiobits_b;
                }
            }
            uncompressed_bytes = 0; // always 0 for uncompressed
            interlacing_shift = 0;
            interlacing_leftweight = 0;
        }

        switch(alac->setinfo_sample_size)
        {
        case 16:
        {
            deinterlace_16(alac->outputsamples_buffer_a,
                           alac->outputsamples_buffer_b,
                           (int16_t*)outbuffer,
                           alac->numchannels,
                           outputsamples,
                           interlacing_shift,
                           interlacing_leftweight);
            break;
        }
        case 24:
        {
            deinterlace_24(alac->outputsamples_buffer_a,
                           alac->outputsamples_buffer_b,
                           uncompressed_bytes,
                           alac->uncompressed_bytes_buffer_a,
                           alac->uncompressed_bytes_buffer_b,
                           (int16_t*)outbuffer,
                           alac->numchannels,
                           outputsamples,
                           interlacing_shift,
                           interlacing_leftweight);
            break;
        }
        case 20:
        case 32:
            spdlog::debug( "FIXME: unimplemented sample size %i\n", alac->setinfo_sample_size);
            break;
        default:
            break;
        }

        break;
    }
    }
}

alac_file *create_alac(int samplesize, int numchannels)
{
    alac_file *newfile = (alac_file*)malloc(sizeof(alac_file));

    if (!newfile)
    {
        throw bad_alloc();
    }
    newfile->samplesize = samplesize;
    newfile->numchannels = numchannels;
    newfile->bytespersample = (samplesize / 8) * numchannels;

    return newfile;
}

void destroy_alac(alac_file* alac) noexcept
{
	deallocate_buffers(alac);
	free(alac);
}
} // namespace alac