#ifndef _THREAD_MANAGER_HPP_
#define _THREAD_MANAGER_HPP_
/** @file ThreadManager.hpp
 * @class ThreadManager
 * ThreadManager is a Singleton abstract base class (ABC).
 * The ThreadManager periodically sends keep-alive messages to all application threads.
 * It must be notified when a thread is created and when a thread is exiting.
 * It waits for exiting threads to finish executing.
 * It can notify all (non-exiting) threads to dump themselves to the INFO log.
 * It participates in abnormal application shutdown.
 * It maintains timers for threads that subscribe and notifies the subscriber
 * threads when the timers expire. Applications using ThreadManager to notify
 * timer expiration must understand the impact on its main functionality, mentioned
 * above.
 * 
 */
#include <ostream>
#include <map>
#include <ctime>
#include "ThreadWithInputQueue.hpp"

class ThreadManager : public virtual ThreadWithInputQueue {
	// -----------------------------------------------------------------------------------
	// -- constants
protected:
	enum {
		/// timeout value in seconds for timed_wait_and_pop(). Currently, it is
		/// set to 1 second. Changes to the code is required, if this value
		/// changes
		TIMED_AND_WAIT_AND_POP_TIMEOUT_IN_SECONDS = 1,
		/// number of seconds in a minute
		SECONDS_IN_A_MINUTE = 60,
		/// when keep-alive miss count reaches this value, log an error
		FIRST_LOG_KEEP_ALIVE_MISS_COUNT = 3,
		/// when keep-alive miss count reaches this value periodically, log an error
		PERIODIC_LOG_KEEP_ALIVE_MISS_COUNT = 30,
		/// in milliseconds, maximum time to wait for an exiting thread to exit
		TIMED_JOIN_PERIOD = 100,
		/// max keep-alive periods to wait for all threads (except Logger) to exit
		STOP_GUARD_KEEP_ALIVE_COUNT = 4,
		/// max thread input queue size, sized to handle at least 1000 threads
		MAX_QUEUE_SIZE = 5000,
	};
	/// thread states that the thread manager keeps track of
	enum TM_ThreadState {
		ACTIVE = 1,	///< OS thread is running
		EXITING		///< waiting for OS thread to exit/terminate
	};
	// -----------------------------------------------------------------------------------
	// -- typedefs, local structs
protected:
	/// Timer subscription information the thread manager needs to know about each thread
	struct ThreadTimerInfo {
		const bool	_periodic_timer;		///< true, if this is a periodic timer. false, if it is a one shot timer
		const int		_thread_specific_timer_type;	///< unique value that helps threads to identify the timer
		const int 	_timer_in_seconds;	///< must be >= keep alive period value the application has set. 	
		int					_elapsed_time_in_seconds;	///< counter indicates the time elapsed, in seconds
		int					_day_count;							///< days since Jan 1 (0-365)
	};
	/// typedef for map of MTL_Timer_Type::Timer_Type to ThreadTimerInfo
	/// This is a multimap because a thread can subscribe to more than one 
	/// timers for the same MTL_Timer_Type::Timer_Type.
	/// ex: A thread has subscribed to 2 periodic EVENT_RELATIVE_TIMERs wherein
	///     the thread wants to be notified every 10 minutes and 30 minutes, from
	///			the application start time.
	typedef std::multimap<MTL_Timer_Type::Timer_Type, ThreadTimerInfo> ThreadTimerInfoMap;	
	/// Information the thread manager needs to know about each thread in the system
	struct ThreadInfo {
		TM_ThreadState	_state;												///< current thread state
		bool						_keep_alive_response_pending;	///< true when a keep-alive response is pending
		time_t					_keep_alive_timestamp;				///< records when keep-alive was sent to a thread
		unsigned int		_keep_alive_counter;					///< acts like a unique ID for each keep-alive sent
		unsigned int		_keep_alive_missed_count;			///< count of missed keep-alives
		ThreadTimerInfoMap	_thread_timer_map;				///< map of ThreadTimerInfo containing timer subscription
																									///< for each MTL_Timer_Type::Timer_Type																									
	};
	/// typedef for map of threads in the application
	typedef std::map<BaseThread*, ThreadInfo> ThreadMap;
		// can't use const* here because STL map needs an assignment operator
	// -----------------------------------------------------------------------------------
	// -- class variables
protected:
	static ThreadManager* _instance_;			///< singleton object
public:
	// use char* instead of std::string to ensure static initialization, see
	// http://stackoverflow.com/questions/459942/defining-class-string-constants-in-c
	static const char* const _logger_thread_name_;		///< logger thread's unique name
	// -----------------------------------------------------------------------------------
	// -- class methods
public:
	/// A derived class will need to implement this method.
	static ThreadManager* get_instance(); // { return _instance_; };
	 // -----------------------------------------------------------------------------------
	// -- instance variables
protected:
	ThreadMap	 _threads;							///< application threads
	time_t	_last_keep_alives_sent_timestamp;	///< records when keep-alives were last sent to all threads
	bool		_exiting_thread_still_running;		///< true when at least one exiting thread is still running
	unsigned int _stop_guard_counter;				///< keep-alive-based counter for stopping
	bool		_stop_pending;						///< true if this thread is trying to stop
	bool		_abnormal_shutdown_called;			///< true after the first abnormal shutdown event
	BaseMessage* _abnormal_shutdown_message;		///< cached message
	// -----------------------------------------------------------------------------------
	// -- instance methods
protected:
	/// Constructor.  Creates/caches one ABNORMAL_SHUTDOWN message.
	/// @param[in] thread_name_ - unique name for this thread
	explicit ThreadManager(std::string const& thread_name_)
	: BaseThread(thread_name_)
	, ConcurrentQueue<BaseMessage*>::ConcurrentQueue(MAX_QUEUE_SIZE)
	, ThreadWithInputQueue(thread_name_)
	, _threads()
	, _last_keep_alives_sent_timestamp(time(0))
	, _exiting_thread_still_running(false)
	, _stop_guard_counter(0)
	, _stop_pending(false)
	, _abnormal_shutdown_called(false)
	, _abnormal_shutdown_message(0) {
		_abnormal_shutdown_message = new BaseMessage(MTL_Thread_Message::MTL_ABNORMAL_SHUTDOWN);
	}
	// --------------------------------------------------------------
public:
	virtual ~ThreadManager() {
		if (!_abnormal_shutdown_called) {
			// an abnormal shutdown never occurred, so free this message
			delete _abnormal_shutdown_message;
		}
	}
	// --------------------------------------------------------------
public:
	virtual void start();
	virtual void run();
	virtual void abnormal_shutdown();
	virtual unsigned int keep_alive_period() const = 0;
protected:
	/// A serious thread error has occurred.
	/// Projects may choose to log the included message and/or send a trap.
	/// @param[in] thread_error_type_ - error type
	/// @param[in] msg_				  - error message
	virtual void thread_error(MTL_Thread_Error::ThreadError thread_error_type_,
		std::string const& thread_name_, std::string const& msg_) const = 0;
	virtual void stop();
	/// Notifies main thread to dump all objects
	virtual void dump_objects() const = 0;
	virtual void process_keep_alives();
	virtual void check_exiting_threads();
	/// Notifies main thread of abnormal shutdown
	virtual void notify_main_thread_of_abnormal_shutdown() const = 0;
	/// MTL_Event methods
	virtual void check_timer_expiry();
	virtual void update_time_elapsed_and_notify_subscriber_threads(BaseThread& app_thread_, ThreadTimerInfoMap& thread_timer_map_);
	/// Handles additional message types
	/// @param[in] msg_ptr_ - input message
	virtual void process_input_message(BaseMessage *msg_ptr_) = 0;
	virtual void report_thread_queue_info(ThreadQueueInfoMessage const& tqi_msg_) const;
	virtual void printOn(std::ostream& os_) const;
};

#endif
