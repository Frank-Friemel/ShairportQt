#include <gtest/gtest.h>
#include <Condition.h>
#include <mutex>
#include <thread>
#include "libutils.h"

using namespace std;
using namespace string_literals;
using namespace literals;

TEST(Queuetest, ConditionTiming)
{
	Condition condition;

	mutex mtx;
	unique_lock<mutex> guard(mtx);

	// start a helper thread
	thread helperThread([&]()
		{
			this_thread::sleep_for(300ms);

			// notify a condition change
			condition.NotifyAll();
		});
	this_thread::sleep_for(10ms);

	// count how often the condition check will occur
	int conditionChangedCounter = 0;

	const auto start = chrono::steady_clock::now();
	EXPECT_EQ(condition.WaitAndLock(guard, [&conditionChangedCounter] { conditionChangedCounter++; return false; }, 1000), cv_status::timeout);
	const auto stop = chrono::steady_clock::now();

	// wait for the end of helper thread
	helperThread.join();

	// we expect the condition check occured two times
	EXPECT_EQ(conditionChangedCounter, 2);

	// the absolute waiting time is expect ~1000ms (although the wait had been interrupted one time)
	EXPECT_LT(abs((chrono::duration_cast<chrono::milliseconds>(stop - start) - 1000ms).count()), 50);
}

TEST(Queuetest, Scope)
{
	int i = 0;

	{
		const ScopeContext sc([&i]() { ++i; });
	}
	EXPECT_EQ(i, 1);
}