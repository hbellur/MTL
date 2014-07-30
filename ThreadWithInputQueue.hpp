#ifndef _THREAD_WITH_INPUT_QUEUE_HPP_
#define _THREAD_WITH_INPUT_QUEUE_HPP_
/** @file ThreadWithInputQueue.hpp  */
#include <ostream>
#include <string>
#include "BaseThread.hpp"
#include "ConcurrentQueue.hpp"
class ThreadManager;

// Non-public inheritance is used for ConcurrentQueue to model is-implemented-in-terms-of
// (or has-a): http://www.gotw.ca/publications/mill06.htm
// http://www.parashift.com/c++-faq-lite/private-inheritance.html
// Containment (another is-implemented-in-terms-of approach) is not used to allow virtual
// function ConcurrentQueue::push() to be overridden (by derived class LoggerThread), and
// hence protected, rather than private, inheritance is used.

/// Abstract Base Class for a thread with an input queue.
class ThreadWithInputQueue : public    virtual BaseThread,
							 protected virtual ConcurrentQueue<BaseMessage *> {
	// -----------------------------------------------------------------------------------
	// -- instance methods
public:
	/// Constructor.  Initializes member data.  Thread is created as "not-a-thread"
	/// @param[in] name_ 			- unique name for this thread
	/// @param[in] parent_thread_	- (optional) parent thead
	/// @param[in] has_stats_		- true when this thread has stats it can report
	ThreadWithInputQueue(std::string const& thread_name_, BaseThread* parent_thread_ = 0,
		bool has_stats_ = false)
	: BaseThread(thread_name_, parent_thread_, has_stats_)
	, ConcurrentQueue<BaseMessage *>() {}
	// -----------------------------------------------------------------------------------
	/// Empties queue, following RAII, http://en.wikipedia.org/wiki/RAII
	virtual ~ThreadWithInputQueue() { 
		if(this->size() != 0) {
			// empty the queue and free the memory so that we don't leak memory
			BaseMessage* msg_ptr = 0;
			while(this->try_pop(msg_ptr)) {
				delete msg_ptr;
			}
		}
	}
protected:
	virtual void stop();
	virtual void printOn(std::ostream& os_) const;
public:
// ---------------------------------------------------------------------------------------
/// This thread's (hopefully unique) name.
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
/// implements ConcurrentQueue
/// overrides  BaseThread
/// @return - this thread's name
virtual std::string const& get_name() const {
	return BaseThread::get_name();
};
	virtual void set_high_watermark(size_t high_watermark_);
	virtual bool post(BaseMessage* const& msg_ptr_);
	virtual bool post_high_priority(BaseMessage* const& msg_ptr_);
};

#endif
