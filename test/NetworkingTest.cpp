#include <gtest/gtest.h>
#include <Networking.h>
#include <chrono>

using namespace std;
using namespace literals;

TEST(NetworkingTest, Init)
{
	EXPECT_NO_THROW(Networking::Init());
}

TEST(NetworkingTest, CreateAndDestroy)
{
	EXPECT_NO_THROW(Networking::Init());

	const int sd = Networking::CreateSocket();

	EXPECT_TRUE(sd >= 0);

	Networking::DestroySocket(sd);
}

TEST(NetworkingTest, CanSetToNonBlockingAndBack)
{
	EXPECT_NO_THROW(Networking::Init());

	const int sd = Networking::CreateSocket();

	EXPECT_TRUE(Networking::SetSocketBlockingEnabled(sd, false));
	EXPECT_TRUE(Networking::SetSocketBlockingEnabled(sd, true));

	Networking::DestroySocket(sd);
}

TEST(NetworkingTest, CanWaitForIncomingData)
{
#ifdef _WIN32
	EXPECT_NO_THROW(Networking::Init());

	const int sd = Networking::CreateSocket();
	EXPECT_TRUE(Networking::SetSocketBlockingEnabled(sd, false));

	unsigned char buf[4096];
	const int read = Networking::Read(sd, buf, 4096);
	EXPECT_LE(read, 0);

	const auto start = chrono::steady_clock::now();
	const int w = Networking::WaitForIncomingData(sd, 500);
	const auto stop = chrono::steady_clock::now();

	const auto diffMS = chrono::duration_cast<chrono::milliseconds>(stop - start).count();

	EXPECT_EQ(w, 0);
	EXPECT_LT(abs(500ll - diffMS), 50ll);

	Networking::DestroySocket(sd);
#endif
}