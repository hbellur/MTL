#ifndef _BASE_THREAD_HPP_
#define _BASE_THREAD_HPP_
/**
 * @file BaseThread.hpp
 * @class BaseThread
 * Abstract Base Class for a thread.
 *
 * Other threads (or even this thread) can post() a message to
 * instances of this class using post().
 * How messages are queued/stored is a derived class implementation detail.
 * However, messages are pointers to BaseMessage messages, which are not 
 * stored/copied (for efficiency reasons).  This has a few implications:
 * @li all messages must be 'new'ed or the messages must be in memory that will
 *     persist -- bascially, messages must not be created on the producer's stack
 * @li once a message is post()ed, it MUST NOT be used afterward by the producer.
 *     Ownership is transferred to the consumer of the queue (i.e., the owner
 *     of the queue)
 * @li the consumer must 'delete' the message when appropriate
 *
 * The consumer can downcast (using dynamic_cast) the pointers obtained from
 * the queue into the appropriate message type.
 *
 * When an object of this class is constructed, the thread is created in an
 * "invalid" state, called "not-a-thread", which will do nothing
 * until the thread object is start()ed.  start() will create an
 * operating system thread which will execute the run() method.
 * While the OS thread is running, the thread object refers to
 * "a thread of execution" instead of not-a-thread.
 *
 * When an object of this class receives an MTL_STOP
 * message, it should call stop(), which posts an MTL_CHILD_THREAD_STOPPED
 * message back to the _parent_thread.  It should then exit its run() method.
 * When the parent thread receives the MTL_CHILD_THREAD_STOPPED msg, it should
 * send a DELETE_THREAD message to the thread manager, after performing
 * any cleanup related to the child thread object.
 *
 * This class is based somewhat off of "Object method II" from
 * http://antonym.org/2009/05/threading-with-boost---part-i-creating-threads.html
 *
 * TBD: refactor message passing to use active objects instead:
 * http://www.drdobbs.com/parallel/prefer-using-active-objects-instead-of-n/225700095
 *
 */
#include <ostream>
#include <string>
#include <boost/date_time.hpp>
#ifdef WIN32
	#include <boost/thread.hpp>
#else
	#include <boost/thread/thread.hpp>
	#include <boost/thread/tss.hpp>
#endif
#include "MTL_Common.hpp"
class ThreadManager;

