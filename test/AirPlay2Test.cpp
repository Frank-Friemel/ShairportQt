#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>
#include "airplay2/AirPlay2Handler.h"
#include "airplay2/Ap2AudioSession.h"
#include "airplay2/Ap2Crypto.h"
#include "airplay2/Ap2Decoder.h"
#include "airplay2/Pairing.h"
#include "airplay2/Plist.h"
#include "airplay2/PtpClock.h"
#include "airplay2/SecureChannel.h"
#include "airplay2/Srp.h"
#include "airplay2/Tlv8.h"
#include "RaopServer.h"
#include "dnssd.h"
#include <chrono>
#include <cstdlib>
#include <thread>
#include <spdlog/spdlog.h>

using namespace std;
using namespace AirPlay2;

namespace
{
    Bytes FromString(const string& s)
    {
        return Bytes(s.begin(), s.end());
    }
}

TEST(AirPlay2, Tlv8Fragmentation)
{
    Bytes large(700);

    for (size_t i = 0; i < large.size(); ++i)
    {
        large[i] = static_cast<uint8_t>(i);
    }
    Tlv8 tlv;
    tlv.Add(TlvType::State, 3);
    tlv.Add(TlvType::PublicKey, large);
    tlv.Add(TlvType::Proof, Bytes{ 1, 2, 3 });

    const auto encoded = tlv.Encode();
    // 700 bytes need 3 fragments
    EXPECT_EQ(encoded.size(), 3u + (2u * 3u + 700u) + 5u);

    Tlv8 decoded;
    ASSERT_TRUE(Tlv8::Decode(encoded.data(), encoded.size(), decoded));

    uint8_t state = 0;
    EXPECT_TRUE(decoded.GetByte(TlvType::State, state));
    EXPECT_EQ(state, 3);
    ASSERT_NE(decoded.Get(TlvType::PublicKey), nullptr);
    EXPECT_EQ(*decoded.Get(TlvType::PublicKey), large);
    ASSERT_NE(decoded.Get(TlvType::Proof), nullptr);
    EXPECT_EQ(decoded.Get(TlvType::Proof)->size(), 3u);
    EXPECT_EQ(decoded.Get(TlvType::Salt), nullptr);

    // truncated data
    EXPECT_FALSE(Tlv8::Decode(encoded.data(), 10, decoded));
}

TEST(AirPlay2, PlistRoundTrip)
{
    auto dict = Plist::Dict();
    dict.Set("bool", Plist::Bool(true));
    dict.Set("small", Plist::Integer(42));
    dict.Set("negative", Plist::Integer(-5));
    dict.Set("big", Plist::UInteger(0x18340405C4A00ULL));
    dict.Set("huge", Plist::UInteger(0xF7FE018E00E00000ULL));
    dict.Set("real", Plist::Real(-18.5));
    dict.Set("string", Plist::String("hello"));
    dict.Set("data", Plist::Data(Bytes{ 0, 1, 2, 255 }));

    auto arr = Plist::Array();
    auto inner = Plist::Dict();
    inner.Set("type", Plist::Integer(103));
    arr.Append(move(inner));
    arr.Append(Plist::String("x"));
    dict.Set("streams", move(arr));

    const auto binary = dict.ToBinary();
    ASSERT_GE(binary.size(), 40u);
    EXPECT_EQ(memcmp(binary.data(), "bplist00", 8), 0);

    Plist parsed;
    ASSERT_TRUE(Plist::FromBinary(binary.data(), binary.size(), parsed));
    ASSERT_TRUE(parsed.IsDict());
    EXPECT_TRUE(parsed.Get("bool").AsBool());
    EXPECT_EQ(parsed.Get("small").AsInt(), 42);
    EXPECT_EQ(parsed.Get("negative").AsInt(), -5);
    EXPECT_EQ(parsed.Get("big").AsUInt(), 0x18340405C4A00ULL);
    EXPECT_EQ(parsed.Get("huge").AsUInt(), 0xF7FE018E00E00000ULL);
    EXPECT_DOUBLE_EQ(parsed.Get("real").AsReal(), -18.5);
    EXPECT_EQ(parsed.Get("string").AsString(), "hello");
    EXPECT_EQ(parsed.Get("data").AsData(), (Bytes{ 0, 1, 2, 255 }));
    ASSERT_TRUE(parsed.Get("streams").IsArray());
    ASSERT_EQ(parsed.Get("streams").Size(), 2u);
    EXPECT_EQ(parsed.Get("streams")[0].Get("type").AsInt(), 103);
    EXPECT_EQ(parsed.Get("streams")[1].AsString(), "x");
    EXPECT_TRUE(parsed.Get("missing").IsNull());

    // garbage
    const string garbage = "bplist00garbage";
    EXPECT_FALSE(Plist::FromBinary(garbage, parsed));
}

