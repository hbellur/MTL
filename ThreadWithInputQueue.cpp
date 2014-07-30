/** @file ThreadWithInputQueue.cpp
 * Logs an error if a push() fails for a critical message.
 * Logs an error periodically (once every LOG_FAILED_PUSH_AGAIN_COUNT failures)
 * if a push() fails for a non-critical message.
 */
#include "LoggerThread.hpp"
#include "ThreadWithInputQueue.hpp"
#include "ThreadManager.hpp"
// ---------------------------------------------------------------------------------------
/// Posts message CHILD_THREAD_STOPPED to the _parent_thread if possible, otherwise posts
/// message DELETE_THREAD to the thread manager.
/// This method is not thread-safe, and hence must only be called in the context of this
/// thead's run() method.
/// overrides - checks for empty queue first
/*virtual*/ void ThreadWithInputQueue::stop() {
	if(this->size() != 0) {
		// There are rare cases where this can happen, e.g., the ThreadManager sends a
		// keep-alive request very soon after this method is called.  Log it, since it
		// could reveal a problem.
		// Following RAII, the queue will be emptied in the destructor.
		MTL_LOG(BaseLogMessage::INFO,
			"stop() called, but input queue was not empty. Q size="
			<< this->size());
	}
	this->BaseThread::stop();
}
// ---------------------------------------------------------------------------------------
/// Sets the high watermark for the input queue to either high_watermark_ or
/// MIN_HIGH_WATERMARK, whichever is greater.
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
/// overrides - to prevent Q size from being set to less than MIN_HIGH_WATERMARK
/// @param[in] high_watermark_ - new high watermark value
/*virtual*/ void ThreadWithInputQueue::set_high_watermark(size_t high_watermark_) {
	const size_t hwm = ( (high_watermark_ < ConcurrentQueue<BaseMessage *>::MIN_HIGH_WATERMARK)
		? ConcurrentQueue<BaseMessage *>::MIN_HIGH_WATERMARK
		: high_watermark_);
	this->ConcurrentQueue<BaseMessage *>::set_high_watermark(hwm);
}
// ---------------------------------------------------------------------------------------
/// If there is room, posts pointer msg_ptr_ to the back of this thread's input queue, and
/// signals the thread via the _condition_variable.  If the message cannot be posted, it
/// is deleted, and a warning or error may be logged, and an SNMP trap may be sent. \n
/// The caller MUST NOT use msg_ptr_ after calling this method.
/// Ownership of the message is transferred to the consumer of the input queue (i.e., the
/// thread represented by this object).
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
/// NOTE: changes made to this method likely require similar changes to overridden methods
/// such as LoggerThread::post() and SomeProjectSpecificThread::post().
/// @param[in] msg_ptr_ - pointer to the data/message to be posted
/// @return - true if msg_ptr_ was successfully posted.  False otherwise.
/*virtual*/ bool ThreadWithInputQueue::post(BaseMessage* const& msg_ptr_) {
	const bool critical_msg = (msg_ptr_->_type < MTL_Thread_Message::MTL_CRITICAL_MESSAGE_END);
	const unsigned int msg_drop_counter = this->push_back(msg_ptr_, critical_msg);
	if(msg_drop_counter) {
		// push failed
		if(critical_msg) {
			std::ostringstream oss;
			oss << _thread_name << "'s input queue dropped CRITICAL message of type "
				<< msg_ptr_->_type << "; Q size=" << this->size();
				// note that size() makes a mutex call
			ThreadManager::get_instance()->post(new ThreadErrorMessage(
				MTL_Thread_Error::INPUT_QUEUE_DROPPED_CRITICAL_MSG, 
				BaseThread::unique_thread_str(), oss.str()));
		} else {
			if(msg_drop_counter == 1) {
				// The watermark has been crossed for the first time, or the first
				// time since the drop counter was reset, so trap/log.
				std::ostringstream oss;
				oss << _thread_name << "'s input queue dropped at least one message"
					"; Q size=" << this->size();
					// note that size() makes a mutex call
				ThreadManager::get_instance()->post(new ThreadErrorMessage(
					MTL_Thread_Error::INPUT_QUEUE_DROPPED_MSG,
					BaseThread::unique_thread_str(), oss.str()));
			}
		}
		delete msg_ptr_;
		return false;
	}
	return true;
}
// ---------------------------------------------------------------------------------------
/// If there is room, posts pointer msg_ptr_ to the front of this thread's input queue,
/// and signals the thread via the _condition_variable.  If the message cannot be posted,
/// it is deleted, and a warning or error may be logged, and an SNMP trap may be sent. \n
/// The caller MUST NOT use msg_ptr_ after calling this method.
/// Ownership of the message is transferred to the consumer of the input queue (i.e., the
/// thread represented by this object).
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
/// NOTE: changes made to this method likely require similar changes to overridden methods
/// such as LoggerThread::post() and SomeProjectSpecificThread::post().
/// @param[in] msg_ptr_ - pointer to the data/message to be posted
/// @return - true if msg_ptr_ was successfully posted.  False otherwise.
/*virtual*/ bool ThreadWithInputQueue::post_high_priority(BaseMessage* const& msg_ptr_) {
	const bool critical_msg = (msg_ptr_->_type < MTL_Thread_Message::MTL_CRITICAL_MESSAGE_END);
	const unsigned int msg_drop_counter = this->push_front(msg_ptr_, critical_msg);
	if(msg_drop_counter) {
		// push failed
		if(critical_msg) {
			std::ostringstream oss;
			oss << _thread_name << "'s input queue dropped CRITICAL message of type "
				<< msg_ptr_->_type << "; Q size=" << this->size();
				// note that size() makes a mutex call
			ThreadManager::get_instance()->post(new ThreadErrorMessage(
				MTL_Thread_Error::INPUT_QUEUE_DROPPED_CRITICAL_MSG,
				BaseThread::unique_thread_str(), oss.str()));
		} else {
			if(msg_drop_counter == 1) {
				// The watermark has been crossed for the first time, or the first
				// time since the drop counter was reset, so trap/log.
				std::ostringstream oss;
				oss << _thread_name << "'s input queue dropped at least one message"
					"; Q size=" << this->size();
					// note that size() makes a mutex call
				ThreadManager::get_instance()->post(new ThreadErrorMessage(
					MTL_Thread_Error::INPUT_QUEUE_DROPPED_MSG,
					BaseThread::unique_thread_str(), oss.str()));
			}
		}
		delete msg_ptr_;
		return false;
	}
	return true;
}
// ---------------------------------------------------------------------------------------
/// Human readable representation of this object as a string.  Useful for debugging.
/// This method is currently thread-safe, since BaseThread's and ConcurrentQueue's
/// printOn() methods are currently thread-safe.
/// @param[in] os_ - ostream into which the string will be inserted
/*virtual*/ void ThreadWithInputQueue::printOn(std::ostream& os_) const {
	os_	<< "ThreadWithInputQueue[\n  ";
	this->BaseThread::printOn(os_);
	os_ << "\n  ";
	this->ConcurrentQueue<BaseMessage *>::printOn(os_);
	os_	<< ']';
}
