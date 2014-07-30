/** @file ExceptionDispatcher.cpp */

#include "ThreadManager.hpp"
#include "LoggerThread.hpp"
#include "ExceptionDispatcher.hpp"
// =======================================================================================
ExceptionDispatcher* ExceptionDispatcher::_instance_ = 0;
// ---------------------------------------------------------------------------------------
/// Application-wide place to handle exceptions that are not caught elsewhere.
/// This method is modeled off
///   http://www.parashift.com/c++-faq-lite/exceptions.html#faq-17.15
/*virtual*/ void ExceptionDispatcher::handle_exception() throw() {  
											// will not throw any exceptions itself
	boost::mutex::scoped_lock lock(_mutex);
	_exception_count++;
	LoggerThread& logger = *(LoggerThread::get_instance());
	try {
		throw;  // re-throw exception so we can catch it
	} catch(std::bad_alloc const& x_) {
		_bad_alloc_count++;
		// This is bad, really bad.  We failed allocating memory...
		// other failures will likely occur.
		// Queue sizes may need to be adjusted down so this doesn't happen again in the
		// future.  Or we may need to track the number of active threads better, etc.
		// We're doing to attempt a shutdown of the program.
		// Notify the thread manager, who will notify the main thread, and call 
		// abnormal_shutdown() on all active threads.  They may try to do any critical
		// shutdown operations, like closing open files.
		ThreadManager::get_instance()->abnormal_shutdown();
		// Note however that this program will continue to run (or at least it will try to
		// run), since we caught the exception.  There is really no guarantee anything
		// will shut down clean.
		// Since we probably can't allocate memory to log to the Logger, see if we have
		// any pre-allocated messages left.  But even if we do, post() may fail, since the
		// Logger uses ConcurrentQueue, which uses push_back().
		if(!_cached_bad_alloc_log_messages.empty()) {
			logger.log(_cached_bad_alloc_log_messages.top());
			_cached_bad_alloc_log_messages.pop();
		} else {
			// well, we're going to try and allocate memory anyway
			MTL_LOG(BaseLogMessage::CRITICAL,
				"bad_alloc count=" << _bad_alloc_count << " :" << x_.what());
		}
	} catch(std::bad_cast const& x_) {
		logger.log(new BaseLogMessage(BaseLogMessage::ERROR, "---", x_.what()));
	} catch(std::bad_exception const& x_) {
		logger.log(new BaseLogMessage(BaseLogMessage::ERROR, "---", x_.what()));
	} catch(std::logic_error const& x_) {
		logger.log(new BaseLogMessage(BaseLogMessage::ERROR, "---", x_.what()));
	} catch(std::runtime_error const& x_) {
		logger.log(new BaseLogMessage(BaseLogMessage::ERROR, "---", x_.what()));
	} catch(std::exception const& x_) {
		logger.log(new BaseLogMessage(BaseLogMessage::ERROR, "---", x_.what()));
	} catch(...) {
		logger.log(new BaseLogMessage(BaseLogMessage::ERROR, "---", "unknown exception"));
	}
	// not included above (there may be others):
	// - bad_typeid        - thrown by typeid
	// - ios_base::failure - thrown by iostream library
}
// ---------------------------------------------------------------------------------------
/// Human readable representation of this object as a string.  Useful for debugging.
/// @param[in] os_ - ostream into which the string will be inserted
/*virtual*/ void ExceptionDispatcher::printOn(std::ostream& os_) const {
	boost::mutex::scoped_lock lock(_mutex);
	os_	<< "ExceptionDispatcher[exception_count=" << _exception_count
		<< ",bad_alloc_count="					  << _bad_alloc_count
		<< ",cached_bad_alloc_log_messages left=" << _cached_bad_alloc_log_messages.size()
		// do not include ThreadManager
		<< ']';
}
// =======================================================================================
/// Converts ed_ to a string, then inserts that into stream os_
/// @param[in,out] os_ - the ostream into which bt_ will be inserted
/// @param[in]     ed_ - the object to be string converted and stream inserted
/// @return - os_ is returned for cascading
std::ostream& operator<< (std::ostream& os_, ExceptionDispatcher const& ed_) {
	ed_.printOn(os_);
	return os_;
}