TEST(AirPlay2, Srp)
{
    SrpServer server("Pair-Setup", "3939");
    SrpClient client("Pair-Setup", "3939");

    Bytes M1;
    ASSERT_TRUE(client.ProcessChallenge(server.Salt(), server.PublicKey(), M1));

    Bytes proof;
    ASSERT_TRUE(server.VerifyClient(client.PublicKey(), M1, proof));
    EXPECT_TRUE(client.VerifyServer(proof));
    EXPECT_EQ(server.SessionKey().size(), 64u);
    EXPECT_EQ(server.SessionKey(), client.SessionKey());

    // wrong password
    SrpServer server2("Pair-Setup", "3939");
    SrpClient wrong("Pair-Setup", "1234");
    ASSERT_TRUE(wrong.ProcessChallenge(server2.Salt(), server2.PublicKey(), M1));
    EXPECT_FALSE(server2.VerifyClient(wrong.PublicKey(), M1, proof));
}

TEST(AirPlay2, TransientPairSetupAndControlChannel)
{
    const auto identity = AirPlay2::Crypto::Ed25519Key::Generate();
    Pairing pairing(identity, "AA:BB:CC:DD:EE:FF");

    // M1
    Tlv8 m1;
    m1.Add(TlvType::Method, 0);
    m1.Add(TlvType::State, 1);
    m1.Add(TlvType::Flags, 0x10);
    auto data = m1.Encode();

    Bytes response;
    ASSERT_TRUE(pairing.HandlePairSetup(data.data(), data.size(), response));

    Tlv8 m2;
    ASSERT_TRUE(Tlv8::Decode(response.data(), response.size(), m2));
    uint8_t state = 0;
    ASSERT_TRUE(m2.GetByte(TlvType::State, state));
    EXPECT_EQ(state, 2);
    ASSERT_NE(m2.Get(TlvType::Salt), nullptr);
    ASSERT_NE(m2.Get(TlvType::PublicKey), nullptr);

    // M3
    SrpClient client("Pair-Setup", Pairing::SetupPin);
    Bytes proof;
    ASSERT_TRUE(client.ProcessChallenge(*m2.Get(TlvType::Salt), *m2.Get(TlvType::PublicKey), proof));

    Tlv8 m3;
    m3.Add(TlvType::State, 3);
    m3.Add(TlvType::PublicKey, client.PublicKey());
    m3.Add(TlvType::Proof, proof);
    data = m3.Encode();

    Bytes secret;
    EXPECT_FALSE(pairing.TakeEncryptionSecret(secret));
    ASSERT_TRUE(pairing.HandlePairSetup(data.data(), data.size(), response));

    Tlv8 m4;
    ASSERT_TRUE(Tlv8::Decode(response.data(), response.size(), m4));
    ASSERT_TRUE(m4.GetByte(TlvType::State, state));
    EXPECT_EQ(state, 4);
    EXPECT_EQ(m4.Get(TlvType::Error), nullptr);
    ASSERT_NE(m4.Get(TlvType::Proof), nullptr);
    EXPECT_TRUE(client.VerifyServer(*m4.Get(TlvType::Proof)));

    ASSERT_TRUE(pairing.TakeEncryptionSecret(secret));
    EXPECT_FALSE(pairing.TakeEncryptionSecret(secret));
    EXPECT_EQ(secret, client.SessionKey());

    // encrypted control channel (controller <-> accessory)
    auto controller = SecureChannel::ForControl(client.SessionKey(), false);
    auto accessory = SecureChannel::ForControl(secret, true);

    string request = "SETUP rtsp://1.2.3.4/123 RTSP/1.0\r\nCSeq: 3\r\n\r\n";
    request += string(3000, 'x'); // more than one frame

    string wire;
    controller.Encrypt(reinterpret_cast<const uint8_t*>(request.data()), request.size(), wire);
    const size_t frames = (request.size() + SecureChannel::MaxFrameSize - 1) / SecureChannel::MaxFrameSize;
    EXPECT_EQ(frames, 3u);
    EXPECT_EQ(wire.size(), request.size() + frames * (2 + 16));

    // feed in small pieces to test partial frames
    string plain;

    for (size_t i = 0; i < wire.size(); i += 7)
    {
        const size_t n = min<size_t>(7, wire.size() - i);
        ASSERT_TRUE(accessory.Decrypt(reinterpret_cast<const uint8_t*>(wire.data()) + i, n, plain));
    }
    EXPECT_FALSE(accessory.HasPartialFrame());
    EXPECT_EQ(plain, request);

    // and back
    const string reply = "RTSP/1.0 200 OK\r\nCSeq: 3\r\n\r\n";
    wire.clear();
    accessory.Encrypt(reinterpret_cast<const uint8_t*>(reply.data()), reply.size(), wire);
    plain.clear();
    ASSERT_TRUE(controller.Decrypt(reinterpret_cast<const uint8_t*>(wire.data()), wire.size(), plain));
    EXPECT_EQ(plain, reply);

    // tampering is detected
    wire.clear();
    accessory.Encrypt(reinterpret_cast<const uint8_t*>(reply.data()), reply.size(), wire);
    wire[5] ^= 1;
    plain.clear();
    EXPECT_FALSE(controller.Decrypt(reinterpret_cast<const uint8_t*>(wire.data()), wire.size(), plain));
}

