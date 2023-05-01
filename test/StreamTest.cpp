#include <gtest/gtest.h>
#include "LayerCake.h"

using namespace std;
using namespace string_literals;

TEST(StreamTest, SupportsQueryInterface)
{
	SharedPtr<IUnknown> pUnk;
	auto stream = MakeShared<BlobStream>();

	const HRESULT hr = stream->QueryInterface(IID_IUnknown, (void**)&pUnk.p);
	EXPECT_EQ(S_OK, hr);
	EXPECT_EQ(1, stream.Clear());
	EXPECT_EQ(0, pUnk.Clear());
}

TEST(StreamTest, VerifyDefault)
{
	const auto stream = MakeShared<BlobStream>();

	EXPECT_EQ(0, stream->GetSize());
}

TEST(StreamTest, VerifyGivenSizeZeroIsOk)
{
	const auto stream = MakeShared<BlobStream>(0);

	EXPECT_EQ(0, stream->GetSize());
}

TEST(StreamTest, VerifyGivenSize)
{
	size_t size = 4096;

	const auto stream = MakeShared<BlobStream>(size);

	EXPECT_EQ(size, stream->GetSize());

	char buf[4096] = { 0x7f };

	ULONG read = 1;

	EXPECT_EQ(S_OK, stream->Read(buf, static_cast<ULONG>(size), &read));
	EXPECT_EQ(size, static_cast<size_t>(read));

	const string test(4096, '\0');
	EXPECT_EQ(test, string(buf, static_cast<size_t>(read)));
}

TEST(StreamTest, VerifyGivenData)
{
	const string test = "test"s;

	const auto stream = MakeShared<BlobStream>(test.size(), test.data());

	EXPECT_EQ(test.size(), stream->GetSize());

	char buf[256] = { 0 };
	
	ULONG read = 1;

	EXPECT_EQ(S_OK, stream->Read(buf, static_cast<ULONG>(test.size()), &read));
	EXPECT_EQ(test.size(), static_cast<size_t>(read));
	EXPECT_EQ(test, string(buf, static_cast<size_t>(read)));
}

TEST(StreamTest, PipeMode)
{
	auto stream = MakeShared<BlobStream>();

	stream->SetMode(BlobStream::Mode::pipeOpen);

	const auto as = async(launch::async, [&]()
	{
		this_thread::sleep_for(500ms);
		ULONG written = 0;
		ASSERT_TRUE(SUCCEEDED(stream->Write("hello", 5, &written)));
		EXPECT_EQ(written, 5);
	});

	char buf[256];
	ULONG read = 0;
	ASSERT_EQ(S_OK, stream->Read(buf, 256, &read));
	EXPECT_EQ(read, 5);
	EXPECT_EQ(memcmp(buf, "hello", 5), 0);

	stream->SetMode(BlobStream::Mode::pipeClosed);
	EXPECT_TRUE(FAILED(stream->Read(buf, 256, &read)));
}

enum class StreamType
{
	typeCreateStreamOnHGlobal,
	typeStreamOnMemory
};


class StreamTest
	: public ::testing::TestWithParam<StreamType>
{
protected:
	void SetUp() override
	{
		switch(GetParam())
		{
			case StreamType::typeCreateStreamOnHGlobal:
			{
				CreateStreamOnHGlobal(NULL, TRUE, &streamTest.p);
			}
			break;

			case StreamType::typeStreamOnMemory:
			{
				streamTest = MakeShared<BlobStream>();
			}
			break;
		}
	}

public:
	SharedPtr<IStream> streamTest;
};

INSTANTIATE_TEST_SUITE_P(Streams, StreamTest,
	::testing::Values(
		StreamType::typeCreateStreamOnHGlobal,
		StreamType::typeStreamOnMemory
		));

TEST_P(StreamTest, VerifyGivenStream)
{
	EXPECT_TRUE(SUCCEEDED(streamTest->Write("1234", 4, nullptr)));
	
	SharedPtr<IStream> stream;
	EXPECT_EQ(S_OK, streamTest->Clone(&stream.p));
	EXPECT_EQ(0, streamTest.Clear());
	EXPECT_FALSE(streamTest.IsValid());

	EXPECT_EQ(static_cast<size_t>(4), BlobStream::GetSize(stream));

	EXPECT_EQ(S_OK, stream->Seek({}, SEEK_SET, nullptr));
	EXPECT_EQ(0, BlobStream::Tell(stream));

	char readbuf[] = { '\0', '\0', '\0', '\0', '\0' };
	ULONG read = 0;
	EXPECT_EQ(S_OK, stream->Read(readbuf, 4, &read));

	EXPECT_EQ(4, static_cast<size_t>(read));
	EXPECT_EQ(readbuf[0], '1');
	EXPECT_EQ(readbuf[1], '2');
	EXPECT_EQ(readbuf[2], '3');
	EXPECT_EQ(readbuf[3], '4');

	EXPECT_EQ(4, BlobStream::Tell(stream));

	EXPECT_EQ(S_OK, stream->Seek({}, SEEK_END, nullptr));

	ULONG written = 0;
	EXPECT_TRUE(SUCCEEDED(stream->Write("56789", 5, &written)));
	EXPECT_EQ(5, written);
	EXPECT_EQ(static_cast<size_t>(9), BlobStream::GetSize(stream));

	ULARGE_INTEGER pos;
	EXPECT_EQ(S_OK, stream->Seek(ToLargeInteger(-5), SEEK_END, &pos));
	EXPECT_EQ(4ull, pos.QuadPart);
	EXPECT_EQ(S_OK, stream->Read(readbuf, 5, &read));

	EXPECT_EQ(5, static_cast<size_t>(read));
	EXPECT_EQ(readbuf[0], '5');
	EXPECT_EQ(readbuf[1], '6');
	EXPECT_EQ(readbuf[2], '7');
	EXPECT_EQ(readbuf[3], '8');
	EXPECT_EQ(readbuf[4], '9');
	
	EXPECT_EQ(9, BlobStream::Tell(stream));
	EXPECT_EQ(0, stream.Clear());
}

