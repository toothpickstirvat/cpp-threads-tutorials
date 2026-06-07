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

## Join & Detach

操作系统里，线程是一个独立的执行单元，但它不是完全自治的，它的生命周期需要有人来交代清楚。`std::thread`对象被析构时，如果它仍然是
`joinable`状态（既没有`join`也没有`detach`），C++标准规定直接调用`std::terminate()`
杀死整个进程。这个设计是故意的强迫你明确表态：这个子线程你到底想怎么处理？

注意：
`joinable()`检查的不是线程是否在运行，而是这个thread对象有没有被交代清楚（join或detach）。哪怕自线程已经跑完了，只要你没有调用
`join/detach`，`t.joinable()`让人是`true`，析构时仍然`terminate`。

### join做了什么

主线程在`t.join()`处阻塞，等待子线程执行完毕，然后回收它的资源（回收线程句柄/OS资源）。

### detach做了什么

把子线程从`std::thread`对象上分离，让它变成一个后台线程（`daemon thread`），自己跑完自己消亡，主线程不管它。换句话说，
`detach()`就是切断C++对象对OS线程的持有，OS线程继续跑，C++对象变成空壳。

### 资源回收角度

#### join

`pthread`（Linux底层线程库）里，一个线程跑完后，它的资源（栈、TCB线程控制块、退出状态）不会立刻被回收，而是进入僵尸的状态，等待有人来
`pthread_join`取走退出状态，然后才释放。`std::thread::join()`底层就是`pthread_join()`，所以说`join`涉及资源回收，这部分是真的。
但`join`更核心的语义是同步：主线程在这里等自线程跑完，资源回收只是等完之后顺带的事。

#### detach

`detach`做的事：放弃对这个线程的管理权，线程跑完后让OS自己清理，我不管了。资源是线程结束时OS自动回收的，不是`detach()`调用时回收的。
`detach`本身不释放任何资源，它只是把`std::thread`对象和底层OS线程解绑。

#### 更准确的理解

| 视角     | join        | detach     |
|--------|-------------|------------|
| 谁等线程结束 | 主线程         | 没人等，OS自己处理 |
| 资源何时释放 | `join()`返回时 | 线程自己跑完时    |
| 核心语义   | 同步          | 放弃所有权      |

### C++线程

C++里线程这个词在两个层面是混用的：

- `std::thread`对象：C++层面的管理句柄，是一个普通的栈上变量，负责持有对OS线程的所有权。
- OS线程：正真在跑的执行单元，有自己的栈、寄存器、调度状态。

#### 再次理解

```c++
std::thread t(run, 10);
```

这行代码做了两件事：

- 调用OS接口（Linux上是`pthread_create`）创建一个OS线程，开始执行`run(10)`
- 把这个OS线程的句柄存进`std::thread`对象t里，由t持有所有权
- 后续join/detach的本质就是在处理这个绑定关系：
    - `join()`：等OS线程跑完，然后解除绑定，释放资源
    - `detach()`：直接解除绑定，OS线程自生自灭


















