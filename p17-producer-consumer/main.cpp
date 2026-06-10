#include <iostream>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
using namespace std;
using namespace std::chrono;

std::mutex m;
std::condition_variable cond;
deque<int> buffer;
constexpr unsigned int maxBufferSize = 50;

void producer(int val) {
    while (val) {
        std::unique_lock<std::mutex> locker(m);
        cond.wait(locker, []() {
            return buffer.size() < maxBufferSize;
        });
        buffer.push_back(val);
        cout << "Produced: " << val << endl;
        val--;
        locker.unlock();
        cond.notify_one();
    }
}

void consumer() {
    while (true) {
        std::unique_lock<std::mutex> locker(m);
        cond.wait(locker, []() {
            return buffer.size() > 0;
        });
        int val = buffer.back();
        buffer.pop_back();
        cout << "Consumed: " << val << endl;
        locker.unlock();
        cond.notify_one();
    }
}

int main() {
    std::thread t1(producer, 100);
    std::thread t2(consumer);
    t1.join();
    t2.join();

    return 0;
}

