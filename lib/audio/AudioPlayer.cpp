#ifdef _WIN32
#include "LayerCake.h"
#include <mmeapi.h>
#include "audio/AudioPlayer.h"
#include "audio/PlaySound.h"
#include <functional>
#include <list>
#include <vector>
#include "audio/WaveHeader.h"
#include <Mmdeviceapi.h>
#include <FunctionDiscoveryKeys_devpkey.h>

#pragma comment(lib, "Winmm.lib")

#define NUM_CHANNELS	2
#define	SAMPLE_SIZE		16
#define	SAMPLE_FACTOR	(NUM_CHANNELS * (SAMPLE_SIZE >> 3))
#define	SAMPLE_FREQ		(44100.0)
#define BYTES_PER_SEC	(SAMPLE_FREQ * SAMPLE_FACTOR)

// aligns to the nearest sample (previous or next)
#define	ALIGN_TO_SAMPLE(__ToAlign)		{ auto __mod = __ToAlign  % SAMPLE_FACTOR; __ToAlign += (__mod > SAMPLE_FACTOR / 2) ? (SAMPLE_FACTOR - __mod) : (__mod * (-1)); }

// aligns to the previous sample
#define	ALIGN_DOWN_TO_SAMPLE(__ToAlign)	{ __ToAlign -= (__ToAlign  % SAMPLE_FACTOR); }

////////////////////////////////////////////////////////////////
// CWaveQueueItem

class CWaveQueueItem
{
public:
	CWaveQueueItem()
	{
		m_nLen		= 0;
		InitNew();
	}
	inline bool IsPlayed() const
	{
		return m_bPlayed;
	}
	inline bool IsDone() const
	{
		return m_WaveHeader.dwFlags & WHDR_DONE ? true : false;
	}
	inline void InitNew()
	{
		memset(&m_WaveHeader, 0, sizeof(m_WaveHeader));
		m_bPlayed	= false;
	}
	bool Prepare(HWAVEOUT hWaveOut);
	void UnPrepare(HWAVEOUT hWaveOut);
	bool Play(HWAVEOUT hWaveOut);
	void WaitIfPlayed();
	void Mute();

public:
	std::vector<unsigned char>	m_Blob;
	ULONG						m_nLen;

private:
	WAVEHDR						m_WaveHeader;
	bool						m_bPlayed;
};

///////////////////////////////////////////////////////////
// WavePlayThread

class WavePlayThread
{
	friend class AudioPlayer;

public:
	typedef std::function<bool(unsigned char* pStream, ULONG dwLen)>	callback_t;

	WavePlayThread(UINT nDeviceID = WAVE_MAPPER);
	~WavePlayThread();

	void Init(ULONG nFreq = (ULONG)SAMPLE_FREQ, ULONG nChannels = NUM_CHANNELS, ULONG nSampleSizeBits = SAMPLE_SIZE);
	bool Start(callback_t callback, size_t nThreshold = 16);
	void Stop();
	void WaitDone();

	void		Pause();
	inline bool IsPaused() const
	{
		return m_bPaused;
	}
	void		Mute(bool bMute);
	inline bool IsMuted() const
	{
		return m_bMuted;
	}
	void SetVolume(WORD wVolume);
	WORD GetVolume();

protected:
	bool OnEvent();

private:
	bool Alloc(std::shared_ptr<CWaveQueueItem>& pItem);
	void Free(std::shared_ptr<CWaveQueueItem>& pItem);

protected:
	HANDLE					m_hEvent;
	HWAVEOUT				m_hWaveOut;

	typedef std::list<std::shared_ptr<CWaveQueueItem>>	CWaveQueueItemList;

	CWaveQueueItemList		m_queue;
	CWaveQueueItemList		m_pool;

	UINT					m_nDeviceID;
	size_t					m_nThreshold;
	volatile bool			m_bPaused;
	volatile bool			m_bMuted;
	callback_t				m_callback;

public:
	WAVEFORMATEX			m_format;
	UINT					m_nRealDeviceID;
};


/////////////////////////////////////////////////////////////////////////////////
// CWaveQueueItem

