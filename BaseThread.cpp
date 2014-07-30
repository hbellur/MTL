/** @file BaseThread.cpp */
#include <signal.h>
#include "LoggerThread.hpp"
#include "ThreadManager.hpp"
#include "BaseThread.hpp"
// =======================================================================================
boost::thread_specific_ptr<std::string> BaseThread::_unique_thread_str_;
// =======================================================================================
/// Creates an operating system thread which executes run().
/// Notifies the thread manager of this new thread.
/*virtual*/ void BaseThread::start() {
	MTL_LOG(BaseLogMessage::INFO, "starting " << _thread_name);
	_thread = boost::thread(&BaseThread::run, this);
	ThreadManager::get_instance()->post(new ThreadCreatedMessage(*this));
}
// ---------------------------------------------------------------------------------------
/// Posts message CHILD_THREAD_STOPPED to the _parent_thread if possible,
/// otherwise posts message DELETE_THREAD to the thread manager.
/*virtual*/ void BaseThread::stop() {
	MTL_LOG(BaseLogMessage::INFO, "stopping");
	if(_parent_thread) {
		_parent_thread->post(new ThreadStoppedMessage(*this));
		// When the parent thread receives this message, it should do any cleanup (if it
		// hasn't already performed cleanup) required for this thread, and then it must
		// post a DELETE_THREAD message to the thread manager.
	} else {
		// notify the thread manager that it can delete this
		// thread, since no parent will do that
		MTL_LOG(BaseLogMessage::INFO,
			"sending DELETE_THREAD for self (no parent)");
		ThreadManager::get_instance()->post(new DeleteThreadMessage(*this));
	}
	MTL_LOG(BaseLogMessage::INFO, "stopped");
}
// ---------------------------------------------------------------------------------------
/// On Linux and Solaris, blocks all signals from being received by this thread.
void BaseThread::block_all_signals() const {
	#ifndef WIN32
		sigset_t new_mask, old_mask;
		// initialize the new signal mask
		sigfillset(&new_mask);
		// block all signals
		sigprocmask(SIG_SETMASK, &new_mask, &old_mask);
	#endif
}
// ---------------------------------------------------------------------------------------
/// Posts pointer log_msg_ptr_ to logger thread's input queue.
/// The caller MUST NOT use log_msg_ after calling this method.
/// Ownership of the message is transferred to the consumer of logger thread's queue
/// (i.e., the logger thread).
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
/// @param[in] log_msg_ptr_ - pointer to the log message to be logged
/*virtual*/ void BaseThread::log(BaseLogMessage* const& log_msg_ptr_) {
	LoggerThread::get_instance()->log(log_msg_ptr_);
}
// ---------------------------------------------------------------------------------------
/// Human readable representation of this object as a string.  Useful for debugging.
/// This method is currently thread-safe, since all instance variables that are included
/// are read-only.
/// @param[in] os_ - ostream into which the string will be inserted
/*virtual*/ void BaseThread::printOn(std::ostream& os_) const {
	os_	<< "BaseThread[name=" << _thread_name 
		<< ",thread id="	  << std::hex << this->id() << std::dec
		<< ",has_stats="	  << _has_stats
		<< ",parent_thread="  << _parent_thread
		// do not include ThreadManager
		<< ']';
}
// =======================================================================================
/// Converts bt_ to a string, then inserts that into stream os_
/// @param[in,out] os_ - the ostream into which bt_ will be inserted
/// @param[in]     bt_ - the object to be string converted and stream inserted
/// @return - os_ is returned for cascading (see C++ [15.11]:
///   How can I provide printing for an entire hierarchy of classes?)
std::ostream& operator<< (std::ostream& os_, BaseThread const& bt_) {
	bt_.printOn(os_);
	return os_;
}
