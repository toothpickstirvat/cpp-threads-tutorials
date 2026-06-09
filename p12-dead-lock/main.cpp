#include <iostream>
#include <thread>
#include <mutex>
using namespace std;

std::mutex m1;
std::mutex m2;

// void thread1() {
//     m1.lock();
//     std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     m2.lock();
//     cout << "Critical section of Thread 1" << endl;
//     m2.unlock();
//     m1.unlock();
// }
//
// void thread2() {
//     m2.lock();
//     std::this_thread::sleep_for(std::chrono::milliseconds(100));
//     m1.lock();
//     cout << "Critical section of Thread 2" << endl;
//     m1.unlock();
//     m2.unlock();
// }

// 解决死锁：保持加锁的顺序一致
void thread1() {
    m1.lock();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    m2.lock();
    cout << "Critical section of Thread 1" << endl;
    m2.unlock();
    m1.unlock();
}

void thread2() {
    m1.lock();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    m2.lock();
    cout << "Critical section of Thread 2" << endl;
    m2.unlock();
    m1.unlock();
}

int main() {
    thread t1(thread1);
    thread t2(thread2);

    t1.join();
    t2.join();

    return 0;
}