TEST(AirPlay2, WrongPinIsRejected)
{
    Pairing pairing(AirPlay2::Crypto::Ed25519Key::Generate(), "AA:BB:CC:DD:EE:FF");

    Tlv8 m1;
    m1.Add(TlvType::Method, 0);
    m1.Add(TlvType::State, 1);
    auto data = m1.Encode();

    Bytes response;
    ASSERT_TRUE(pairing.HandlePairSetup(data.data(), data.size(), response));
    Tlv8 m2;
    ASSERT_TRUE(Tlv8::Decode(response.data(), response.size(), m2));

    SrpClient client("Pair-Setup", "0000");
    Bytes proof;
    ASSERT_TRUE(client.ProcessChallenge(*m2.Get(TlvType::Salt), *m2.Get(TlvType::PublicKey), proof));

    Tlv8 m3;
    m3.Add(TlvType::State, 3);
    m3.Add(TlvType::PublicKey, client.PublicKey());
    m3.Add(TlvType::Proof, proof);
    data = m3.Encode();
    ASSERT_TRUE(pairing.HandlePairSetup(data.data(), data.size(), response));

    Tlv8 m4;
    ASSERT_TRUE(Tlv8::Decode(response.data(), response.size(), m4));
    uint8_t error = 0;
    EXPECT_TRUE(m4.GetByte(TlvType::Error, error));
    EXPECT_EQ(error, static_cast<uint8_t>(TlvError::Authentication));

    Bytes secret;
    EXPECT_FALSE(pairing.TakeEncryptionSecret(secret));
}

