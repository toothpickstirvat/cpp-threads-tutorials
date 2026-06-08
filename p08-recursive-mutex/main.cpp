#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>
using namespace std;

// example 1: with recursion
// std::recursive_mutex m1;
// int buffer = 0;
//
// void recursion(char c, int loopFor) {
//     if (loopFor < 0) {
//         return;
//     }
//
//     m1.lock();
//     cout << "ThreadID:" << c << " " << buffer++ << endl;
//     recursion(c, --loopFor);
//     m1.unlock();
//     cout << "unlock by thread " << c << endl;
// }
//
// int main() {
//     std::thread t1(recursion, '1', 10);
//     std::thread t2(recursion, '2', 10);
//     t1.join();
//     t2.join();
//
//     return 0;
// }


// example 2: with loop
std::recursive_mutex m1;
int main() {
    for (int i = 0; i < 5; ++i) {
        m1.lock();
        cout << "locked" << i << endl;
    }
    for ( int i = 0; i < 5; ++i) {
        m1.unlock();
        cout << "unlocked" << i << endl;
    }
    return 0;
}
