#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>

using namespace std;

int counter = 0;
std::mutex mtx;

void increase() {
    for (int i = 0; i < 100000; ++i) {
        if (mtx.try_lock()) { // 尝试加锁，若无法加锁，立即返回false，不会阻塞
            ++counter;
            mtx.unlock();
        }
    }
}

int main() {
    std::thread t1(increase);
    std::thread t2(increase);
    t1.join();
    t2.join();

    cout << "counter: " << counter << endl;

    return 0;
}
