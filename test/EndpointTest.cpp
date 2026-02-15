#include <gtest/gtest.h>
#include "RaopEndpoint.h"
#include <list>
#include <future>
#include <thread>

using namespace std;
using namespace literals;

TEST(EndpointTest, CreateSocketv4Udp)
{
    RtpRequestHandler handler;
    {
        EXPECT_NO_THROW(RtpEndpoint(&handler, "127.0.0.1"s));
    }
}

TEST(EndpointTest, CreateSocketv6Udp)
{
    RtpRequestHandler handler;
    {
        EXPECT_NO_THROW(RtpEndpoint(&handler, "::1"s));
    }
}

TEST(EndpointTest, Sendv4Udp)
{
    RtpRequestHandler handler;
    {
        RtpEndpoint endpoint(&handler, "127.0.0.1"s);
        EXPECT_TRUE(endpoint.IsV4());
        EXPECT_NE(0, endpoint.GetPort());

        EXPECT_TRUE(endpoint.SendTo("hello", 5, endpoint.GetPort()));
        this_thread::sleep_for(500ms);

        ASSERT_EQ(static_cast<size_t>(1), handler.packetList.size());
        EXPECT_EQ(static_cast<size_t>(5), (*handler.packetList.begin())->size());
        EXPECT_EQ(0, memcmp("hello", (*handler.packetList.begin())->data(), 5));
    }
}

TEST(EndpointTest, Sendv4UdpToPeerPort)
{
    RtpRequestHandler handler;
    {
        RtpEndpoint endpointReceiver(&handler, "127.0.0.1"s);
        EXPECT_TRUE(endpointReceiver.IsV4());
        EXPECT_NE(0, endpointReceiver.GetPort());

        RtpEndpoint endpointSender(&handler, "127.0.0.1"s, endpointReceiver.GetPort());
        EXPECT_TRUE(endpointSender.IsV4());
        EXPECT_NE(0, endpointSender.GetPort());

        EXPECT_NE(endpointSender.GetPort(), endpointReceiver.GetPort());

        EXPECT_TRUE(endpointSender.SendTo("hello", 5, endpointReceiver.GetPort()));
        this_thread::sleep_for(500ms);

        ASSERT_EQ(static_cast<size_t>(1), handler.packetList.size());
        EXPECT_EQ(static_cast<size_t>(5), (*handler.packetList.begin())->size());
        EXPECT_EQ(0, memcmp("hello", (*handler.packetList.begin())->data(), 5));
    }
}

TEST(EndpointTest, Sendv4UdpToPeerPortFromWorkerThread)
{
    auto pr = make_shared<promise<bool>>();
    auto prResult = pr->get_future();

    RtpRequestHandler handler(move(pr));
    {
        RtpEndpoint endpointReceiver(&handler, "127.0.0.1"s);
        EXPECT_TRUE(endpointReceiver.IsV4());
        EXPECT_NE(0, endpointReceiver.GetPort());

        RtpEndpoint endpointSender(&handler, "127.0.0.1"s, endpointReceiver.GetPort());
        EXPECT_TRUE(endpointSender.IsV4());
        EXPECT_NE(0, endpointSender.GetPort());

        EXPECT_NE(endpointSender.GetPort(), endpointReceiver.GetPort());

        {
            thread t([&endpointSender, &endpointReceiver]()
                {
                    if (!endpointSender.SendTo("hello", 5, endpointReceiver.GetPort()))
                    {
                        throw runtime_error("failed to send hello");
                    }
                });
            ASSERT_TRUE(t.joinable());

            ASSERT_EQ(prResult.wait_for(500ms), future_status::ready);
            EXPECT_TRUE(prResult.get());

            EXPECT_NO_THROW(t.join());
        }
        ASSERT_EQ(static_cast<size_t>(1), handler.packetList.size());
        EXPECT_EQ(static_cast<size_t>(5), (*handler.packetList.begin())->size());
        EXPECT_EQ(0, memcmp("hello", (*handler.packetList.begin())->data(), 5));
    }
}

TEST(EndpointTest, Sendv6Udp)
{
    RtpRequestHandler handler;
    {
        RtpEndpoint endpoint(&handler, "::1"s);

        EXPECT_FALSE(endpoint.IsV4());
        EXPECT_NE(0, endpoint.GetPort());

        EXPECT_TRUE(endpoint.SendTo("hello", 5, endpoint.GetPort()));
        this_thread::sleep_for(500ms);

        ASSERT_EQ(static_cast<size_t>(1), handler.packetList.size());
        EXPECT_EQ(static_cast<size_t>(5), (*handler.packetList.begin())->size());
        EXPECT_EQ(0, memcmp("hello", (*handler.packetList.begin())->data(), 5));
    }
}

TEST(EndpointTest, Sendv6UdpToPeerPort)
{
    RtpRequestHandler handler;
    {
        RtpEndpoint endpointReceiver(&handler, "::1"s);
        EXPECT_FALSE(endpointReceiver.IsV4());
        EXPECT_NE(0, endpointReceiver.GetPort());

        RtpEndpoint endpointSender(&handler, "::1"s, endpointReceiver.GetPort());
        EXPECT_FALSE(endpointSender.IsV4());
        EXPECT_NE(0, endpointSender.GetPort());

        EXPECT_NE(endpointSender.GetPort(), endpointReceiver.GetPort());

        EXPECT_TRUE(endpointSender.SendTo("hello", 5, endpointReceiver.GetPort()));
        this_thread::sleep_for(500ms);

        ASSERT_EQ(static_cast<size_t>(1), handler.packetList.size());
        EXPECT_EQ(static_cast<size_t>(5), (*handler.packetList.begin())->size());
        EXPECT_EQ(0, memcmp("hello", (*handler.packetList.begin())->data(), 5));
    }
}

TEST(EndpointTest, Sendv6UdpToPeerPortFromWorkerThread)
{
    auto pr = make_shared<promise<bool>>();
    auto prResult = pr->get_future();

    RtpRequestHandler handler(move(pr));
    {
        RtpEndpoint endpointReceiver(&handler, "::1"s);
        EXPECT_FALSE(endpointReceiver.IsV4());
        EXPECT_NE(0, endpointReceiver.GetPort());

        RtpEndpoint endpointSender(&handler, "::1"s, endpointReceiver.GetPort());
        EXPECT_FALSE(endpointSender.IsV4());
        EXPECT_NE(0, endpointSender.GetPort());

        EXPECT_NE(endpointSender.GetPort(), endpointReceiver.GetPort());

        {
            thread t([&endpointSender, &endpointReceiver]()
                {
                    if (!endpointSender.SendTo("hello", 5, endpointReceiver.GetPort()))
                    {
                        throw runtime_error("failed to send hello");
                    }
                });
            ASSERT_TRUE(t.joinable());

            ASSERT_EQ(prResult.wait_for(500ms), future_status::ready);
            EXPECT_TRUE(prResult.get());

            EXPECT_NO_THROW(t.join());
        }
        ASSERT_EQ(static_cast<size_t>(1), handler.packetList.size());
        EXPECT_EQ(static_cast<size_t>(5), (*handler.packetList.begin())->size());
        EXPECT_EQ(0, memcmp("hello", (*handler.packetList.begin())->data(), 5));
    }
}