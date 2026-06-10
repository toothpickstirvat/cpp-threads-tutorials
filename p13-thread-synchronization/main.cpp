#include <iostream>
#include <thread>
#include <mutex>
using namespace std;

long long balance = 0;
std::mutex m;

void addMoney(const long long val) {
    m.lock();
    balance += val;
    m.unlock();
}

int main() {
    std::thread t1(addMoney, 100);
    std::thread t2(addMoney, 200);

    t1.join();
    t2.join();

    cout << "Balance: " << balance << endl;

    return 0;
}
