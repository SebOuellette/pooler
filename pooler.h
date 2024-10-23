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


#define POOLER_FUNC [&](Pooler::ThreadID id, void* data) -> void

class Pooler {
	private:
		std::vector<std::thread> _threads;
		const uint8_t _THREAD_COUNT;
		std::atomic<uint8_t> _threadsComplete;
		std::atomic<uint8_t> _threadsWaiting;

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

		// A callback performed by each thread
		POOLER_FUNC _threadCallback;
		// A pointer to some data structure 
		void* _threadParam;

	public:
		typedef uint8_t ThreadID;

		Pooler(uint8_t threadCount) : _THREAD_COUNT(threadCount) {
			this->resetThreadLoop();

			// Start threads
			for (uint8_t id=0;id<threadCount;id++) {
				this->_threads.push_back(std::thread(&Pooler::threadAction, this, id));
			}
		}

		~Pooler() {}

		/* @brief run | Calculate fft in multiple threads
		 * @param[in] samples	Calculate fft on a list of samples
		 */
		void run(complex_list* samples, POOLER_FUNC newCallback, void* newParam = nullptr) {
			// Wait for all threads to begin waiting for the next action
			{
				std::unique_lock<std::mutex> waitLock(this->_waitLock);
						
				if (this->_threadsWaiting != this->_THREAD_COUNT) {
					this->_waitCv.wait(waitLock, [&]{return this->_threadsWaiting == this->_THREAD_COUNT;});
				}
			}

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
			{
				std::lock_guard<std::mutex> lock(this->_actionLock);
				this->resetThreadLoop();
			}
			this->_actionCv.notify_all();
		}

		/* @brief Join all threads
		 */
		void joinall() {
			this->_completeCv.notify_all();
			// Send stop command to all threads
			{
				std::lock_guard<std::mutex> lock(this->_actionLock);
				this->_action = STOP;
			}
			this->_actionCv.notify_all();
				
			// Wait for threads to finish
			for (int i=0;i<this->_threads.size();i++) {
				this->_threads[i].join();
			}

			this->_threads.clear();
		}

	private:
		/* @brief The action that threads in the pool perform until a joinall/STOP command. 
		 * @description Waits for RUN commands, performs action, then signals complete
		 * @param threadID	ID of this thread. Thread #1 is index 0
		 */
		void threadAction(ThreadID threadID) {
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
						std::cout << "Stop command received\n";
						break; // Stop the loop
					} 
				}

				// Do FFT
				this->_threadCallback(threadID);

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
