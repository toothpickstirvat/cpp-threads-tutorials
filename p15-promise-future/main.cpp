#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <future>
using namespace std;
using namespace std::chrono;
typedef unsigned long long ull;

void findOdd(std::promise<ull>&& oddSumPromise, const ull start, const ull end) {
    ull oddSum = 0;
    for (ull i = start; i <= end; ++i) {
        if (i & 1) {
            oddSum += i;
        }
    }
    oddSumPromise.set_value(oddSum);
}

int main() {
    ull start = 0, end = 1900000000;

    // 创建promise对象
    std::promise<ull> oddSum; // 奇数和
    // 用promise创建future对象
    std::future<ull> oddSumFuture = oddSum.get_future();

    cout << "Thread created!!" << endl;
    std::thread t1(findOdd, std::move(oddSum), start, end);

    cout << "Waiting for result!!" << endl;
    cout << "oddSum: " << oddSumFuture.get() << endl;
    cout << "Completed!!" << endl;

    t1.join();

    return 0;
}


