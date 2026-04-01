#include <gtest/gtest.h>
#include <vector>
#include <libutils.h>
#include <LayerCake.h>

using namespace std;
using namespace literals;

TEST(Values, RandomValues)
{
	srand((unsigned int)time(nullptr));

	uint32_t dice[6]{};

	for (int i = 0; i < 6000; ++i)
	{
		++dice[CreateRand(5)];
	}
	for (int i = 0; i < 6; ++i)
	{
		EXPECT_GT(dice[i], 0);
	}
	for (int i = 0; i < 6; ++i)
	{
		EXPECT_GT(dice[i], 200);
	}
}

TEST(Values, ValuesFromDouble)
{
	{
		int64_t v;

		v = VariantValue::Get<int64_t>(Variant(L"1.5"));
		EXPECT_EQ(v, int64_t(2));

		v = VariantValue::Get<int64_t>(Variant(L"1.2"));
		EXPECT_EQ(v, int64_t(1));

		v = VariantValue::Get<int64_t>(Variant(L"-1.5"));
		EXPECT_EQ(v, int64_t(-2));

		v = VariantValue::Get<int64_t>(Variant(L"-1.2"));
		EXPECT_EQ(v, int64_t(-1));
	}
	{
		int32_t v;

		v = VariantValue::Get<int32_t>(Variant(L"1.5"));
		EXPECT_EQ(v, int32_t(2));

		v = VariantValue::Get<int32_t>(Variant(L"1.2"));
		EXPECT_EQ(v, int32_t(1));

		v = VariantValue::Get<int32_t>(Variant(L"-1.5"));
		EXPECT_EQ(v, int32_t(-2));

		v = VariantValue::Get<int32_t>(Variant(L"-1.2"));
		EXPECT_EQ(v, int32_t(-1));
	}
	{
		int16_t v;

		v = VariantValue::Get<int16_t>(Variant(L"1.5"));
		EXPECT_EQ(v, int16_t(2));

		v = VariantValue::Get<int16_t>(Variant(L"1.2"));
		EXPECT_EQ(v, int16_t(1));

		v = VariantValue::Get<int16_t>(Variant(L"-1.5"));
		EXPECT_EQ(v, int16_t(-2));

		v = VariantValue::Get<int16_t>(Variant(L"-1.2"));
		EXPECT_EQ(v, int16_t(-1));
	}
	{
		int8_t v;

		v = VariantValue::Get<int8_t>(Variant(L"1.5"));
		EXPECT_EQ(v, int8_t(2));

		v = VariantValue::Get<int8_t>(Variant(L"1.2"));
		EXPECT_EQ(v, int8_t(1));

		v = VariantValue::Get<int8_t>(Variant(L"-1.5"));
		EXPECT_EQ(v, int8_t(-2));

		v = VariantValue::Get<int8_t>(Variant(L"-1.2"));
		EXPECT_EQ(v, int8_t(-1));
	}
	{
		int32_t v;

		v = VariantValue::Get<int32_t>(Variant(1.5));
		EXPECT_EQ(v, int32_t(2));

		v = VariantValue::Get<int32_t>(Variant(1.2));
		EXPECT_EQ(v, int32_t(1));

		v = VariantValue::Get<int32_t>(Variant(-1.5));
		EXPECT_EQ(v, int32_t(-2));

		v = VariantValue::Get<int32_t>(Variant(-1.2));
		EXPECT_EQ(v, int32_t(-1));
	}
	{
		string v;

		v = VariantValue::Get<string>(Variant(1.5));
		EXPECT_EQ(v.substr(0, 3), "1.5"s);

		v = VariantValue::Get<string>(Variant(1.2));
		EXPECT_EQ(v.substr(0, 3), "1.2"s);

		v = VariantValue::Get<string>(Variant(-1.5));
		EXPECT_EQ(v.substr(0, 4), "-1.5"s);

		v = VariantValue::Get<string>(Variant(-1.2));
		EXPECT_EQ(v.substr(0, 4), "-1.2"s);
	}
}

TEST(Values, ValuesToDouble)
{
	double v;

	v = VariantValue::Get<double>(Variant("1.5"));
	EXPECT_EQ(v, 1.5);

	v = VariantValue::Get<double>(Variant(true));
	EXPECT_EQ(v, 1.);

	v = VariantValue::Get<double>(Variant(false));
	EXPECT_EQ(v, 0.);

	v = VariantValue::Get<double>(Variant(int8_t(127)));
	EXPECT_EQ(v, 127.);

	v = VariantValue::Get<double>(Variant(uint8_t(255)));
	EXPECT_EQ(v, 255.);

	v = VariantValue::Get<double>(Variant(int16_t(32767)));
	EXPECT_EQ(v, 32767.);

	v = VariantValue::Get<double>(Variant(uint16_t(65535)));
	EXPECT_EQ(v, 65535.);

	v = VariantValue::Get<double>(Variant(int32_t(2147483647)));
	EXPECT_EQ(v, 2147483647.);

	v = VariantValue::Get<double>(Variant(uint32_t(4294967295)));
	EXPECT_EQ(v, 4294967295.);

	v = VariantValue::Get<double>(Variant(int64_t(9223372036854775807)));
	EXPECT_EQ(v, 9223372036854775807.);

	v = VariantValue::Get<double>(Variant(uint64_t(18446744073709551615)));
	EXPECT_EQ(v, 18446744073709551615.);
}