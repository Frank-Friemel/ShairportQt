#include <gtest/gtest.h>
#include "RaopEndpoint.h"
#include <list>

using namespace std;
using namespace string_literals;

class RtpRequestHandler
    : public IRtpRequestHandler
{
public:
	void OnRequest(RtpEndpoint*, std::unique_ptr<RtpPacket>&& packet) override
    {
        packetList.emplace_back(move(packet));
    }

public:
    list<std::unique_ptr<RtpPacket>> packetList;
};

static RtpRequestHandler  handler;

TEST(EndpointTest, CreateSocketv4Udp)
{
    EXPECT_NO_THROW(RtpEndpoint(&handler, "127.0.0.1"s));
}

TEST(EndpointTest, CreateSocketv6Udp)
{
    EXPECT_NO_THROW(RtpEndpoint(&handler, "::1"s));
}

TEST(EndpointTest, Sendv4Udp)
{
    handler.packetList.clear();

    RtpEndpoint endpoint(&handler, "127.0.0.1"s);
    EXPECT_NE(0, endpoint.GetPort());

    EXPECT_TRUE(endpoint.SendTo("hello", 5, endpoint.GetPort()));
    this_thread::sleep_for(500ms);

    ASSERT_EQ(static_cast<size_t>(1), handler.packetList.size());
    EXPECT_EQ(static_cast<size_t>(5), (*handler.packetList.begin())->size());
    EXPECT_EQ(0, memcmp("hello", (*handler.packetList.begin())->data(), 5));
}

TEST(EndpointTest, Sendv6Udp)
{
    handler.packetList.clear();
    RtpEndpoint endpoint(&handler, "::1"s);

    EXPECT_NE(0, endpoint.GetPort());

    EXPECT_TRUE(endpoint.SendTo("hello", 5, endpoint.GetPort()));
    this_thread::sleep_for(500ms);

    ASSERT_EQ(static_cast<size_t>(1), handler.packetList.size());
    EXPECT_EQ(static_cast<size_t>(5), (*handler.packetList.begin())->size());
    EXPECT_EQ(0, memcmp("hello", (*handler.packetList.begin())->data(), 5));
}