TEST(AirPlay2, PairVerify)
{
    const auto identity = AirPlay2::Crypto::Ed25519Key::Generate();
    const string deviceId = "AA:BB:CC:DD:EE:FF";
    Pairing pairing(identity, deviceId);

    Bytes clientPriv, clientPub;
    AirPlay2::Crypto::X25519Generate(clientPriv, clientPub);

    Tlv8 m1;
    m1.Add(TlvType::State, 1);
    m1.Add(TlvType::PublicKey, clientPub);
    auto data = m1.Encode();

    Bytes response;
    ASSERT_TRUE(pairing.HandlePairVerify(data.data(), data.size(), response));

    Tlv8 m2;
    ASSERT_TRUE(Tlv8::Decode(response.data(), response.size(), m2));
    ASSERT_NE(m2.Get(TlvType::PublicKey), nullptr);
    ASSERT_NE(m2.Get(TlvType::EncryptedData), nullptr);

    const Bytes serverPub = *m2.Get(TlvType::PublicKey);
    const Bytes shared = AirPlay2::Crypto::X25519Shared(clientPriv, serverPub);
    const Bytes key = AirPlay2::Crypto::HkdfSha512(shared, "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info");

    const Bytes& enc = *m2.Get(TlvType::EncryptedData);
    ASSERT_GT(enc.size(), 16u);
    Bytes plain(enc.size() - 16);
    uint8_t nonce[12];
    AirPlay2::Crypto::MakeLabelNonce("PV-Msg02", nonce);
    ASSERT_TRUE(AirPlay2::Crypto::ChaChaDecrypt(key.data(), nonce, nullptr, 0, enc.data(), plain.size(), enc.data() + plain.size(), plain.data()));

    Tlv8 sub;
    ASSERT_TRUE(Tlv8::Decode(plain.data(), plain.size(), sub));
    ASSERT_NE(sub.Get(TlvType::Identifier), nullptr);
    EXPECT_EQ(*sub.Get(TlvType::Identifier), FromString(deviceId));
    ASSERT_NE(sub.Get(TlvType::Signature), nullptr);

    Bytes info = serverPub;
    const Bytes id = FromString(deviceId);
    info.insert(info.end(), id.begin(), id.end());
    info.insert(info.end(), clientPub.begin(), clientPub.end());
    EXPECT_TRUE(AirPlay2::Crypto::Ed25519Key::Verify(identity.PublicKey(), info, *sub.Get(TlvType::Signature)));

    // M3
    const auto controllerKey = AirPlay2::Crypto::Ed25519Key::Generate();
    Bytes info3 = clientPub;
    const Bytes controllerId = FromString("controller");
    info3.insert(info3.end(), controllerId.begin(), controllerId.end());
    info3.insert(info3.end(), serverPub.begin(), serverPub.end());

    Tlv8 sub3;
    sub3.Add(TlvType::Identifier, controllerId);
    sub3.Add(TlvType::Signature, controllerKey.Sign(info3));
    const auto plain3 = sub3.Encode();

    Bytes enc3(plain3.size() + 16);
    AirPlay2::Crypto::MakeLabelNonce("PV-Msg03", nonce);
    ASSERT_TRUE(AirPlay2::Crypto::ChaChaEncrypt(key.data(), nonce, nullptr, 0, plain3.data(), plain3.size(), enc3.data(), enc3.data() + plain3.size()));

    Tlv8 m3;
    m3.Add(TlvType::State, 3);
    m3.Add(TlvType::EncryptedData, enc3);
    data = m3.Encode();
    ASSERT_TRUE(pairing.HandlePairVerify(data.data(), data.size(), response));

    Tlv8 m4;
    ASSERT_TRUE(Tlv8::Decode(response.data(), response.size(), m4));
    uint8_t state = 0;
    ASSERT_TRUE(m4.GetByte(TlvType::State, state));
    EXPECT_EQ(state, 4);
    EXPECT_EQ(m4.Get(TlvType::Error), nullptr);

    Bytes secret;
    ASSERT_TRUE(pairing.TakeEncryptionSecret(secret));
    EXPECT_EQ(secret, shared);
}

