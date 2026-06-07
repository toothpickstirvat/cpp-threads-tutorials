#include <iostream>
#include <thread>
#include <chrono>
using namespace std;

void run(int count) {
    while (count-- > 0) {
        cout << count << "C++" << endl;
    }
    //std::this_thread::sleep_for(std::chrono::seconds(3));
    cout << "thread finished" << endl;
}

int main() {
    std::thread t(run, 10);
    cout << "main()" << endl;
    // t.join();
    // if (t.joinable()) {
    //     t.join();
    // }

    // t.detach();
    // if (t.joinable()) {
    //     t.detach();
    // }
    // cout << "main() after" << endl;

    //std::this_thread::sleep_for(std::chrono::seconds(3));
    return 0;
}