class BaseThread {
	// -----------------------------------------------------------------------------------
	// -- class variables
protected:
	/// unique thread identification string (stored in thread local storage)
	static boost::thread_specific_ptr<std::string> _unique_thread_str_;
	// -----------------------------------------------------------------------------------
	// -- class methods
public:
	/// unique thread identification string (from thread local storage)
	/// @return - unique thread identification string
	static const std::string& unique_thread_str() {
		return *_unique_thread_str_;
	}
	/// Sets the unique thread identification string (in thread local storage).
	/// This method is not thread safe and must only be called either
	/// before the thread starts (by only one caller thread) or only by this thread.
	/// @param unique_thread_str_ - unique thread identification string
	static void set_unique_thread_str(std::string const& unique_thread_str_) {
		_unique_thread_str_.reset(new std::string(unique_thread_str_));
	}
	// -----------------------------------------------------------------------------------
	// -- instance variables
protected:
	boost::thread		_thread;			///< the boost thread object
	const std::string	_thread_name;		///< unique identifier
	const bool			_has_stats;			///< true when this thread collects and reports stats
	BaseThread* const	_parent_thread;		///< _parent_thread is notified when run() exits
	// -----------------------------------------------------------------------------------
	// -- instance methods
public:
	/// Initializes member data.  Thread is created as "not-a-thread"
	/// @param[in] name_ 			- unique name for this thread
	/// @param[in] parent_thread_	- (optional) parent thead
	/// @param[in] has_stats_		- true when this thread has stats it can report
	BaseThread(std::string const& name_, BaseThread* parent_thread_ = 0,
		bool has_stats_ = false)
	: _thread()	// creates the thread in the "not-a-thread" state
	, _thread_name(name_)
	, _has_stats(has_stats_)
	, _parent_thread(parent_thread_) {}
	// --------------------------------------------------------------
	/// Does nothing
	virtual ~BaseThread() {}
	// --------------------------------------------------------------
	/// This thread's (hopefully unique) name.
	/// This method is thread-safe, since the name can only be set in the constructor.
	///  @return - this thread's name
	virtual std::string const& get_name() const {
		return _thread_name;
	}
	// --------------------------------------------------------------
	/// Indicates if this thread collects (and can report) stats.
	/// This method is thread-safe, since _has_stats can only be set in the constructor.
	///  @return - true if this thread collects (and can report) stats
	virtual bool has_stats() const {
		return _has_stats;
	}
	// --------------------------------------------------------------
	/// Blocks the calling thread until this object's thread has completed.
	/// If this object's thread has already completed, or if this
	/// object represents Not-a-Thread, then this method returns immediately.
	virtual void join() {
		_thread.join();
	}
	// --------------------------------------------------------------
	/// Indicates if the thread has stopped.
	/// @return - true if *this refers to a thread of execution, false otherwise.
	virtual bool joinable() const {
		return _thread.joinable();
	}
	// --------------------------------------------------------------
	/// Mimics boost:thread::try_join_for().
	/// @param[in] rel_time_ - relative time from now
	/// @return - true if *this refers to a thread of execution on entry,
	///  and that thread of execution has completed before the call times
	///  out, false otherwise
	virtual bool try_join_for(boost::chrono::milliseconds const& rel_time_) {
		return _thread.try_join_for(rel_time_);
	}
	// --------------------------------------------------------------
	/// This object's thread ID, or Not-any-thread if the thread is not running.
	/// @return - if this object refers to a thread of execution, an
	/// instance of boost::thread::id that represents this thread is
	/// returned. Otherwise, a default-constructed boost::thread::id
	/// is returned (and if you insert it into a stream, 
	/// you'll get '{Not-any-thread}').
	boost::thread::id id() const {
		return _thread.get_id();
	}
	// --------------------------------------------------------------
	virtual void start();
	// --------------------------------------------------------------
	/// This method is run when the OS thread is created.
	/// Because of the way we call this method (using Boost), it must
	/// be public.  However, it must not be called outside this class,
	/// since it is not thread-safe.
	virtual void run() = 0;
	// Sample implementation, assuming ConcurrentQueue is used:
	//	this->block_all_signals();
	//	MTL_LOG(BaseLogMessage::INFO, "ID=" << std::hex << this->id() << std::dec << " started");
	//	BaseMessage* msg_ptr = 0;
	//	bool should_run = true;
	//	while(should_run) {
	//		try {
	//			this->wait_and_pop(msg_ptr);
	//			switch(msg_ptr->_type) {
	//			case MTL_Thread_Message::MTL_STOP:
	//				MTL_LOG(BaseLogMessage::INFO, "STOP");
	//				should_run = false;
	//				break;
	//			case MTL_Thread_Message::MTL_KEEP_ALIVE_REQUEST: {
	//				KeepAliveRequestMessage const& ka = dynamic_cast<KeepAliveRequestMessage&>(*msg_ptr);
	//				ThreadManager::get_instance()->post(new KeepAliveResponseMessage(
	//					*this, ka._counter, time(0)));
	//				break;
	//			}
	//			default: {
	//				MTL_LOG(BaseLogMessage::WARNING, "UNKNOWN msg, type=" << msg_ptr->_type);
	//				break;
	//			}
	//		} catch(...) {
	//			ExceptionDispatcher::get_instance()->handle_exception();
	//		}
	//		delete msg_ptr;  // we own it, so we must delete it!
	//	}
	//	// do any final clean up here, or override stop()
	//	this->stop();
	// --------------------------------------------------------------
protected:
	virtual void stop();
	void block_all_signals() const;
	// --------------------------------------------------------------
public:
	/// Performs any critical cleanup before the program exits.
	/// Derived classes can override this method when needed.
	/// This method is (and must remain) thread-safe.
	virtual void abnormal_shutdown() {}
	// --------------------------------------------------------------
	/// Posts pointer msg_ptr_ to the back of this thread's input queue.
	/// The caller MUST NOT use msg_ptr_ after calling this method.
	/// Ownership of the message is transferred to the consumer of
	/// the input queue (i.e., the thread represented by this object).
	/// This method must be implemented in a thread-safe way, supporting
	/// multiple simultaneous callers.
	/// @param[in] msg_ptr_ - pointer to the data/message to be posted
	/// @return - true if msg_ptr_ was successfully posted.  False otherwise.
	virtual bool post(BaseMessage* const& msg_ptr_) = 0;
	// --------------------------------------------------------------
	/// Posts pointer msg_ptr_ to the front of this thread's input queue.
	/// The caller MUST NOT use msg_ptr_ after calling this method.
	/// Ownership of the message is transferred to the consumer of
	/// the input queue (i.e., the thread represented by this object).
	/// This method must be implemented in a thread-safe way, supporting
	/// multiple simultaneous callers.
	/// @param[in] msg_ptr_ - pointer to the data/message to be posted
	/// @return - true if msg_ptr_ was successfully posted.  False otherwise.
	virtual bool post_high_priority(BaseMessage* const& msg_ptr_) = 0;
	// --------------------------------------------------------------
	virtual void log(BaseLogMessage* const& log_msg_ptr_);
	// --------------------------------------------------------------
	// Any class that inherits from BaseThread is stream insertable.
	// Subclasses should override printOn().
	friend std::ostream& operator<< (std::ostream& os_, BaseThread const& bt_);
	// --------------------------------------------------------------
protected:
	virtual void printOn(std::ostream& os_) const;
};

#endif
