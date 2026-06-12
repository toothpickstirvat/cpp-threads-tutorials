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

void findEven(const ull start, const ull end) {
    for (ull i = start; i <= end; ++i) {
        if ((i & 1) == 0) {
            OddSum += i;
        }
    }
}

void findOdd(const ull start, const ull end) {
    for (ull i = start; i <= end; ++i) {
        if ((i & 1) == 1) {
            EvenSum += i;
        }
    }
}


int main() {
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
    void findEven(const ull start, const ull end) {
        ull sum = 0; // 栈上的局部变量，每个线程独立
        for (ull i = start; i <= end; ++i) {
            if ((i & 1) == 0) {
                sum += i;
            }
        }
        OddSum = sum; // 整个循环只写一次全局变量
    }
    
    void findOdd(const ull start, const ull end) {
        ull sum = 0;
        for (ull i = start; i <= end; ++i) {
            if ((i & 1) == 1) {
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

具体来说是：

- `std::thread::detach()`内部调用了`pthread_detach()`，在OS线程上打上一个标记，表示这个线程结束后资源自动回收，需要人来join。
    - 所谓打上标记，就是把`detach_state`从`JOINABLE`改成`DETACHED`。线程结束时，OS检查这个字段。
        - `JOINABLE`：保留资源，等待有人`pthread_join`
        - `DETACHED`：直接回收资源
    - TCB里有一个字段记录线程的分离状态
        - `detach_state = PTHREAD_CREATE_JOINABLE`
        - `detach_state = PTHREAD_CREATE_DETACHED`
- 清空`std::thread`对象，即解除绑定。

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

### OS线程

创建OS线程，本质上是让OS为这个线程准备做好独立运行所需的一切，然后交给调度器。
具体做了：

#### 1. 分配独立的栈

每个线程有自己的栈（Linux上默认是8MB），存放局部变量、函数调用帧。这是线程间最核心的隔离。

#### 2. 创建TCB（Thread Control Block）

OS为这个线程创建一个数据结构，记录它的状态：寄存器值、栈指针、程序计数器（PC）、线程ID、调度优先级等。

#### 3. 设置初始执行上下文

把程序计数器指向你传入的函数（如run），把栈指针只想新分配的栈。

#### 4. 加入调运起的运行队列

OS调度器看到这个新线程，在合适的时机把它分配到某个CPU核心上执行。

和进程相比，线程创建的代价小得多，原因就在这里：

- 创建进程：需要复制整个地址空间、文件描述符表等等
- 创建线程：只需要新栈+TCB，地址空间共享，不复制

在Linux上，`std::thread` -> `pthread_create()` -> `clone()`系统调用，本质就是创建一个共享空间的轻量级进程。

## try_lock_for VS try_lock_until

这两个函数都属于std::timed_mutex，功能相似但参数语义不同。

### try_lock_for - 等待一段时长

```c++
m.try_lock_for(std::chrono::seconds(1));
```

- 参数是相对时间（duration）：从调用那一刻起，最多等待多久
- 内部等价于：`try_lock_until(now()+duration)`
- 语义：我愿意等1秒

### try_lock_until - 等待到某个时间点

```c++
auto now = std::chrono::steady_clock::now();
m.try_lock_until(now + std::chrono::seconds(1));
```

- 参数是绝对时间点（time_point）：等到哪个时刻为止
- 你需要自己计算截止时间，灵活性更高
- 语义：我等到now+1秒这个时刻

### 总结

| 对比项  | try_lock_for | try_lock_until               |
|------|--------------|------------------------------|
| 参数类型 | duration（时长） | time_point（时间点）              |
| 参数示例 | seconds(1)   | now() + seconds(1)           |
| 使用场景 | 只关心等待多久      | 需要精确截止时间（如多步操作共享同一个deadline） |

## std::recursive_mutex

### 背景

普通`std::mutex`有一个硬性约束：同一个线程不能对它lock两次，第二次lock会造成死锁。但现实中有一类很自然的场景：递归函数或函数A调用函数B，两者都需要加锁，而且调用方已经持有锁。

```c++
// 用普通mutex会死锁
void funA() {
    m.lock();
    funcB(); // funcB也要lock m，死锁
    m.unlock();
}

void funcB() {
    m.lock(); // 同一线程再次lock，卡死
    // ...
    m.unlock();
}
```

`recursive_mutex`就是为了解决这个问题而生的：同一线程可以多次`lock`，内部维护一个计数器，`lock`几次就要`unlock`几次才真正释放。

### 使用场景

- 递归函数内部需要保护共享数据
- 同一线程内多个函数相互调用且都要加锁（但通常这是代码设计问题，能重构则重构）

### 注意

`recursive_mutex`比普通`mutex`开销更大（需要维护所有权和技术），不要随手替代普通`mutex`。

## std::lock_guard

### 背景

手动`lock/unlock`有个经典问题：如果中间抛异常，`unlock`永远不会被调用，锁泄漏，其他线程永远阻塞。

```c++
// 危险写法
m.lock();
doSomething(); // 如果这里抛异常，unlock不执行，死锁
m.unlock();
```

这是RAII（Resource Acquisition Is Initialization）思想的典型应用场景。`lock_guard`在构造时加锁，析构时自动解锁，无论函数正常推出还是异常退出，析构函数必定执行。

```c++
void task(const char* threadNumber, int loopFor) {
    std::lock_guard<mutex> lock(m1);  // 构造，m1.lock()
    for (int i = 0; i < loopFor; ++i) {
        buffer++;
        cout << threadNumber << buffer << endl;
    }
}   // 函数退出，lock析构，m1.unlock()自动调用
```

### 使用场景

- 绝大多数简单加锁场景：进入作用域加锁，离开作用域解锁
- 只需要“锁住整个代码块”的情况

### 局限

`lock_guard`及其简单，没有任何灵活性：

- 不能中途手动`unlock`
- 不能延迟加锁
- 不能转移所有权

## std::unique_lock

### 背景

`lock_guard`够用，但不够灵活。`unique_lock`是`lock_guard`
的功能超集，在保留RAII自动解锁的同时，增加了大量控制能力。它的出现是为了满足条件变量（`condition_variable`）、延迟加锁、超时加锁等高级场景。

```c++
std::unique_lock<mutex> lock(m); // 等价于lock_guard，构造时加锁

std::unique_lock<mutex> lock(m, std::defer_lock); // 构造时不加锁
lock.lock(); // 手动决定何时加锁
```

`std::defer_lock`是一个tag，告诉`unique_lock`先别加锁，我晚点手动锁。

### 使用场景

```c++
std::condition_variable cv;
std::unique_lock<mutex> lock(m);
cv.wait(lock, []{return ready;});
// wait内部需要能unlock（等待时释放锁）再lock（被唤醒后再重新获取）
// lock_guard做不到这一点
```

## 锁的“所有权”

本质就是：“谁负责unlock这把锁？”

### 从底层看

`mutex`本身只是一个状态机：

- `locked`：某人持有它
- `unlocked`：没人持有

它不记录“谁”锁了它（除了`recursive_mutex`需要记录线程ID）。`lock()`就是抢占，`unlock()`就是释放，仅此而已。
所有权是建立在`mutex`之上的软件层约定：由某个对象来独占地承担调用`unlock()`的职责。

### lock_guard VS unique_lock的区别就在这里

`lock_guard`的所有权是绑死的：

```c++
// 绑死：lock_guard活着，锁就不会释放;lock_guard死了，锁就释放
// 没有任何方式吧这个职责转交给别人
std::lock_guard<mutex> lock(m);
```

`unique_lock`的所有权是可转移的：

```c++
std::unique_lock<mutex> lock1(m); // lock1拥有锁
std::unique_lock<mutex> lock2 = std::move(lock1);
// 现在lock2拥有锁，lock1什么都没有
// 析构时，lock2负责unlock，lock1什么都不做
```

`move`之后，`lock1.owns_lock()`返回`false`，`lock2.own_lock()`返回`true`。“谁负责unlock”这个职责被转移了。

### defer_lock也是所有权的体现

```c++
std::unique_lock<mutex> lock(m, std::defer_lock);
// lock对象存在，但owns_lock() == false
// 此时lock不拥有m，析构时不会unlock

lock.lock(); // 现在owns_lock()==true，析构时会unlock
```

`unique_lock`内部就是维护了一个`bool owned`标志，析构函数检查这个标志再决定要不要`unlock`。

### 总结

所有权=析构时是否有义务调用`unlock()`。

- `lock_guard`这个义务不可转移
- `unique_lock`这个义务可以通过`move`转交给另一个对象

## std::condition_variable

条件变量是一种线程同步机制，用来解决这类问题：一个线程需要等待某个条件成立，才能继续执行。

### 为什么需要条件变量

如果没有条件变量，只能用轮训：

```c++
// 忙等，非常浪费CPU
while (balance == 0) {
    // 一直循环检查
}
```

条件变量让线程可以挂起睡眠，等条件满足时再被唤醒，不浪费CPU。

### 核心操作

| 操作                      | 说明                 |
|-------------------------|--------------------|
| cv.wait(mtx, predicate) | 条件不满足时释放锁并挂起，满足时继续 |
| cv.notify_one()         | 唤醒一个正在等待的线程        |
| cv.notify_all()         | 唤醒所有正在等在的线程        |

### wait()

wait()内部做的事：

```
unlock(m)   ← 释放锁
sleep()     ← 挂起
被唤醒后...
lock(m)     ← 重新加锁
检查条件
条件不满足    → 重复上面流程
条件满足      → 返回（此时锁是持有状态）
```

### Lost Wakeup（丢失唤醒）

```
withdrawMoney                                         addMoney
-------------------------------------------------------------------------------------------------------------
检查balance==0，确实为0
                                                      balance += 500
                                                      cv.notify_one()   ← 通知发出了(但此时还没人在等，通知丢失)
wait()  ← 开始加锁、挂起（永远睡着，没人再唤醒它了）
```

问题根源：检查条件和进入等待这两步之间有空隙，notify可能恰好这个空隙发生。

先持有锁，在调用wait()就消灭了这个空隙：

```
withdrawMoney                                         addMoney
-------------------------------------------------------------------------------------------------------------
lock(m)
检查balance == 0，确实为0
wait() → unlock(m) + 挂起 ← 这两步是原子的
                                                      lock(m)  ← 必须等 unlock 之后才能进来
                                                      balance += 500
                                                      notify_one()
                                                      unlock(m)
被唤醒，重新lock(m)，继续执行
```

addMoney在wait()释放锁之前根本进不来，所以通知永远不会丢失。

## std::lock

在多线程编程中，当多个线程需要同时持有多个互斥锁时，极易发生死锁。

经典的死锁场景：

Thread 1: lock(m1) -> 等待m2

Thread 2: lock(m2) -> 等待m1

两个线程互相等待对方释放锁，程序永久卡住。

死锁的四个必要条件（Coffman条件）：

- 互斥：资源一次只能被一个线程持有
- 持有并等待：持有一个锁的同时等待另一个
- 不可剥夺：锁只能由持有者释放
- 循环等待：形成T1 -> T2 -> T1的等待环

只要打破其中一个条件，就能避免死锁。std::lock破坏的是循环等待。

### 原理

`std::lock(m1, m2, ...)`是C++11引入的原子性多锁获取函数，它保证：要么全部加锁成功，要么全部不持有（无部分加锁的中间态）

内部实现策略（标准未规定具体算法，但常见实现使用以下思路）：
典型实现是：尝试加锁+回退(try-and-backoff）算法

- 尝试lock(m1)
- 尝试try_lock(m2)
    - 成功 -> 全部加锁完成
    - 失败 -> 释放m1，换一个顺序重试
- 循环，直到所有锁都拿到

关键点：当部分锁失败时，会释放已持有的所有锁重试，从而打破循环等待链。

### 与std::unique_lock配合使用

直接用`std::lock`有个问题：异常安全。如果加锁后抛异常，需要手动unlock，很容易望。推荐写法是配合`std::unique_lock`的
`std::adopt_lock`：
`std::adopt_lock`是一个标记（tag），用来告诉`unique_lock`或`scoped_lock`：这个锁已经被加锁了，你只需要接管它，负责释放就行，不要再加锁一次。

```c++
void taskA() {
    std::lock(m1, m2);                              // 先加锁
    std::unique_lock<std::mutex> lk1(m1, std::adopt_lock);  // 接管，不重新加锁
    std::unique_lock<std::mutex> lk2(m2, std::adopt_lock);  // 接管，不重新加锁
    // 函数结束时 lk1, lk2 自动 unlock，异常安全
}
```

C++17引入了更简洁的`std::scoped_lock`，一步到位：

```c++
void taskA() {
    std::scoped_lock lk(m1, m2);  // 自动多锁 + RAII，推荐！
    // ...
}
```

### 注意事项

- 必须手动unlock：`std::lock`本身不是RAII，加锁后需要手动释放，推荐用`scoped_lock`代替。
- 多次独立调用会死锁：所有需要同时持有的锁，必须在同一次`std::lock`调用中传入。
- 性能开销：由于可能多次重试，在高竞争场景下性能不如单锁。
- 递归锁：`std::lock`不支持`std::recursive_mutex`的递归语义，混用需小心。
- 只解决多锁死锁：对于单个锁的死锁（如递归加锁、忘记释放）`std::lock`无能为力。

## std::promise和std::future

多线程变成中，线程之间经常需要传递数据。在C++11之前，最常见的做法是：

```c++
//旧方式：共享变量+mutex+条件变量
ull result = 0;
bool ready = false;
std::mutex mtx;
std::condition_variable cv;

// 子线程
{
    std::lock_guard<std::mutex> lk(mtx);
    result = compute();
    ready = true;
}
cv.nofity_one();

// 主线程
std::unique_lock<std::mutex> lk(mtx);
cv.wait(lk, []{ return ready; });
// 使用result...
```

这段代码有几个明显的问题：

- 样板代码多：每次传值都要写一套mutex + condition_variable
- 容易出错：忘记notify、wait条件写错、虚假唤醒处理
- 异常无法传递：子线程崩了，主线程毫不知情，死等
- 语义不清晰：代码意图被同步细节淹没

解决思路：
C++11引入了更高层的抽象：把“一次性的跨线程传值”封装称一个独立概念。这个概念来自函数式变成和并发理论，叫`Future/Promise`
模型（最早可追溯到1977年的论文，Scala、Java、JavaScript都有类似实现）。

### 核心原理

#### 共享状态（Shared State）

`promise`和`future`的本质是共享一块内部状态，这块状态由标准库在堆上分配：

```
┌─────────────┐          ┌──────────────────────────┐         ┌─────────────┐
│   promise   │──write──▶│  Shared State (堆内存)    │◀──read──│   future    │
│  (写入端)    │          │  - 值 (T)                │         │  (读取端)    │
└─────────────┘          │  - 异常指针               │         └─────────────┘
                         │  - 状态标志 (ready/not)   │
                         │  - mutex + condition_var │
                         └──────────────────────────┘
```

### 状态机

共享状态只有三种状态：

```
[无值]  ──set_value()──▶  [有值，ready]
   │
   └──set_exception()──▶  [有异常，ready]

```

一旦进入`ready`状态，所有阻塞在`future.get()`上的线程都会被唤醒。

### 所有权模型

- promise → 只能 move，不能 copy（独占写入权）
- future → 只能 move，不能 copy（独占读取权）
- shared_future → 可以 copy（多个读取端）

### 使用场景

#### 最基础用法

子线程计算，主线程等待结果

```c++
// 子线程写入
void worker(std::promise<int>&& p) {
    p.set_value(42);
}
//主线程
std::promise<int> p;
std::future<int> f = p.get_future();
std::thread t(worker, std::move(p));
int result = f.get(); // 阻塞直到子线程set_value
t.join();
```

#### 传递异常

这回`promise/future`相比共享变量最大的优势之一。

```c++
void worker(std::promise<int>&& p) {
    try {
        // 模拟可能失败的操作
        throw std::runtime_error("计算失败");
    } catch (...) {
        p.set_exception(std::current_exception());  // 把异常打包传给主线程
    }
}

// 主线程
try {
    int result = f.get();  // 如果子线程设置了异常，这里会重新抛出
} catch (const std::exception& e) {
    std::cout << "捕获到异常: " << e.what() << std::endl;
}
```

子线程的异常被"渡送"到主线程，不再静默消失。

#### 带超时的等待

```c++
auto status = f.wait_for(std::chrono::seconds(2));

switch (status) {
    case std::future_status::ready:
        std::cout << "结果: " << f.get() << std::endl;
        break;
    case std::future_status::timeout:
        std::cout << "超时！" << std::endl;
        break;
    case std::future_status::deferred:
        // std::async 的延迟执行情况
        break;
}
```

#### 一对多广播（shared_future）

一个`future`只能`get()`一次，但是`shared_future`可以被多个线程同时等待

```c++

std::promise<int> p;
std::shared_future<int> sf = p.get_future().share();

// 可以把sf拷贝给多个线程
auto t1 = std::thread([sf]{ std::cout << sf.get() << std::endl; });
auto t2 = std::thread([sf]{ std::cout << sf.get() << std::endl; });

p.set_value(100);  // 同时唤醒t1和t2
t1.join(); t2.join();

```

#### std::async（promise的上层封装）

大多数时候你需要手写promise，`std::async`自动创建

```c++
// async内部自动创建promise，函数返回值自动set_value
std::future<int> f = std::async(std::launch::async, []{
    return 42;
});
int result = f.get();
```

用`promise`的场合：需要在任意时刻、任意地点（不一定是函数返回时）设置值，比如回调、信号处理、事件驱动。

### 注意事项

#### get()只能调用一次

```c++
f.get();  // OK
f.get();  // 抛出 std::future_error: No associated state
```

第二次调用会抛异常，因为值已经被"取走"了。

#### promise销毁时没有set_value → 异常

````c++
std::future<int> f;
{
    std::promise<int> p;
    f = p.get_future();
    // p离开作用域，但没有set_value！
}
// 此时f的共享状态被标记为broken_promise
f.get();  // 抛出 std::future_error: Broken promise
````

`promise`析构时，如果共享状态还没就绪，会自动设置一个`broken_promise`异常。

#### get()之前线程已经结束不代表值已就绪

子线程可能在`set_value`之前崩溃，这时你会收到`broken_promise`异常，而不是死等。

#### future析构会隐式join（仅std::async创建的情况

```c++
{
    auto f = std::async(std::launch::async, []{
        std::this_thread::sleep_for(std::chrono::seconds(5));
    });
    // f 离开作用域 → 析构函数会阻塞 5 秒等待线程完成！
}
```

这是`std::async`返回的`future`的特殊行为，手动创建的`promise/future`不会这样。

#### promise不能copy，只能move

```c++
std::promise<int> p1;
std::promise<int> p2 = p1;         // 编译错误
std::promise<int> p3 = std::move(p1);  // OK
```

传给线程时必须用`std::move`，这也是你代码里`std::move(oddSum)`的原因。

### 总结

`promise`是写入端，`future`是读取端，两者共享一块内部状态。
`promise`在子线程写入值或异常，`future`在主线程阻塞等待并取出——标准库把`mutex`和条件变量的复杂性全部封装在内部，让跨线程的一次性传值变得干净、安全、可传递异常。

## std::async

C++11之前，多线程编程需要手动管理pthread或平台原生线程，代码繁琐且容易出错。C++11 引入了高层异步抽象：

- std::thread → 最底层，手动管理生命周期
- std::async → 更高层，自动管理线程 + 返回值
- std::future → 异步结果的"句柄"
- std::promise → 手动设置异步结果

std::async是"任务式并发"的入口：你描述做什么，而不是怎么管线程。

### 函数签名

```c++
// 头文件：<future>
template< class Function, class... Args >
std::future<std::invoke_result_t<Function, Args...>>
async( std::launch policy, Function&& f, Args&&... args );

// 也可省略 policy（由实现决定）
template< class Function, class... Args >
std::future<...> async( Function&& f, Args&&... args );

```

### launch policy

| Policy                  | 含义    | 何时执行                     |
|-------------------------|-------|--------------------------|
| `std::launch::async`    | 立即异步  | 调用`async`时就启动            |
| `std::launch::deferred` | 延迟执行  | 调用`.get()`或`.wait()`时才执行 |
| `async\|deferred`       | 由实现决定 | 不确定                      |

代码分析：

```c++
// 此处不启动线程，只是"登记"了任务
std::future<ull> oddSum = std::async(std::launch::deferred, findOdd, start, end);

cout << "Waiting for result!!" << endl;

// 直到这里才真正在主线程中执行 findOdd
cout << "OddSum: " << oddSum.get() << endl;
```

所以你会看到`findOdd`的`ThreadID`和`main`的`ThreadID`相同。

### 原理

std::async 内部工作流程：

```
std::async(policy, f, args...)
          │
          ├── launch::async
          │     └─ 创建新线程执行 f(args...)
          │        线程结果存入 shared state
          │
          └── launch::deferred
                └─ 将 f + args 打包存起来，不执行
                   当 future.get() 被调用时才执行

future<T>
 └── 持有 shared state 的指针
       ├── 结果值（或异常）
       ├── 状态标志（ready / not-ready）
       └── 同步原语（mutex/condvar）

.get()
 └── 阻塞等待 shared state 变为 ready
     取出结果（只能调用一次！）
```

### 使用场景

#### 并行计算（async）

```c++
// 将任务切分，并行执行
auto f1 = std::async(std::launch::async, findOdd, 0, 500000000);
auto f2 = std::async(std::launch::async, findOdd, 500000001, 1000000000);

ull result = f1.get() + f2.get();  // 理论上快 2 倍
```

#### 异步IO/网络请求

```c++
auto future = std::async(std::launch::async, []() {
    return fetch_from_network("http://...");
});

// 主线程做其他工作
do_other_work();

// 需要结果时再取
auto data = future.get();
```

#### 惰性求值（deferred）

```c++
// 只有真正需要时才计算，避免不必要的开销
auto result = std::async(std::launch::deferred, expensive_computation, input);

if (need_result) {
    use(result.get());   // 此时才执行计算
}
// 如果不需要，计算永远不会发生
```

#### 异常传播

```c++
auto f = std::async(std::launch::async, []() -> int {
    throw std::runtime_error("出错了");
    return 42;
});

try {
    int val = f.get();   // 异常在这里被重新抛出！
} catch (const std::exception& e) {
    // 可以安全捕获异步线程中的异常
}

```

### 注意事项

#### future析构行为 — 最大陷阱

```c++
// 危险！async返回的future被立即析构
std::async(std::launch::async, some_task);  // 析构时会阻塞等待任务完成！

// 正确做法：持有 future
auto f = std::async(std::launch::async, some_task);
// ... 做其他事
f.get();  // 或者让f在作用域末尾析构
```

`std::async`返回的`future`析构时，如果是`async`策略，会阻塞直到任务完成。这与普通`future`不同。

#### 默认policy不确定

```c++
// 这行代码可能在新线程执行，也可能在当前线程惰性执行
// 行为由实现决定，不要依赖它
auto f = std::async(findOdd, 0, 1000);

// 应该明确指定policy
auto f = std::async(std::launch::async, findOdd, 0, 1000);
```

#### .get()只能调用一次

```c++
auto f = std::async(std::launch::async, []{ return 42; });
int a = f.get();   // OK
int b = f.get();   // 抛出std::future_error！
```

#### async不是线程池

每次`std::launch::async`都可能创建新线程，大量调用会耗尽资源：

```c++
// 危险：可能创建数千个线程
for (int i = 0; i < 10000; ++i) {
    futures.push_back(std::async(std::launch::async, task));
}

// 生产环境应该用线程池（如Intel TBB、自定义实现）
```

#### 参数传递是拷贝

```c++
std::string data = "hello";
// data 会被拷贝进 async，修改不影响原变量
auto f = std::async(std::launch::async, [](std::string s){ ... }, data);

// 若需引用，用 std::ref
auto f = std::async(std::launch::async, func, std::ref(data));
// 但需确保data的生命周期覆盖future
```

### 与thread/promise对比

| 对比项  | std::thread     | std::async   | std::promise      |
|------|-----------------|--------------|-------------------|
| 返回值  | 需手动             | 自动           | 手动                |
| 异常传播 | 程序崩溃            | 自动传给`future` | 手动`set_exception` |
| 生命周期 | 必须`join/detach` | 自动           | 手动                |
| 灵活性  | 最高              | 中等           | 最高                |
| 推荐场景 | 长期运行线程          | 一次性任务        | 跨线程通知             |

`std::async` — 自动：你只需要写普通函数，return返回值即可，框架自动把返回值塞进`shared_state`，你不需要写任何`set_value`。

```c++
ull findOdd(ull start, ull end) {
    // ...
    return oddSum;   // 这个返回值被async自动存入future，你不用管
}

auto f = std::async(std::launch::async, findOdd, 0, 1000);
f.get();  // 自动拿到oddSum

```

`std::promise` — 手动：没有函数返回值机制，你必须显式调用`set_value`

```c++
std::promise<int> p;
std::future<int> f = p.get_future();

std::thread t([&p]() {
    int result = compute();
    p.set_value(result);   // 必须手动set，否则f.get()永远阻塞
});

f.get();
t.join();
```

`std::async`的"自动"是指它把函数的return值自动映射为`future`的结果，背后当然有`set_value`，但是框架替你做了，你不用写。
`std::promise`则是把这个控制权完全交给你。










































