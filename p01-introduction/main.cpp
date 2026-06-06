#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
using namespace std;
using namespace std::chrono;
typedef unsigned long long ull;

ull OddSum = 0;
ull EvenSum = 0;

void findEven(const ull start, const ull end)
{
    ull sum = 0;
    for (ull i = start; i <= end; ++i)
    {
        if ((i & 1) == 0)
        {
            sum += i;
        }
    }
    EvenSum = sum;
}

void findOdd(const ull start, const ull end)
{
    ull sum = 0;
    for (ull i = start; i <= end; ++i)
    {
        if ((i & 1) == 1)
        {
            sum += i;
        }
    }
    OddSum = sum;
}


int main()
{
    constexpr ull start = 0;
    constexpr ull end = 1900000000;
    const auto startTime = high_resolution_clock::now();
    std::thread t1(findOdd, start, end);
    std::thread t2(findEven, start, end);
    t1.join();
    t2.join();

    // findOdd(start, end);
    // findEven(start, end);
    const auto stopTime = high_resolution_clock::now();
    const auto duration = duration_cast<microseconds>(stopTime - startTime);
    cout << "OddSum: " << OddSum << endl;
    cout << "EvenSum: " << EvenSum << endl;
    cout << "Sec: " << duration.count()/1000000 << endl;
    return 0;
}
