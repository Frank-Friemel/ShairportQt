#include <gtest/gtest.h>
#include <Trim.h>
#include <list>
#include "libutils.h"
#include <vector>
#include <LayerCake.h>

using namespace std;
using namespace literals;

TEST(Trim, Basic)
{
    {
        auto t = "   \t\r\n Test \t\r\n "s;
        Trim(t, " \t\r\n"s);
        EXPECT_EQ(t, "Test"s);
    }
    {
        auto t = L"   \t\r\n Test \t\r\n "s;
        Trim(t, L" \t\r\n"s);
        EXPECT_EQ(t, L"Test"s);
    }
}

TEST(Trim, Unicode)
{
    const auto unicodeString = L"\x05d3\x05d5\x05e0\x05d3\x05d0\x05e8\x05df\x05de\x05e2"s;

    const auto utfStringA = CW2AEX(unicodeString);
    const auto utfStringW = CA2WEX(utfStringA);

    EXPECT_EQ(utfStringW, unicodeString);
}

TEST(Trim, ErrorMessage)
{
    const auto msg = ErrorToString(ERROR_ACCESS_DENIED);
    EXPECT_FALSE(msg.empty());
}

TEST(Trim, ToHex)
{
    {
        const vector<unsigned char> test{ 0, 127, 128, 255 };

        const auto strL = EncodeToHex(test);
        const auto strU = EncodeToHex(test, true);

        EXPECT_EQ(strL, "007f80ff"s);
        EXPECT_EQ(strU, "007F80FF"s);
    }
    {
        const list<unsigned char> test{ 0, 127, 128, 255 };

        const auto strL = EncodeToHex(test);
        const auto strU = EncodeToHex(test, true);

        EXPECT_EQ(strL, "007f80ff"s);
        EXPECT_EQ(strU, "007F80FF"s);
    }
    {
        const string test{ "ABCabc"s };

        const auto str = EncodeToHex(test);

        EXPECT_EQ(str, "414243616263"s);
    }
}

TEST(Trim, FromHex)
{
    {
        const auto v = DecodeFromHex(""s);

        ASSERT_EQ(v.size(), static_cast<size_t>(0));
    }
    {
        const auto v = DecodeFromHex("A"s);

        ASSERT_EQ(v.size(), static_cast<size_t>(1));
        ASSERT_EQ(v[0], static_cast<unsigned char>(10));
    }
    {
        const auto v = DecodeFromHex("a"s);

        ASSERT_EQ(v.size(), static_cast<size_t>(1));
        ASSERT_EQ(v[0], static_cast<unsigned char>(10));
    }
    {
        const auto v = DecodeFromHex("9"s);

        ASSERT_EQ(v.size(), static_cast<size_t>(1));
        ASSERT_EQ(v[0], static_cast<unsigned char>(9));
    }
    {
        const auto v = DecodeFromHex("0"s);

        ASSERT_EQ(v.size(), static_cast<size_t>(1));
        ASSERT_EQ(v[0], static_cast<unsigned char>(0));
    }
    {
        const auto v = DecodeFromHex("FF"s);

        ASSERT_EQ(v.size(), static_cast<size_t>(1));
        ASSERT_EQ(v[0], static_cast<unsigned char>(255));
    }
    {
        const auto v = DecodeFromHex("ff"s);

        ASSERT_EQ(v.size(), static_cast<size_t>(1));
        ASSERT_EQ(v[0], static_cast<unsigned char>(255));
    }
    {
        string hex;

        vector<unsigned char> vIn;
        vIn.resize(256);

        {
            for (int i = 0; i < 256; ++i)
            {
                vIn[i] = (unsigned char)i;
            }
            hex = EncodeToHex(vIn);
        }
        const auto vOut = DecodeFromHex(hex);

        EXPECT_EQ(vIn, vOut);
    }
    {
        const uint64_t n = HexToInteger("iTunes_Ctrl_50CF"s);

        EXPECT_EQ(n, 20687);
    }
    {
        const uint64_t n = HexToInteger("iTunes_Ctrl_50df"s);

        EXPECT_EQ(n, 20703);
    }
}

TEST(Trim, ParseRegEx)
{
    const string rtpInfo = "trash=10;seq=4161;rtptime=3439233566"s;

    int result = 0;

    ParseRegEx(rtpInfo, "seq=\\d+"s, [&result](const string s) -> bool
        {
            result = atoi(s.c_str() + 4);
            return false;
        });

    EXPECT_EQ(result, 4161);
}

TEST(Trim, NTP)
{
    {
        const uint64_t ntp = ToNTP(chrono::system_clock::time_point());
        const auto str = ToISO8601String(FromNTP(ntp), true);

        EXPECT_EQ(str, "1970-01-01T00:00:00+0000"s);
    }
    {
        const auto str = ToISO8601String(FromNTP(0xffffffff83AA7E80), true);

        EXPECT_EQ(str, "1970-01-01T00:00:00.999999+0000"s);
    }
    {
        const auto str = ToISO8601String(FromNTP(0xffffffffffffffff), true);

        EXPECT_EQ(str, "2036-02-07T06:28:15.999999+0000"s);
    }
    {
        const auto t = time(NULL);
        const auto now = chrono::system_clock::from_time_t(t);

        const uint64_t ntp = ToNTP(now + 500ms);
        const auto strNTP = ToISO8601String(FromNTP(ntp));

        printf("time from ntp       : %s\n", strNTP.c_str());

        const auto strTP = ToISO8601String(now + 500ms);
        printf("time from time_point: %s\n", strTP.c_str());
        EXPECT_EQ(strNTP, strTP);
    }
    {
        const auto t = time(NULL);
        const auto now = chrono::system_clock::from_time_t(t);

        const uint64_t ntp = ToNTP(now + 999999us);
        const auto strNTP = ToISO8601String(FromNTP(ntp));

        printf("time from ntp       : %s\n", strNTP.c_str());

        const auto strTP = ToISO8601String(now + 999999us);
        printf("time from time_point: %s\n", strTP.c_str());
        EXPECT_EQ(strNTP, strTP);
    }
}