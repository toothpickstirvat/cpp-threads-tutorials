#include <iostream>
#include <chrono>
#include <mutex>
#include <thread>

using namespace std;
// std::try_lock会按顺序依次尝试锁定传入的所有的锁对象

int X = 0, Y = 0;
std::mutex m1, m2;

void doSomeWork(int seconds) {
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

void increaseXY(int& XorY, std::mutex& m, const char* desc) {
    for (int i = 0; i < 5; ++i) {
        m.lock();
        ++XorY;
        cout << desc << XorY << '\n';
        m.unlock();
        doSomeWork(1);
    }
}

void consumeXY() {
    int useCount = 5;
    int XplusY = 0;
    while (1) {
        int lockResult = std::try_lock(m1, m2); // 尝试对m1和m2加锁
        if (lockResult == -1) { // 全部加锁成功
            if (X != 0 && Y != 0) {
                --useCount;
                XplusY += X+Y;
                X = 0;
                Y = 0;
                cout << "XplusY " << XplusY << '\n';
            }
            m1.unlock();
            m2.unlock();

            if (useCount == 0) {
                break;
            }
        }
    }
}


int main() {
    std::thread t1(increaseXY, std::ref(X), std::ref(m1), "X ");
    std::thread t2(increaseXY, std::ref(Y), std::ref(m2), "Y ");
    std::thread t3(consumeXY);

    t1.join();
    t2.join();
    t3.join();

    return 0;
}
