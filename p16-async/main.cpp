#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <future>
using namespace std;
using namespace std::chrono;
typedef unsigned long long ull;

ull findOdd(const ull start, const ull end) {
    ull oddSum = 0;
    cout << "ThreadID of findOdd " << std::this_thread::get_id() << endl;
    for (ull i = start; i <= end; ++i) {
        if (i & 1) {
            oddSum += i;
        }
    }
    return oddSum;
}

int main() {
    ull start = 0, end = 1900000000;
    cout << "ThreadID " << std::this_thread::get_id() << endl;
    cout << "Thread created if policy is std::launch::async!!" << endl;
    // std::launch::async：任务是以另一个线程执行的
    // std::launch::deferred：任务是在同一个线程中执行的
    std::future<ull> oddSum = std::async(std::launch::deferred, findOdd, start, end);

    cout << "Waiting for result!!" << endl;
    cout << "OddSum: " << oddSum.get() << endl;
    cout << "Completed!!" << endl;

    return 0;
}


