# pooler.h
A tiny single-header thread-pool library for C++11 and above

## Why Pooler?
Using Pooler is similar to using other thread-pool libraries such as boost. 
The advantage of Pooler however, is that there is no need for the rest of boost or any other threading library. 
Pooler is built using standard c++ conditional_lock objects, so any compiler capable of C++11 can already compile a project with Pooler.
<br><br>
Pooler is great for existing projects where the addition of a whole threading library will either be too complicated to implement pratically, or introduce a large amount of overhead to the codebase. 
These frustrations were the driving forces behind the development of Pooler.
## Pooler vs Boost

No metrics on efficiency or speed have been calculated so far. An example of the differences in implementation can be found below:
#### Boost
This example was collected from [boost.org's thread_pool documentation.](https://www.boost.org/doc/libs/1_75_0/doc/html/boost_asio/reference/thread_pool.html)
```cpp
void my_task()
{
  ...
}

...

// Launch the pool with four threads.
boost::asio::thread_pool pool(4);

// Submit a function to the pool.
boost::asio::post(pool, my_task);

// Submit a lambda object to the pool.
boost::asio::post(pool,
    []()
    {
      ...
    });

// Wait for all tasks in the pool to complete.
pool.join();
```
#### Pooler
Pooler allows passing input data to threads before the RUN signal is issued using an optional second argument to the run() member function.
```cpp
#include "pooler.h"

POOLER_FUNC(my_task, {   // equivalent to   void my_task(Pooler::threadid_t id, void* data) { ... }
  ...
})

...

// Create a pool object and launch 4 threads
Pooler pool(4);

// Input data for the pool (optional)
void* inputData = ...;

// Signal to threads to perform a pooler func. Provide optional inputData to be shared by each thread
// Block until all threads are finished
pool.run(my_task, inputData);

// Signal to threads to perform a pooler lambda
pool.run(POOLER_LAMBDA{ // equivalent to  [](Pooler::threadid_t id, void* data)->void { ... }
  ...
});

// Stop all threads once run returns
pool.stop();
```
## Can I use pooler in in my project?
Yes. There are no restrictions on how you use Pooler or what you use it for. Personal and enterprise use is permitted free of charge. 
