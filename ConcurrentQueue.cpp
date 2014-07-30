/** @file ConcurrentQueue.cpp */
#include "ConcurrentQueue.hpp"

// ---------------------------------------------------------------------------------------
/// Queue empty state. Use this method if you don't already have the _mutex locked -- e.g.,
/// all external clients.
/// @return - true if this queue is empty, false otherwise
template<typename Data>
bool ConcurrentQueue<Data>::empty() const {
	boost::mutex::scoped_lock lock(_mutex);
	return _queue.empty();
}
// ---------------------------------------------------------------------------------------
/// Queue size.  Use this method if you don't already have the _mutex locked -- e.g.,
/// all external clients.
/// @return - size of (count of messages in) this queue
template<typename Data>
size_t ConcurrentQueue<Data>::size() const {
	boost::mutex::scoped_lock lock(_mutex);
	return _queue.size();
}
// ---------------------------------------------------------------------------------------
/// High watermark.  Use this method if you don't already have the _mutex locked -- e.g.,
/// all external clients.
/// @return - high watermark, above which only critical messages can be stored
template<typename Data>
size_t ConcurrentQueue<Data>::high_watermark() const {
	boost::mutex::scoped_lock lock(_mutex);
	return _high_watermark;
}
// ---------------------------------------------------------------------------------------
/// Max queue size.  Use this method if you don't already have the _mutex locked -- e.g.,
/// all external clients.
/// @return - max size this queue can grow to
template<typename Data>
size_t ConcurrentQueue<Data>::max_queue_size() const {
	boost::mutex::scoped_lock lock(_mutex);
	return _max_queue_size;
}
// ---------------------------------------------------------------------------------------
/// Available queue size.  Use this method if you don't already have the _mutex locked -- 
/// e.g., all external clients.
/// @return - number of slots available in the queue
template<typename Data>
size_t ConcurrentQueue<Data>::available_size() const {
	boost::mutex::scoped_lock lock(_mutex);
	return _high_watermark - _queue.size();
}
// ---------------------------------------------------------------------------------------
/// Set the high watermark.
/// @param[in] high_watermark_ - new high watermark value
template<typename Data>
void ConcurrentQueue<Data>::set_high_watermark(size_t high_watermark_) {
	boost::mutex::scoped_lock lock(_mutex);
	_high_watermark = high_watermark_;
	_max_queue_size = high_watermark_ + MAX_CRITICAL_MESSAGE_COUNT;
}
// ---------------------------------------------------------------------------------------
///	If this queue is empty, returns false.  Otherwise, pops and populates data_ with the
/// item from the top of this queue.
/// This method does not block, and can be used when polling is required.
/// @param[out] data_ - if true is returned, contains the data/message from the top of
///   this queue
/// @return - true if there was an item in the queue and it was copied
///   into data_;  false if the queue is empty
template<typename Data>
bool ConcurrentQueue<Data>::try_pop(Data& data_) {
	boost::mutex::scoped_lock lock(_mutex);
	if(_queue.empty()) {
		return false;
	}
	data_ = _queue.front();
	_queue.pop_front();
	return true;
}
// ---------------------------------------------------------------------------------------
/// If this queue is empty, waits for absolute_time to be reached.
/// If the queue becomes non-empty, pops and populates data_ with the item from the top of
/// this queue.  If the queue remains empty, returns false.
/// This method blocks until a message is received or absolute_time is reached.
/// @param[out] data_		 - if true is returned, contains the data/message from the
///   top of this queue
/// @param[in] absolute_time - absolute time to wait up until
/// @return - true if there was (or eventually was) an item in the queue
///   and it was copied into data_;  false if the queue remained empty.
template<typename Data>
bool ConcurrentQueue<Data>::timed_wait_and_pop(Data& data_, boost::system_time const& absolute_time) {
	boost::mutex::scoped_lock lock(_mutex);
	while(_queue.empty()) {
		// timed_wait() atomically unlocks _mutex and adds the calling thread
		// to the operating system's wait queue
		if(!_condition_variable.timed_wait(lock, absolute_time)) {
			return false;
		}
	}
	data_ = _queue.front();
	_queue.pop_front();
	return true;
}
// ---------------------------------------------------------------------------------------
/// If this queue is empty, blocks on the condition variable until the condition variable
/// is signaled.  Once this queue is not empty, pops and populates data_ with the
/// item from the top of this queue.
/// This method blocks.
/// @param[out] data_ - the data/message from the top of this queue
template<typename Data>
void ConcurrentQueue<Data>::wait_and_pop(Data& data_) {
	boost::mutex::scoped_lock lock(_mutex);
	// use a while loop to handle suprious wakes
	while(_queue.empty()) {
		_condition_variable.wait(lock);
		// wait() atomically unlocks _mutex and adds the calling thread to the operating
		// system's wait queue
	}
	data_ = _queue.front();
	_queue.pop_front();
}
// ---------------------------------------------------------------------------------------
/// Places a copy of data_ onto the back of this queue, and signals any consumers via the
/// _condition_variable.  However, for non-critical messages, if the high watermark is
/// (or was) reached, data_ is not placed onto the queue.
/// For critical messages, if the queue is full, data_ is not placed onto the queue.
/// @param[in] data_		 - the data/message to copy into this queue
/// @param[in] critical_msg_ - true if the message is a "critical" message
/// @return - 0 when data_ is added to the queue,
/// 		  value of _msg_drop_counter when data_ is not added.
template<typename Data>
/*virtual*/ unsigned int ConcurrentQueue<Data>::push_back(Data const& data_, bool critical_msg_) {
	size_t result = 0;
	boost::mutex::scoped_lock lock(_mutex);
	size_t const size = _queue.size();
	if(size < _high_watermark) {	// normal, most common, case - there is room
		_queue.push_back(data_);	// may throw bad_alloc
	} else {						// Q is at or over the high watermark
		if(critical_msg_) {
			if(size < _max_queue_size) {
				// good, there is room for the critical message
				_queue.push_back(data_);
			} else {
				// bad, the queue is full, the critical msg cannot be pushed!
				_msg_drop_counter++;
				result = _msg_drop_counter;
			}
		} else {					// do not push the non-critical message
			_msg_drop_counter++;
			result = _msg_drop_counter;
		}
	}
	_push_counter++;
	if(_push_counter >= RESET_COUNT) {
		// Reset the counters.  Clients can notice the reset in order to periodically log 
		_msg_drop_counter = 0;
		_push_counter = 0;
	}
	// unlock first, so that when the waiting thread is notified, the mutex will be available
	lock.unlock();
	// if we did not push the message, we do not need to signal the consumers, but maybe
	// we should anyway, since the queue is over the watermark
	_condition_variable.notify_one();
	return result;
}
// ---------------------------------------------------------------------------------------
/// Places a copy of each Data item in data_ onto the back of this queue, and signals any
/// consumers via the _condition_variable.
/// All messages are put onto the queue, or, if there is not enough space (i.e., the high
/// watermark would be reached or exceeded), no messages are put onto the queue.
/// data_ may not contain any critical messages.
/// @param[in] data_ - the data/messages to copy into this queue
/// @return	- 0 when data_ is added to the queue,
///			  value of _msg_drop_counter when data_ is not added.
template<typename Data>
/*virtual*/ unsigned int ConcurrentQueue<Data>::push_back_multiple(std::vector<Data> const& data_) {
	size_t result = 0;
	size_t const data_size = data_.size();
	boost::mutex::scoped_lock lock(_mutex);
	size_t const size = _queue.size();
	if(size <= (_high_watermark - data_size) ) {	// normal, most common, case - there is room
		_queue.insert(_queue.end(), data_.begin(), data_.end());	// may throw bad_alloc
	} else {	// Q is at or over, or would go over, the high watermark
		_msg_drop_counter += data_size;
		result = _msg_drop_counter;
	}
	_push_counter += data_size;
	if(_push_counter >= RESET_COUNT) {
		// Reset the counters.  Clients can notice the reset in order
		// to periodically log 
		_msg_drop_counter = 0;
		_push_counter = 0;
	}
	// unlock first, so that when the waiting thread is notified,
	// the mutex will be available
	lock.unlock();
	// if we did not push the messages, we do not need to signal
	// the consumers, but maybe we should anyway, since the queue
	// is over the watermark
	_condition_variable.notify_one();
	return result;
}
// ---------------------------------------------------------------------------------------
/// Makes a copy of data_, places it onto the front of this queue, and signals any
/// consumers via the _condition_variable.
/// However, for non-critical messages, if the high watermark is (or was) reached, data_
/// is not placed onto the queue.
/// For critical messages, if the queue is full, data_ is not placed onto the queue.
/// @param[in] data_ - the data/message to copy into this queue
/// @param[in] critical_msg_ - true if the message is a "critical" message
/// @return - 0 when data_ is added to the queue,
/// 		  value of _msg_drop_counter when data_ is not added.
template<typename Data>
/*virtual*/ unsigned int ConcurrentQueue<Data>::push_front(Data const& data_, bool critical_msg_) {
	size_t result = 0;
	boost::mutex::scoped_lock lock(_mutex);
	size_t const size = _queue.size();
	if(size < _high_watermark) {	// normal, most common, case - there is room
		_queue.push_front(data_);	// may throw bad_alloc
	} else {						// Q is at or over the high watermark
		if(critical_msg_) {
			if(size < _max_queue_size) {
				// good, there is room for the critical message
				_queue.push_front(data_);
			} else {
				// bad, the queue is full, the critical msg cannot be pushed!
				_msg_drop_counter++;
				result = _msg_drop_counter;
			}
		} else {					// do not push the non-critical message
			_msg_drop_counter++;
			result = _msg_drop_counter;
		}
	}
	_push_counter++;
	if(_push_counter >= RESET_COUNT) {
		// Reset the counters.  Clients can notice the reset in order
		// to periodically log 
		_msg_drop_counter = 0;
		_push_counter = 0;
	}
	// unlock first, so that when the waiting thread is notified,
	// the mutex will be available
	lock.unlock();
	// if we did not push the message, we do not need to signal
	// the consumers, but maybe we should anyway, since the queue
	// is over the watermark
	_condition_variable.notify_one();
	return result;
}
// ---------------------------------------------------------------------------------------
/// Human readable representation of this object as a string. Useful for debugging.
/// @param[in] os_ - the ostream into which the string will be inserted
template<typename Data>
/*virtual*/ void ConcurrentQueue<Data>::printOn(std::ostream& os_) const {
	boost::mutex::scoped_lock lock(_mutex);
	os_	<< "ConcurrentQueue[size="	<< _queue.size()
		<< ",max_queue_size="		<< _max_queue_size
		<< ",high_watermark="		<<_high_watermark
		<< ",msg_drop_counter="		<< _msg_drop_counter
		<< ",push_counter="			<< _push_counter
		<< ']';
}
// =======================================================================================
/// Converts cq_ to a string, then inserts that into stream os_
/// @param[in,out] os_ - ostream into which cq_ will be inserted
/// @param[in]     cq_ - object to be string converted and stream inserted
/// @return - os_ is returned for cascading (see C++ FAQ 15.4)
template<typename Data>
std::ostream& operator<< (std::ostream& os_, ConcurrentQueue<Data> const& cq_) {
	cq_.printOn(os_);
	return os_;
}
