#ifndef _CONCURRENT_QUEUE_HPP_
#define _CONCURRENT_QUEUE_HPP_
/** @file ConcurrentQueue.hpp
 * @class ConcurrentQueue
 * Thread-safe message queue, hence multiple producers and consumers are supported.
 * A high watermark is supported.  After crossing the watermark, no new messages
 * can be added to the queue, except for critical messages like keep-alive and
 * STOP (although there is a max limit even for these).
 *
 * Based on code from
 * http://www.justsoftwaresolutions.co.uk/threading/implementing-a-thread-safe-queue-using-condition-variables.html
 * and for timed_wait_and_pop():
 * http://www.justsoftwaresolutions.co.uk/threading/condition-variable-spurious-wakes.html
 *
 * Design consideration for methods try_pop() and wait_and_pop():
 * "Using a reference parameter to receive the result is used to transfer ownership out
 * of the queue in order to avoid the exception safety issues of returning data by-value:
 * if the copy constructor of a by-value return throws, then the data has been removed
 * from the queue, but is lost, whereas with this approach, the potentially problematic
 * copy is performed prior to modifying the queue (see Herb Sutter's Guru Of The
 * Week #8 for a discussion of the issues). This does, of course, require that an instance
 * of Data can be created by the calling code in order to receive the result, which is not
 * always the case. In those cases, it might be worth using something like
 * boost::optional to avoid this requirement."
 *
 */
#include <ostream>
#include <deque>
#include <vector>
#ifdef WIN32
	#include <boost/thread.hpp>
#else
	#include <boost/thread/thread.hpp>
#endif

// http://stackoverflow.com/questions/1297609/overloading-friend-operator-for-template-class
template<typename Data> class ConcurrentQueue;
template<typename Data>
  std::ostream& operator<< (std::ostream& os_, ConcurrentQueue<Data> const& cq_);

template<typename Data>
class ConcurrentQueue : boost::noncopyable {
	// -----------------------------------------------------------------------------------
	// -- constants
public:
	enum {
		// There is no "low" watermark.  As soon as the size of the queue goes below the
		// high watermark, a new message can be pushed onto the queue.  In essence then,
		// low watermark = high watermark - 1

		/// Default High watermark.  Clients can use a different value for the high
		/// watermark.  Once the high watermark is reached, non-critical messages
		/// can not be pushed on to the queue.
		DEFAULT_HIGH_WATERMARK = 500,
		/// Minimum high watermark
		MIN_HIGH_WATERMARK = 200,
		/// This is the number of critical messages that can be pushed onto the queue
		/// above the high watermark. In essense then,
		/// max queue size = high watermark + critical message count
		MAX_CRITICAL_MESSAGE_COUNT = 100,
		/// Every RESET_COUNT times push() is called, reset the counters.
		RESET_COUNT = 500
	};
	// -----------------------------------------------------------------------------------
	// -- instance variables
protected:
	size_t						_high_watermark;	///< once reached, only critical msgs can be posted
	size_t						_max_queue_size;	///< max # of msgs that can be in the queue
	size_t						_msg_drop_counter;	///< count of dropped messages; reset every RESET_COUNT pushes
	size_t						_push_counter;		///< count of pushed msgs; reset every RESET_COUNT pushes
	std::deque<Data>			_queue;				///< the actual queue
	mutable boost::mutex		_mutex;				///< for concurrency
	boost::condition_variable	_condition_variable;///< for efficient concurrency
	// -----------------------------------------------------------------------------------
	// -- instance methods
protected:
	/// Constructor.  Initializes member data
	explicit ConcurrentQueue(size_t high_watermark_ = DEFAULT_HIGH_WATERMARK)
	: _high_watermark(high_watermark_)
	, _max_queue_size(high_watermark_ + MAX_CRITICAL_MESSAGE_COUNT)
	, _msg_drop_counter(0)
	, _push_counter(0)
	, _queue()
	, _mutex()
	, _condition_variable() {}
	// --------------------------------------------------------------
	/// Does nothing.  Derived class must empty queue, following RAII.
	virtual ~ConcurrentQueue() {}
	// --------------------------------------------------------------
public:
	/// This queue's (hopefully unique) name
	virtual std::string const& get_name() const = 0;
	virtual unsigned int push_back(Data const& data_, bool critical_msg_);
	virtual unsigned int push_back_multiple(std::vector<Data> const& data_);
	virtual unsigned int push_front(Data const& data_, bool critical_msg_);
	bool   empty() const;
	size_t size() const;
	size_t high_watermark() const;
	size_t max_queue_size() const;
	size_t available_size() const;
	virtual void set_high_watermark(size_t high_watermark_);
	bool try_pop(Data& data_);
	bool timed_wait_and_pop(Data& data_, boost::system_time const& abs_time);
	void wait_and_pop(Data& data_);
	// http://stackoverflow.com/questions/1297609/overloading-friend-operator-for-template-class
	friend std::ostream& operator<< <> (std::ostream& os_, ConcurrentQueue<Data> const& cq_);
protected:
	virtual void printOn(std::ostream& os_) const;
};

#endif
