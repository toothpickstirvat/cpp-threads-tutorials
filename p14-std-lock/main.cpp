#include <iostream>
#include <thread>
#include <mutex>
using namespace std;

/*
 * Example 0: No deadlock
 *   Thread 1                     Thread 2
 *   std::lock(m1, m2);           std::lock(m1, m2);
 *
 * Example 1: No deadlock
 *   Thread 1                     Thread 2
 *   std::lock(m1, m2);           std::lock(m2, m1);
 *
 * Example 2: No deadlock
 *   Thread 1                     Thread 2
 *   std::lock(m1, m2, m3, m4);   std::lock(m3, m4);
 *                                std::lock(m1, m2);
 *
 * Example 3: Yes, the below can deadlock
 *   Thread 1                     Thread 2
 *   std::lock(m1, m2);           std::lock(m3, m4);
 *   std::lock(m3, m4);           std::lock(m1, m2);
 *
 */

std::mutex m1, m2;


void taskA() {
    while (true) {
        std::lock(m1, m2);
        cout << "task a\n";
        m1.unlock();
        m2.unlock();
    }
}

void taskB() {
    while (true) {
        std::lock(m2, m1);
        cout << "task b\n";
        m2.unlock();
        m1.unlock();
    }
}

int main() {
    std::thread t1(taskA);
    std::thread t2(taskB);
    t1.join();
    t2.join();

    return 0;
}
