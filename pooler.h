/*

*/


#ifndef HONEYLIB_POOLER_H
#define HONEYLIB_POOLER_H

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>

#include <vector>
#include <cstdint>
#include <iostream>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>

typedef uint16_t threadid_t;
typedef std::function<void(threadid_t, void*)> pooler_func_t;

#define POOLER_FUNC(funcName, code) void funcName(threadid_t id, void* data) code 

class Pooler {
	private:
		std::vector<std::thread> _threads;
		const threadid_t _THREAD_COUNT;
		std::atomic<threadid_t> _threadsComplete;
		std::atomic<threadid_t> _threadsWaiting;

		std::mutex _actionLock;
		std::mutex _waitLock;
		std::mutex _completeLock;
		std::condition_variable _actionCv;
		std::condition_variable _waitCv;
		std::condition_variable _completeCv;

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
		pooler_func_t _threadCallback;
		// A pointer to some data structure 
		void* _threadParam;

	public:
		Pooler(threadid_t threadCount) : _THREAD_COUNT(threadCount) {
			this->resetThreadLoop();

			// Start threads
			for (threadid_t id=0;id<threadCount;id++) {
				this->_threads.push_back(std::thread(&Pooler::threadAction, this, id));
			}
		}

		~Pooler() {}

		/* @brief run | Calculate fft in multiple threads
		 * @param[in] samples	Calculate fft on a list of samples
		 */
		void run(pooler_func_t newCallback, void* newParam = nullptr) {
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
					this->_threadCallback = newCallback;
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

		/* @brief Join all threads
		 */
		void stop() {
			this->waitForThreadsToFinish();

			//this->_completeCv.notify_all();
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
		 * @description Waits for RUN commands, performs action, then signals complete
		 * @param threadID	ID of this thread. Thread #1 is index 0
		 */
		void threadAction(threadid_t threadID) {
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
						//std::cout << "Stop command received\n";
						break; // Stop the loop
					} 
				}

				// Do FFT
				this->_threadCallback(threadID, this->_threadParam);

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
