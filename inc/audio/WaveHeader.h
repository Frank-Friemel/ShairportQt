#pragma once

#include "stdint.h"
#include <memory.h>

namespace AlsaAudio
{
    class WaveHeader
    {
    public:
        struct
        {
            uint8_t	    chunkID[4];
            uint32_t	chunkSize;
            uint8_t	    format[4];
            uint8_t	    subchunk1ID[4];
            uint32_t	subchunk1Size;
            uint16_t	audioFormat;
            uint16_t	numChannels;
            uint32_t	sampleRate;
            uint32_t	byteRate;
            uint16_t	blockAlign;
            uint16_t	bitsPerSample;
            uint8_t	    subchunk2ID[4];
            uint32_t	subchunk2Size;
        } myData;
    public:
        inline void init(uint32_t sampleRate = 44100, uint8_t bitDepth = 16, uint8_t numChans = 2)
        {
            // set all the ASCII literals
            memcpy(myData.chunkID, "RIFF", 4);
            memcpy(myData.format, "WAVE", 4);
            memcpy(myData.subchunk1ID, "fmt ", 4);
            memcpy(myData.subchunk2ID, "data", 4);

            // set all the known numeric values
            myData.audioFormat = 1;
            myData.chunkSize = 36;
            myData.subchunk1Size = 16;
            myData.subchunk2Size = 0;

            // set the passed-in numeric values
            myData.sampleRate = sampleRate;
            myData.bitsPerSample = bitDepth;
            myData.numChannels = numChans;

            // calculate the remaining dependent values
            myData.blockAlign = (uint16_t)((uint16_t)myData.numChannels * (uint16_t)myData.bitsPerSample) / ((uint16_t)8);
            myData.byteRate = myData.sampleRate * myData.blockAlign;
        }
        inline void update(uint32_t numBytes)
        {
            myData.subchunk2Size = numBytes;
            myData.chunkSize = myData.subchunk2Size + 36;
        }
        inline uint8_t	mySize() const
        {
            return sizeof(myData);
        }
    };
} // namespace AlsaAudio