TEST(AirPlay2, PtpMessageAndOffset)
{
    // Sync (two-step) followed by Follow_Up
    uint8_t sync[44]{};
    sync[0] = 0x00;     // Sync
    sync[1] = 0x02;     // version
    sync[6] = 0x02;     // flags: two-step
    for (int i = 0; i < 8; ++i)
    {
        sync[20 + i] = static_cast<uint8_t>(0x10 + i);
    }
    sync[30] = 0x12;
    sync[31] = 0x34;

    PtpMessage msg;
    ASSERT_TRUE(PtpMessage::Parse(sync, sizeof(sync), msg));
    EXPECT_EQ(msg.type, 0);
    EXPECT_TRUE(msg.twoStep);
    EXPECT_EQ(msg.sequenceId, 0x1234);
    EXPECT_EQ(msg.clockId, 0x1011121314151617ULL);

    uint8_t followUp[44];
    memcpy(followUp, sync, sizeof(followUp));
    followUp[0] = 0x08;     // Follow_Up
    followUp[6] = 0;
    // precise origin timestamp: 100 s + 500 ns
    followUp[39] = 100;
    followUp[43] = 0xf4;
    followUp[42] = 0x01;

    PtpMessage fu;
    ASSERT_TRUE(PtpMessage::Parse(followUp, sizeof(followUp), fu));
    EXPECT_EQ(fu.type, 8);
    EXPECT_EQ(fu.originNs, 100'000'000'500LL);

    PtpClock clock;
    const int64_t local = LocalTimeNs();
    clock.OnMessage(msg, local);
    clock.OnMessage(fu, local);

    int64_t offset = 0;
    ASSERT_TRUE(clock.GetOffset(msg.clockId, offset));
    EXPECT_EQ(offset, 100'000'000'500LL - local);
    ASSERT_TRUE(clock.GetOffset(0, offset));

    int64_t converted = 0;
    ASSERT_TRUE(clock.NetworkToLocal(msg.clockId, 100'000'000'500LL + 1000, converted));
    EXPECT_EQ(converted, local + 1000);

}

TEST(AirPlay2, DecryptAudioPacket)
{
    const Bytes key = AirPlay2::Crypto::RandomBytes(32);
    const Bytes payload = AirPlay2::Crypto::RandomBytes(100);

    // [4 seq][4 ts][4 ssrc][cipher][16 tag][8 nonce]
    Bytes packet(12 + payload.size() + 16 + 8);
    packet[0] = 0x80;
    packet[1] = 0x00;
    packet[2] = 0x00;
    packet[3] = 0x05;
    packet[4] = 0x00; packet[5] = 0x01; packet[6] = 0x02; packet[7] = 0x03;    // ts
    packet[8] = 0x16; packet[9] = 0x00; packet[10] = 0x00; packet[11] = 0x00;  // ssrc (AAC 44.1k)

    uint8_t nonce8[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t nonce12[12]{};
    memcpy(nonce12 + 4, nonce8, 8);

    ASSERT_TRUE(AirPlay2::Crypto::ChaChaEncrypt(key.data(), nonce12, packet.data() + 4, 8, payload.data(), payload.size(),
        packet.data() + 12, packet.data() + 12 + payload.size()));
    memcpy(packet.data() + packet.size() - 8, nonce8, 8);

    AudioFrame frame;
    ASSERT_TRUE(DecryptAudioPacket(packet.data(), packet.size(), key, frame));
    EXPECT_EQ(frame.timestamp, 0x00010203u);
    EXPECT_EQ(frame.ssrc, 0x16000000u);
    EXPECT_EQ(frame.seq, 0x5u);
    EXPECT_EQ(frame.payload, payload);

    AudioFormat format;
    ASSERT_TRUE(AudioFormatFromSsrc(frame.ssrc, format));
    EXPECT_EQ(format.codec, AudioCodec::AAC);
    EXPECT_EQ(format.sampleRate, 44100);

    packet[20] ^= 1;
    EXPECT_FALSE(DecryptAudioPacket(packet.data(), packet.size(), key, frame));
}

TEST(AirPlay2, AudioFormats)
{
    AudioFormat format;

    ASSERT_TRUE(AudioFormatFromAirPlayFormat(0x40000, format));
    EXPECT_EQ(format.codec, AudioCodec::ALAC);
    EXPECT_EQ(format.sampleRate, 44100);
    EXPECT_EQ(format.bitDepth, 16);

    ASSERT_TRUE(AudioFormatFromAirPlayFormat(0x200000, format));
    EXPECT_EQ(format.codec, AudioCodec::ALAC);
    EXPECT_EQ(format.sampleRate, 48000);
    EXPECT_EQ(format.bitDepth, 24);

    ASSERT_TRUE(AudioFormatFromAirPlayFormat(0x800000, format));
    EXPECT_EQ(format.codec, AudioCodec::AAC);
    EXPECT_EQ(format.sampleRate, 48000);

    EXPECT_FALSE(AudioFormatFromAirPlayFormat(0x1000000000ULL, format));
}

TEST(AirPlay2, DecoderAvailability)
{
    AudioFormat format;
    ASSERT_TRUE(AudioFormatFromAirPlayFormat(0x400000, format));

    auto decoder = Ap2Decoder::Create(format);

    if (Ap2Decoder::IsAvailable())
    {
        ASSERT_NE(decoder, nullptr);

        ASSERT_TRUE(AudioFormatFromAirPlayFormat(0x40000, format));
        EXPECT_NE(Ap2Decoder::Create(format), nullptr);
        ASSERT_TRUE(AudioFormatFromAirPlayFormat(0x200000, format));
        EXPECT_NE(Ap2Decoder::Create(format), nullptr);
    }
    else
    {
        EXPECT_EQ(decoder, nullptr);
    }
}

TEST(AirPlay2, DecodeUncompressedAlac)
{
    AudioFormat format;
    ASSERT_TRUE(AudioFormatFromSsrc(0x0000FACE, format));

    auto decoder = Ap2Decoder::Create(format);

    if (!Ap2Decoder::IsAvailable())
    {
        EXPECT_EQ(decoder, nullptr);
        return;
    }
    ASSERT_NE(decoder, nullptr);

    // ALAC frame in "escape" (uncompressed) mode: CPE element, 352 stereo 16 bit samples
    constexpr int frames = 352;
    vector<bool> bits;
    const auto put = [&bits](uint32_t v, int n)
        {
            for (int i = n - 1; i >= 0; --i)
            {
                bits.push_back((v >> i) & 1);
            }
        };
    put(1, 3);      // element type: CPE (stereo)
    put(0, 4);      // element instance tag
    put(0, 12);     // unused
    put(0, 1);      // no explicit frame size
    put(0, 2);      // no shifted bytes
    put(1, 1);      // not compressed

    for (int i = 0; i < frames; ++i)
    {
        put(static_cast<uint16_t>(i * 10), 16);
        put(static_cast<uint16_t>(-i * 10), 16);
    }
    put(7, 3);      // end element

    Bytes packet((bits.size() + 7) / 8, 0);

    for (size_t i = 0; i < bits.size(); ++i)
    {
        if (bits[i])
        {
            packet[i / 8] |= static_cast<uint8_t>(0x80 >> (i % 8));
        }
    }
    vector<int16_t> pcm;
    ASSERT_TRUE(decoder->Decode(packet.data(), packet.size(), pcm));
    ASSERT_EQ(pcm.size(), static_cast<size_t>(frames * Ap2Decoder::OutputChannels));

    for (int i = 0; i < frames; ++i)
    {
        EXPECT_EQ(pcm[i * 2], static_cast<int16_t>(i * 10));
        EXPECT_EQ(pcm[i * 2 + 1], static_cast<int16_t>(-i * 10));
    }
}

TEST(AirPlay2, ServiceInfoAndTxt)
{
    auto config = MakeShared<ValueCollection>();

    auto hwaddr = MakeShared<BlobStream>();
    const uint8_t mac[6] = { 0x00, 0x11, 0x22, 0xaa, 0xbb, 0xcc };
    hwaddr->Write(mac, 6, nullptr);
    VariantValue::Key("HWaddress").Set(config, hwaddr);
    VariantValue::Key("APname").Set(config, string("Test"));
    VariantValue::Key("EnableAirPlay2").Set(config, true);

    Service service(config, nullptr);
    EXPECT_EQ(service.DeviceId(), "00:11:22:AA:BB:CC");
    EXPECT_EQ(service.IsEnabled(), Service::IsSupported());

    // identity is persisted
    ASSERT_TRUE(VariantValue::Key("AirPlay2Key").Has(config));
    const auto pi = VariantValue::Key("AirPlay2Pi").Get<string>(config);
    EXPECT_EQ(pi.size(), 36u);
    {
        Service service2(config, nullptr);
        const auto txt1 = service.AirPlayTxtRecords();
        const auto txt2 = service2.AirPlayTxtRecords();
        EXPECT_EQ(txt1, txt2);
    }
    const auto txt = service.AirPlayTxtRecords();
    bool hasFeatures = false;

    for (const auto& record : txt)
    {
        if (record.first == "features")
        {
            hasFeatures = true;
            EXPECT_EQ(record.second, "0x405FCA00,0x18340");
        }
        if (record.first == "pk")
        {
            EXPECT_EQ(record.second.size(), 64u);
        }
    }
    EXPECT_TRUE(hasFeatures);

    // GET /info
    auto connection = service.CreateConnection();
    Request request;
    request.method = "GET";
    request.path = "/info";
    request.remoteAddr = "192.168.1.2";
    request.localAddr = "192.168.1.3";

    Response response;
    ASSERT_TRUE(service.Handle(*connection, request, response));
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.contentType, "application/x-apple-binary-plist");

    Plist info;
    ASSERT_TRUE(Plist::FromBinary(response.body, info));
    EXPECT_EQ(info.Get("deviceID").AsString(), "00:11:22:AA:BB:CC");
    EXPECT_EQ(info.Get("features").AsUInt(), service.Features());
    EXPECT_EQ(info.Get("name").AsString(), "Test");
    EXPECT_EQ(info.Get("pk").AsData().size(), 32u);
    EXPECT_FALSE(connection->IsAirPlay2());

    // AirPlay 1 requests are not handled
    request.method = "ANNOUNCE";
    request.path = "rtsp://192.168.1.3/123";
    EXPECT_FALSE(service.Handle(*connection, request, response));

    // AirPlay 2 password protection is not supported
    VariantValue::Key("HasPassword").Set(config, true);
    VariantValue::Key("Password").Set(config, string("secret"));
    EXPECT_FALSE(service.IsEnabled());
}

// Manual end-to-end helper: runs a real RaopServer with AirPlay 2 enabled on port 5000+
// for AP2_SERVE_SECONDS seconds, so that external clients (e.g. a test script or an iPhone)
// can connect. Run with: ShairportQtTest --gtest_also_run_disabled_tests --gtest_filter=*Serve*
TEST(AirPlay2, DISABLED_ServeForManualTesting)
{
    spdlog::set_level(spdlog::level::debug);

    auto config = MakeShared<ValueCollection>();

    VariantValue::Key("APname").Set(config, "ShairportQt-AP2-Test"s);
    VariantValue::Key("HWaddress").Set(config, vector<uint8_t>{ 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 });
    VariantValue::Key("StartFill").Set(config, 500);
    VariantValue::Key("EnableAirPlay2").Set(config, true);

    ASSERT_TRUE(Service::IsSupported());

    const char* env = getenv("AP2_SERVE_SECONDS");
    const int seconds = env ? atoi(env) : 30;

    auto server = make_unique<RaopServer>(config, MakeShared<DnsSD>(false));
    this_thread::sleep_for(chrono::seconds(seconds));
    server.reset();
}
