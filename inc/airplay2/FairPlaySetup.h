#pragma once

#include "airplay2/Ap2Crypto.h"

namespace AirPlay2
{
    // Answers POST /fp-setup (FairPlay SAP handshake) with fixed replies, like shairport-sync.
    // iOS requires this step before SETUP, but the AirPlay 2 audio keys come from the pairing,
    // therefore nothing is decrypted with FairPlay.
    // returns false if the request is not a supported fp-setup message
    bool HandleFairPlaySetup(const uint8_t* data, size_t len, Bytes& response);
}