bool CWaveQueueItem::Prepare(HWAVEOUT hWaveOut)
{
	bool bResult = true;

	if ((m_WaveHeader.dwFlags & WHDR_PREPARED) == 0)
	{
		m_WaveHeader.dwBufferLength	= m_nLen;
		m_WaveHeader.lpData			= (LPSTR)m_Blob.data();

		if (MMSYSERR_NOERROR != waveOutPrepareHeader(hWaveOut, &m_WaveHeader, sizeof(m_WaveHeader)))
		{
			bResult = false;
			memset(&m_WaveHeader, 0, sizeof(m_WaveHeader));
		}
	}
	else
	{
		m_WaveHeader.dwBufferLength	= m_nLen;
	}
	m_bPlayed	= false;

	return bResult;
}


void CWaveQueueItem::UnPrepare(HWAVEOUT hWaveOut)
{
	if (m_WaveHeader.dwFlags & WHDR_PREPARED)
	{
		while (waveOutUnprepareHeader(hWaveOut, &m_WaveHeader, sizeof(m_WaveHeader)) != MMSYSERR_NOERROR)
			Sleep(10);
		InitNew();
	}
}

bool CWaveQueueItem::Play(HWAVEOUT hWaveOut)
{
	m_WaveHeader.dwFlags &= ~WHDR_DONE;

	if (waveOutWrite(hWaveOut, &m_WaveHeader, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
	{
		m_bPlayed = true;
		return true;
	}
	return false;
}

void CWaveQueueItem::WaitIfPlayed()
{
	if (m_bPlayed)
	{
		while (!(m_WaveHeader.dwFlags & WHDR_DONE))
		{
			Sleep(10);
		}
	}
}

void  CWaveQueueItem::Mute()
{
	memset(m_Blob.data(), 0, m_nLen);
}

/////////////////////////////////////////////////////////////////////////////////
// WavePlayThread

WavePlayThread::WavePlayThread(UINT nDeviceID /*= WAVE_MAPPER*/)
{
	m_hWaveOut		= NULL;
	m_nDeviceID		= nDeviceID;
	m_nRealDeviceID	= 0;
	m_nThreshold	= 16;
	Init();
	m_bPaused		= false;
	m_bMuted		= false;
	m_hEvent		= NULL;
}

WavePlayThread::~WavePlayThread()
{
	Stop();

	if (m_hEvent)
	{
		::CloseHandle(m_hEvent);
	}
}

void WavePlayThread::Init(ULONG nFreq /*= 44100*/, ULONG nChannels /*= 2*/, ULONG nSampleSizeBits /*= 16*/)
{
	memset(&m_format, 0, sizeof(m_format));

	m_format.cbSize				= 0;
	m_format.wFormatTag			= WAVE_FORMAT_PCM;
	m_format.nSamplesPerSec		= nFreq;
	m_format.nChannels			= (WORD) nChannels;
	m_format.wBitsPerSample		= (WORD) nSampleSizeBits;
	m_format.nBlockAlign		= (WORD) (nChannels * (nSampleSizeBits >> 3));
	m_format.nAvgBytesPerSec	= nFreq * m_format.nBlockAlign;

	m_pool.clear();
}

bool WavePlayThread::Alloc(std::shared_ptr<CWaveQueueItem>& pItem)
{
	if (pItem)
		pItem.reset();

	if (!m_pool.empty())
	{
		pItem = std::move(m_pool.front());
		m_pool.pop_front();
	}
	else
	{
		pItem = std::make_shared<CWaveQueueItem>();

		pItem->m_nLen = 4096;

		try
		{
			pItem->m_Blob.resize(pItem->m_nLen);
		}
		catch (...)
		{
			return false;
		}
	}
	memset(pItem->m_Blob.data(), 0, pItem->m_nLen);
	return true;
}

void WavePlayThread::Free(std::shared_ptr<CWaveQueueItem>& pItem)
{
	assert(pItem);
	pItem->InitNew();
	m_pool.emplace_back(std::move(pItem));
}

bool WavePlayThread::Start(callback_t callback, size_t nThreshold /*= 16*/)
{
	bool bResult = false;

	m_nThreshold	= nThreshold;
	m_callback		= callback;

	assert(m_callback);

	if (!m_hEvent)
	{
		m_hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	}
	else
	{
		::ResetEvent(m_hEvent);
	}
	assert(m_hWaveOut == NULL);
	m_bPaused = false;
	m_bMuted = false;

	if (MMSYSERR_NOERROR != waveOutOpen(&m_hWaveOut, m_nDeviceID, &m_format, (DWORD_PTR)(HANDLE)m_hEvent, NULL, CALLBACK_EVENT))
	{
		m_hWaveOut = NULL;
	}
	else
	{
		// suspend player, so we can control when to start
		if (waveOutPause(m_hWaveOut) == MMSYSERR_NOERROR)
		{
			m_bPaused = true;
		}
		else
		{
			// unexpected
			assert(FALSE);
		}
		bResult = true;

		if (MMSYSERR_NOERROR != waveOutGetID(m_hWaveOut, &m_nRealDeviceID))
		{
			m_nRealDeviceID = m_nDeviceID;
		}
	}
	if (!bResult)
	{
		Stop();
	}
	return bResult;
}

void WavePlayThread::Stop()
{
	if (m_hWaveOut)
	{
		// waveOutReset will abort all queued items
		if (waveOutReset(m_hWaveOut) != MMSYSERR_NOERROR)
		{
			assert(false);
		}

		while (!m_queue.empty())
		{
			std::shared_ptr<CWaveQueueItem> pItem = std::move(m_queue.front());
			m_queue.pop_front();

			// wait for abortion to complete
			pItem->WaitIfPlayed();
			pItem->UnPrepare(m_hWaveOut);
			pItem.reset();
		}
		m_pool.clear();

		if (waveOutClose(m_hWaveOut) != MMSYSERR_NOERROR)
		{
			assert(false);
		}

		m_hWaveOut = NULL;
		m_bPaused = false;
	}
}

void WavePlayThread::WaitDone()
{
	while (!m_queue.empty())
	{
		std::shared_ptr<CWaveQueueItem> pItem = std::move(m_queue.front());
		m_queue.pop_front();

		// wait for abortion to complete
		pItem->WaitIfPlayed();
	}
}

bool WavePlayThread::OnEvent()
{
	bool result = true;

	while (!m_queue.empty())
	{
		if (m_queue.front()->IsDone())
		{
			auto pItem = std::move(m_queue.front());
			m_queue.pop_front();
			Free(pItem);
		}
		else
		{
			break;
		}
	}
	if (!m_bPaused)
	{
		while (m_nThreshold >= m_queue.size())
		{
			std::shared_ptr<CWaveQueueItem> pItem;

			if (!Alloc(pItem))
				break;
			result = m_callback(pItem->m_Blob.data(), pItem->m_nLen);

			if (pItem->Prepare(m_hWaveOut))
			{
				m_queue.emplace_back(std::move(pItem));
			}
			else
			{
				assert(FALSE);
			}
		}
		// https://docs.microsoft.com/en-us/windows/win32/multimedia/using-an-callback-to-process-driver-messages
		// After you call the waveOutPrepareHeader function, but before sending waveform-audio data to the device,
		// put the event into a nonsignaled state by calling the ResetEvent function
		::ResetEvent(m_hEvent);

		// now, play all items in the queue
		for (const auto& item : m_queue)
		{
			if (!item->IsPlayed())
			{
				if (m_bMuted)
				{
					item->Mute();
				}
				item->Play(m_hWaveOut);
			}
		}		
	}
	return result;
}

void WavePlayThread::Pause()
{
	if (m_hWaveOut != NULL)
	{
		if (m_bPaused)
		{
			m_bPaused = false;

			// signal worker thread to fill the queue
			::SetEvent(m_hEvent);
			Sleep(0);

			if (waveOutRestart(m_hWaveOut) != MMSYSERR_NOERROR)
			{
				assert(false);
			}
		}
		else
		{
			if (waveOutPause(m_hWaveOut) == MMSYSERR_NOERROR)
			{
				m_bPaused = true;
				::ResetEvent(m_hEvent);
			}
		}
	}
}

void WavePlayThread::Mute(bool bMute)
{
	m_bMuted = bMute;
}

void WavePlayThread::SetVolume(WORD wVolume)
{
	assert(m_hWaveOut != NULL);

	DWORD dwVolume = ((DWORD) wVolume) | (((DWORD) wVolume) << 16);
	if (waveOutSetVolume(m_hWaveOut, dwVolume) != MMSYSERR_NOERROR)
	{
		assert(false);
	}
}

WORD WavePlayThread::GetVolume()
{
	assert(m_hWaveOut != NULL);

	DWORD dwVolume = 0;
	if (waveOutGetVolume(m_hWaveOut, &dwVolume) != MMSYSERR_NOERROR)
	{
		assert(false);
	}
	return (WORD) (dwVolume & 0x0000ffff);
}

//////////////////////////////////////////////////////////
// AudioPlayer

AudioPlayer::AudioPlayer()
{
	m_pAudioData = NULL;
}

AudioPlayer::~AudioPlayer()
{
	Close();
}

void AudioPlayer::Mute(bool bMute)
{
	if (m_threadWavePlay)
	{
		m_threadWavePlay->Mute(bMute);
	}
}

bool AudioPlayer::IsMuted() const
{
	if (m_threadWavePlay)
	{
		return m_threadWavePlay->IsMuted();
	}
	return false;
}

bool AudioPlayer::Init(IStream* pStream, bool isRawStream /*= false*/)
{
	if (!pStream)
	{
		::SetLastError(ERROR_INVALID_PARAMETER);
		return false;
	}
	Close();

	m_pAudioData = pStream;

	m_pAudioData->AddRef();
	
	m_threadWavePlay = std::make_shared<WavePlayThread>();

	if (!isRawStream)
	{
		// seek to begin
		m_pAudioData->Seek({ 0 }, 0, NULL);

		AlsaAudio::WaveHeader wav_hdr;
		ULONG read = 0;

		if (SUCCEEDED(m_pAudioData->Read(&wav_hdr, sizeof(wav_hdr), &read)) && read == sizeof(wav_hdr))
		{
			if (wav_hdr.myData.audioFormat != 1)
			{
				Close();
				return false;
			}
			m_threadWavePlay->Init(wav_hdr.myData.sampleRate, wav_hdr.myData.numChannels, wav_hdr.myData.bitsPerSample);
		}
		else
		{
			Close();
			return false;
		}
	}
	return m_threadWavePlay->Start([this](unsigned char* pStream, ULONG dwLen) -> bool { return OnPlayAudio(pStream, dwLen); });
}

void AudioPlayer::Play()
{
	if (m_threadWavePlay && m_threadWavePlay->IsPaused())
	{
		m_threadWavePlay->Pause();

		while (::WaitForSingleObject(m_threadWavePlay->m_hEvent, INFINITE) == WAIT_OBJECT_0)
		{
			if (!m_threadWavePlay->OnEvent())
			{
				break;
			}
		}
	}
}

void AudioPlayer::WaitDone()
{
	if (m_threadWavePlay)
	{
		m_threadWavePlay->WaitDone();
	}
}

void AudioPlayer::Pause()
{
	if (m_threadWavePlay && !m_threadWavePlay->IsPaused())
	{
		m_threadWavePlay->Pause();
	}
}

void AudioPlayer::Close()
{
	if (m_threadWavePlay)
	{
		m_threadWavePlay->Stop();
		m_threadWavePlay.reset();
	}
	if (m_pAudioData)
	{
		m_pAudioData->Release();
		m_pAudioData = NULL;
	}
}

bool AudioPlayer::OnPlayAudio(unsigned char* pData, ULONG dwLen)
{
	if (dwLen)
	{
		memset(pData, 0, dwLen);

		int		nTry		= 5;
		DWORD	dwRead		= 0;
		DWORD	dwReadTotal	= 0;

		// read PCM data from handle directly to the sound buffer in mem
		while (nTry > 0 && dwLen)
		{
			if (m_pAudioData->Read(pData + dwReadTotal, dwLen, &dwRead) != S_OK)
			{
				return false;
			}
			if (dwRead == 0)
			{
				// the input stream might be slow ... give it some time to deliver the data (over all 25ms)
				nTry--;
				Sleep(5);
			}
			else
			{
				nTry		= 5;
				dwLen		-= dwRead;
				dwReadTotal += dwRead;
			}
		}
	}
	return true;
}

namespace AlsaAudio
{
	std::future<int> Play(IStream* stream, std::string device /*= "default"*/)
	{
		UNREFERENCED_PARAMETER(device);

		std::future<int> result;

		if (stream)
		{
			stream->AddRef();

			try
			{
				result = std::async(std::launch::async, [](IStream* stream, std::string device) -> int
					{
						UNREFERENCED_PARAMETER(device);

						SharedPtr<IStream> _stream;

						_stream.Attach(stream);

						AudioPlayer player;

						if (player.Init(_stream))
						{
							player.Play();
							player.WaitDone();
							return 0;
						}
						return -1;
					}, stream, device);
			}
			catch (...)
			{
				stream->Release();
				throw;
			}
		}
		return result;
	}

	std::future<int> Play(const void* buf, size_t bufsize, std::string device /*= "default"*/)
	{
		SharedPtr<BlobStream> stream = new BlobStream(bufsize, buf);
		return Play(stream, device);
	}

	std::future<int> Play(std::string file_path, std::string device /*= "default"*/)
	{
		SharedPtr<BlobStream> stream = new BlobStream();
		stream->FromFile(file_path);
		return Play(stream, device);
	}
}

static void GetFullAudioDeviceByShortName(std::wstring& strDevname, std::string* pVarAudioID = nullptr)
{
	if (strDevname.empty())
	{
		return;
	}
	size_t nLength = strDevname.length();

	std::wstring strBestMatch;

	IMMDeviceEnumerator* pEnumerator = NULL;

	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator)))
	{
		IMMDeviceCollection* pCollection = NULL;

		if (SUCCEEDED(pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, &pCollection)))
		{
			UINT count = 0;

			pCollection->GetCount(&count);

			for (UINT i = 0; i < count; ++i)
			{
				IMMDevice* pEndPoint = NULL;

				if (SUCCEEDED(pCollection->Item(i, &pEndPoint)))
				{
					IPropertyStore* pProps = NULL;

					if (SUCCEEDED(pEndPoint->OpenPropertyStore(STGM_READ, &pProps)))
					{
						PROPVARIANT varName;
						PropVariantInit(&varName);

						if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName)))
						{
							if (nLength <= wcslen(varName.pwszVal))
							{
								if (_wcsnicmp(varName.pwszVal, strDevname.c_str(), nLength) == 0)
								{
									if (wcslen(varName.pwszVal) > strBestMatch.length())
									{
										strBestMatch = varName.pwszVal;

										if (pVarAudioID)
										{
											PWSTR pStrID = NULL;

											if (SUCCEEDED(pEndPoint->GetId(&pStrID)))
											{
												*pVarAudioID = CW2AEX(pStrID);
												CoTaskMemFree(pStrID);
											}
										}
									}
								}
							}
							PropVariantClear(&varName);
						}
						pProps->Release();
					}
					pEndPoint->Release();
				}
			}
			pCollection->Release();
		}
		pEnumerator->Release();
	}
	if (!strBestMatch.empty())
	{
		strDevname = strBestMatch;
	}
	if (pVarAudioID && pVarAudioID->empty())
	{
		*pVarAudioID = CW2AEX(strDevname);
	}
}

std::map<std::string, std::string> AlsaAudio::ListDevices()
{
	std::map<std::string, std::string> result;

	WAVEOUTCAPSW caps;

	UINT n = waveOutGetNumDevs();

	// i is the "WaveMapperID"
	for (UINT i = 0; i < n; ++i)
	{
		if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
		{
			break;
		}
		std::wstring strDevname(caps.szPname);
		std::string deviceID;

		GetFullAudioDeviceByShortName(strDevname, &deviceID);

		if (!strDevname.empty() && !deviceID.empty())
		{
			result[std::move(deviceID)] = CW2AEX(strDevname);
		}
	}
	return result;
}

#endif // _WIN32