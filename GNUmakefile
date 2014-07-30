#ifndef _EXCEPTION_DISPATCHER_HPP_
#define _EXCEPTION_DISPATCHER_HPP_
/** @file ExceptionDispatcher.hpp
 * @class ExceptionDispatcher
 * A singleton that handles exceptions for the application.
 * Except for a bad_alloc exception, the exception is simply logged.
 * A bad_alloc exception results in an abnormal shutdown sequence.
 * To increase the probability that we can log to the Logger,
 * we pre-allocate some BaseLogMessages.
 */
#include <ostream>
#include <stack>
#ifdef WIN32
	#include <boost/thread.hpp>
#else
	#include <boost/thread/thread.hpp>
#endif
#include "MTL_Common.hpp"
class ThreadManager;

class ExceptionDispatcher {
	// -----------------------------------------------------------------------------------
	// -- constants
protected:
	enum {
		/// count of BaseLogMessages to preallocate
		BASE_LOG_MESSAGES_TO_PREALLOCATE_COUNT = 20
	};
	// -----------------------------------------------------------------------------------
	// -- class variables
protected:
	static ExceptionDispatcher* _instance_;	///< singleton object
	// -----------------------------------------------------------------------------------
	// -- class methods
public:
	/// If the single instance does not exist it is created.
	///	Note, this method is not thread-safe.  It must first be called by the main thread
	// before any other threads are started.  Then it can be considered thread-safe.
	/// @return - the single instance of this class
	static ExceptionDispatcher* get_instance() {
		if(_instance_ == 0) {
			_instance_ = new ExceptionDispatcher();
		}
		return _instance_;
	}
	// -----------------------------------------------------------------------------------
	// -- instance variables
protected:
	mutable boost::mutex			_mutex;				///< for accurate exception counting
	std::stack<BaseLogMessage *>	_cached_bad_alloc_log_messages; ///< cache of BaseLogMessages
	unsigned int					_bad_alloc_count;	///< count of bad_alloc exceptions
	unsigned int					_exception_count;	///< count of exceptions
	// -----------------------------------------------------------------------------------
	// -- instance methods
protected:
	/// Preallocates some BaseLogMessages
	ExceptionDispatcher()
	: _mutex()
	, _cached_bad_alloc_log_messages()
	, _bad_alloc_count(0)
	, _exception_count(0) {
		// pre-allocate a bunch of bad_alloc messages
		unsigned int i = 0;
		while(i < BASE_LOG_MESSAGES_TO_PREALLOCATE_COUNT) {
			_cached_bad_alloc_log_messages.push(
				new BaseLogMessage(BaseLogMessage::CRITICAL, "---", "bad_alloc!"));
			i++;
		}
	}
	// --------------------------------------------------------------
	virtual ~ExceptionDispatcher() {};
	// --------------------------------------------------------------
public:
	virtual void handle_exception() throw();  // will not throw any exceptions itself
	friend std::ostream& operator<< (std::ostream& os_, ExceptionDispatcher const& ed_);
protected:
	virtual void printOn(std::ostream& os_) const;
};

#endif
