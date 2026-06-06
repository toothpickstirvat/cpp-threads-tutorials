# CPP Threads Tutorials

## False Sharing（伪共享）

下面的代码，多线程执行要用10+秒，而单线程执行只需2秒，这是为什么？

```c++
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
using namespace std;
using namespace std::chrono;
typedef unsigned long long ull;

ull OddSum = 0;
ull EvenSum = 0;

void findEven(const ull start, const ull end)
{
    for (ull i = start; i <= end; ++i)
    {
        if ((i & 1) == 0)
        {
            OddSum += i;
        }
    }
}

void findOdd(const ull start, const ull end)
{
    for (ull i = start; i <= end; ++i)
    {
        if ((i & 1) == 1)
        {
            EvenSum += i;
        }
    }
}


int main()
{
    constexpr ull start = 0;
    constexpr ull end = 1900000000;
    const auto startTime = high_resolution_clock::now();
    
    // 多线程执行
    std::thread t1(findOdd, start, end);
    std::thread t2(findEven, start, end);
    t1.join();
    t2.join();

    // 单线程执行
    // findOdd(start, end);
    // findEven(start, end);
    
    const auto stopTime = high_resolution_clock::now();
    const auto duration = duration_cast<microseconds>(stopTime - startTime);
    
    cout << "OddSum: " << OddSum << endl;
    cout << "EvenSum: " << EvenSum << endl;
    cout << "Sec: " << duration.count()/1000000 << endl;
    
    return 0;
}
```

这就是伪共享问题。`OddSum`和`EvenSum`是两个相邻的全局变量，在内存中紧挨着，大概率落在同一各个CPU缓存行（cache line，通常64字节）里。
多核CPU的缓存一致性协议（MESI）规定：任何一个核修改了缓存行，其他持有该缓存行的核必须将其标记为无效，然后重新从内存或其他核同步。
结果就是：

- t1修改OddSum -> t2持有的缓存行失效 -> t2必须重新加载
- t2修改EvenSum -> t1持有的缓存行失效 -> t1必须重新加载
- 两个线程相互“踢”对方的缓存，每次循环都在做无效化操作（标记无效，强制重新同步），比单线程穿行执行慢得多

### 修复方法

* 用`alignas(64)`把两个变量强制对齐到不同的缓存行
    ```c++
    alignas(64) ull OddSum = 0;
    alignas(64) ull EvenSum = 0;
    ```
* 使用局部变量
    ```c++
    void findEven(const ull start, const ull end)
    {
        ull sum = 0; // 栈上的局部变量，每个线程独立
        for (ull i = start; i <= end; ++i)
        {
            if ((i & 1) == 0)
            {
                sum += i;
            }
        }
        OddSum = sum; // 整个循环只写一次全局变量
    }
    
    void findOdd(const ull start, const ull end)
    {
        ull sum = 0;
        for (ull i = start; i <= end; ++i)
        {
            if ((i & 1) == 1)
            {
                sum += i;
            }
        }
        EvenSum = sum;
    }
    ```
  局部变量在各自线程的栈上，内存地址相距很远，根本不再同一个缓存行，自然就没有false sharing。


