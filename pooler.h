/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 * 
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * 
 * For more information, please refer to <https://unlicense.org>
*/

#ifndef HONEYLIB_POOLER_H
#define HONEYLIB_POOLER_H

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>

#include <vector>
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>

#define __POOLER_FUNC_ARGS 	Pooler::threadid_t id, void* data

/* @brief Define a new function with thread arguments
 * @param[in] callback	A name for this function variable; where 'callback' is the function name within <> in 'void <callback>(...) {...}'
 * @param[out] id	A Pooler::threadid_t containing the id of the running thread, from 0 to N (where N is the pooler's thread count)
 * @param[out] data	A pointer to a shared struct provided at "run()"-time
 */
#define POOLER_FUNC(callback, code) void callback(__POOLER_FUNC_ARGS) code 

/* @brief Define a lambda function to be passed directly into "run()"
 * @param[in] callback	A name for this function variable; where 'callback' is the function name within <> in 'void <callback>(...) {...}'
 * @param[out] id	A Pooler::threadid_t containing the id of the running thread, from 0 to N (where N is the pooler's thread count)
 * @param[out] data	A pointer to a shared struct provided at "run()"-time
 */
#define POOLER_LAMBDA [](__POOLER_FUNC_ARGS)->void

/* @brief 	The pooler class. Each instance of the pooler class is a separate thread pool.
 * @description Obviously, The pool is thread-safe by default. 
 * @description The pool can be made unsafe with irresponsible use of shared data in POOLER_FUNCs.
 */
class Pooler {
	// Typedefs 
	public:
		typedef uint16_t threadid_t;
		typedef std::function<void(threadid_t, void*)> func_t;

	// Private vars and forward declarations
	private:
		// Store threads
		std::vector<std::thread> _threads;
		const Pooler::threadid_t _THREAD_COUNT;
		
		// Synchronization 
		std::atomic<Pooler::threadid_t> _threadsComplete;
		std::atomic<Pooler::threadid_t> _threadsWaiting;
		std::mutex _actionLock;
		std::mutex _waitLock;
		std::mutex _completeLock;
		std::condition_variable _actionCv;
		std::condition_variable _waitCv;
		std::condition_variable _completeCv;
		
		// The internal state of the pooler. It can either wait, or perform a command. Each command is listed in the Action enum. 
		enum Action {
			RUN,
			WAIT,
			STOP
		} _action;

		/* @brief resetThreadLoop will initialize the condition lock variables
		 */
		void resetThreadLoop() {
			this->_threadsWaiting = 0;
			this->_threadsComplete = 0;
			this->_action = WAIT;
		}

		/* @brief Wait for all threads to be ready for a new instruction, after performing the previous one
		 */
		void waitForThreadsToFinish() {
			std::unique_lock<std::mutex> waitLock(this->_waitLock);
						
			if (this->_threadsWaiting != this->_THREAD_COUNT) {
				this->_waitCv.wait(waitLock, [&]{return this->_threadsWaiting == this->_THREAD_COUNT;});
			}
		}

		/* @brief Tell the threads to go back to the idle state,where they will wait for the next instruction. 
		 */
		void tellThreadsToIdle() {
			{
				std::lock_guard<std::mutex> lock(this->_actionLock);
				this->resetThreadLoop();
			}
			this->_actionCv.notify_all();
		}
	
		// A callback performed by each thread
		Pooler::func_t _threadCallback;
		// A pointer to some data structure 
		void* _threadParam;

	public:
		/* @brief Construct a new pooler object
		 * @param[in] threadCount	The number of threads this thread pool instance will use
		 */
		Pooler(Pooler::threadid_t threadCount) : _THREAD_COUNT(threadCount) {
			this->resetThreadLoop();

			// Start threads
			for (Pooler::threadid_t id=0;id<threadCount;id++) {
				this->_threads.push_back(std::thread(&Pooler::threadAction, this, id));
			}
		}

		~Pooler() {}

		/* @brief Set the callback to perform in all threads, then signal each thread to perform the action. 
		 * @param[in] callback	The callback function as defined in some POOLER_FUNC
		 * @param[in] newParam	A pointer to some data-structure that will be passed to each thread. Thread-safe by default. 
		 */
		void run(Pooler::func_t callback, void* newParam = nullptr) {
			// Wait for all threads to begin waiting for the next action
			this->waitForThreadsToFinish();

			// Tell all the threads all the information they need to know
			{
				std::unique_lock<std::mutex> completeLock(this->_completeLock);

				this->_threadsWaiting = 0;

				// While the completeLock is locked, tell the threads to start.
				// No threads can finish before we have a chance to grab the completeLock, because we already have it. 
				{
					std::lock_guard<std::mutex> lock(this->_actionLock);
					this->_threadCallback = callback;
					this->_threadParam = newParam;
					this->_action = RUN;
				}
					
				// Notify the threads to start processing using this data and the previous iteration's samples
				this->_actionCv.notify_all();

				// Yield this thread's execution status
				std::this_thread::yield();

				// Wait for all threads to complete
				this->_completeCv.wait(completeLock, [&]{return this->_threadsComplete == this->_THREAD_COUNT;});
			}

			// Set the action back to idle, telling the threads to go back to the start in the process. 
			this->tellThreadsToIdle();
		}

		/* @brief Wait for all threads to finish their job, tell the threads to perform a STOP command, then wait for all threads to terminate 
		 */
		void stop() {
			this->waitForThreadsToFinish();

			// Send stop command to all threads
			{
				std::lock_guard<std::mutex> lock(this->_actionLock);
				this->_action = STOP;
			}
			this->_actionCv.notify_all();
				
			// Wait for threads to finish
			for (threadid_t i=0;i<this->_threads.size();i++) {
				this->_threads[i].join();
			}

			this->_threads.clear();
		}
	
	private:

		/* @brief The action that threads in the pool perform until a joinall/STOP command. 
		 * @description 	Waits for RUN commands, performs action, then signals complete
		 * @param[in] threadID	ID of this thread. Thread #1 is index 0
		 */
		void threadAction(Pooler::threadid_t threadID) {
			// Threads will loop forever, waiting for instructions from the main thread
			while (true) {
				// -- CRITICAL SECTION -- 
				{
					// We will be waiting for commands
					std::unique_lock<std::mutex> lock(this->_actionLock);
					
					{
						std::lock_guard<std::mutex> waitLock(this->_waitLock);
						this->_threadsWaiting++;
					}
					this->_waitCv.notify_all();

					
					// Now wait for a command
					this->_actionCv.wait(lock, [&]{return this->_action != WAIT;});

					if (this->_action == STOP) {
						break; // Stop the loop
					} 
				}

				// Do Thread action
				this->_threadCallback(threadID, this->_threadParam);
				
				// Signal to the main thread that we have finished work. 
				{
					std::unique_lock<std::mutex> lock(this->_actionLock);

					{
						std::lock_guard<std::mutex> completeLock(this->_completeLock);
						this->_threadsComplete++;
					}
					this->_completeCv.notify_all();

					this->_actionCv.wait(lock, [&]{return this->_action == WAIT;});
				}
			}
		}
};
#endif
