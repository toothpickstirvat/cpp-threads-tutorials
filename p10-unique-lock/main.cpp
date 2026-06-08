#include <iostream>
#include <thread>
#include <mutex>
using namespace std;

std::mutex m1;
int buffer = 0;

// example 1
// void task(const char* threadNumber, int loopFor) {
//     std::unique_lock<mutex> lock(m1); // 自动调用m1.lock()
//     for (int i = 0; i < loopFor; i++) {
//         buffer++;
//         cout << threadNumber << buffer << endl;
//     }
// }
//
// int main() {
//     std::thread t1(task, "T1 ", 10);
//     std::thread t2(task, "T2 ", 10);
//     t1.join();
//     t2.join();
//     cout << "buffer: " << buffer << endl;
//
//     return 0;
// }


// example 2
void task(const char* threadNumber, int loopFor) {
    std::unique_lock<mutex> lock(m1, std::defer_lock); // 不会调用m1.lock()
    lock.lock();
    for (int i = 0; i < loopFor; ++i) {
        buffer++;
        cout << threadNumber << buffer << endl;
    }
}

int main() {
    std::thread t1(task, "T1 ", 10);
    std::thread t2(task, "T2 ", 10);
    t1.join();
    t2.join();
    cout << "buffer: " << buffer << endl;

    return 0;
}


