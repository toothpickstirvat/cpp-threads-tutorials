#include <iostream>
#include <thread>
using namespace std;

// 1.函数指针
// void fun(int x) {
//     while (x-- > 0) {
//         cout << x << endl;
//     }
// }
//
// int main() {
//     std::thread t(fun, 10);
//     t.join();
//     return 0;
// }

// 2.Lambda函数
// int main() {
//     auto fun = [](int x) {
//         while (x-- > 0) {
//             cout << x << endl;
//         }
//     };
//
//     std::thread t(fun, 10);
//     t.join();
//     return 0;
// }

// 3. Functor(Function Object)
// class Base {
// public:
//     void operator()(int x) const {
//         while (x-- > 0) {
//             cout << x << endl;
//         }
//     }
// };
// int main() {
//     std::thread t(Base(), 10);
//     t.join();
//     return 0;
// }

// 4. Non-static member function
// class Base {
// public:
//     void run(int x) {
//         while (x-- > 0) {
//             cout << x << endl;
//         }
//     }
// };
// int main() {
//     Base b;
//     std::thread t(&Base::run, &b, 10);
//     t.join();
//     return 0;
// }

// 5. Static member function
class Base {
public:
    static void run(int x) {
        while (x-- > 0) {
            cout << x << endl;
        }
    }
};

int main() {
    std::thread t(Base::run, 10);
    t.join();
    return 0;
}
