#include <gtest/gtest.h>


int main(int argc, char** argv)
{
	// Initialize GoogleTest
	::testing::InitGoogleTest(&argc, argv);

	// Execute the tests
	return RUN_ALL_TESTS();
}