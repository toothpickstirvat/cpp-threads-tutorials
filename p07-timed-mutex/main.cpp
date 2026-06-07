#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>
using namespace std;

int myAmount = 0;
std::timed_mutex m;

// void increase(int i) {
//     if (m.try_lock_for(std::chrono::seconds(1))) { // 阻塞到超时或成功获取的到锁
//         ++myAmount;
//         std::this_thread::sleep_for(std::chrono::seconds(2));
//         cout << "Thread " << i << " Entered" << endl;
//         m.unlock();
//     } else {
//         cout << "Thread " << i << " Couldn't Enter" << endl;
//     }
// }

void increase(int i) {
    auto now = std::chrono::steady_clock::now();
    if (m.try_lock_until(now+std::chrono::seconds(1))) {
        ++myAmount;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        cout << "Thread " << i << " Entered" << endl;
        m.unlock();
    } else {
        cout << "Thread " << i << " Couldn't Enter" << endl;
    }
}

int main() {
    std::thread t1(increase, 1);
    std::thread t2(increase, 2);

    t1.join();
    t2.join();

    cout << myAmount << endl;

    return 0;
}
