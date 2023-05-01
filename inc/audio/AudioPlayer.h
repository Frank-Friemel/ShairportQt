/////////////////////////////////////////////////////////////////////////////////
// AudioPlayer

#pragma once

#include "Mmsystem.h"
#include <memory>
#include <objidl.h>

class 	WavePlayThread;

///////////////////////////////////////////////////////////
// AudioPlayer

class AudioPlayer 
{
public:
	AudioPlayer();
	~AudioPlayer();

	// data stream
	// set "isRawStream" to true if there is now wav-header
	bool	Init(IStream* pDataSource, bool isRawStream = false);
	void	Close();

	void	Play();
	void	Pause();

	void	WaitDone();

	void	Mute(bool bMute);
	bool	IsMuted() const;
		
public:
	// the default implementation just copies the data over from the stream
	virtual bool OnPlayAudio(unsigned char* pData, unsigned long dwLen);

protected:
	IStream*						m_pAudioData;
	std::shared_ptr<WavePlayThread>	m_threadWavePlay;
};

