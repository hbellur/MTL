/** @file ThreadManager.cpp */
#include <vector>
#include "LoggerThread.hpp"
#include "ExceptionDispatcher.hpp"
#include "LoggerThread.hpp"
#include "ThreadManager.hpp"
#include "MTL_Common.hpp"
#include "ConcurrentQueue.cpp"  // need to #include cpp file to get templatized definitions


// =======================================================================================
ThreadManager* ThreadManager::_instance_				= 0;
const char* const ThreadManager::_logger_thread_name_	= "Logger";
// =======================================================================================
/// Creates an operating system thread which executes run().
/// overrides - does not notify the thread manager of itself
/*virtual*/ void ThreadManager::start() {
	MTL_LOG(BaseLogMessage::INFO, "starting " << _thread_name);
	_thread = boost::thread(&ThreadWithInputQueue::run, this);
}
// ---------------------------------------------------------------------------------------
/// Processes this thread's input queue.
/// Periodically sends keep-alives to all threads.
/*virtual*/ void ThreadManager::run() {
	this->block_all_signals();
	BaseThread::set_unique_thread_str(_thread_name);
	MTL_LOG(BaseLogMessage::INFO, " ID=" << std::hex << this->id() << std::dec << " started");
	const boost::posix_time::time_duration timeout_period(0, 0, TIMED_AND_WAIT_AND_POP_TIMEOUT_IN_SECONDS);
	BaseMessage* msg_ptr = 0;
	bool should_run = true;
	const unsigned int keep_alive_period = this->keep_alive_period();
	unsigned int timeout_counter = 0;
	boost::system_time timeout = boost::get_system_time() + timeout_period;
	while(should_run) {
		try {
			if(!this->timed_wait_and_pop(msg_ptr, timeout)) {
				// we timed out waiting for a message
				if(_stop_pending) {
					_stop_guard_counter++;
					if(_stop_guard_counter == (STOP_GUARD_KEEP_ALIVE_COUNT * keep_alive_period)) {
						// give up waiting for threads to exit
						// (note that stop() will log a warning about this)
						should_run = false;
					} else {
						this->check_exiting_threads();
						if(_threads.size() <= 1) {  // 1 is for the LoggerThread
							// good, all threads have fully exited
							should_run = false;
						}
					}
				} else {
					// since we timed-out, request keep-alives, if the number of timeouts
					// match the keep alive period
					if (++timeout_counter == keep_alive_period) {
						this->process_keep_alives();
						timeout_counter = 0;
					} // else nothing to do
					// Check if any timers subscribed by the application threads have expired.
					// If so, notify the appropriate subscriber threads 
					this->check_timer_expiry();
				}
				// reset timeout
				timeout = boost::get_system_time() + timeout_period;
				continue;
			}
			if(_exiting_thread_still_running) {
				this->check_exiting_threads();
			}
			switch(msg_ptr->_type) {
			case MTL_Thread_Message::MTL_KEEP_ALIVE_RESPONSE:  { // most common message
				KeepAliveResponseMessage const& kam = dynamic_cast<KeepAliveResponseMessage&>(*msg_ptr);
				std::string const& name = kam._thread.get_name();
				const ThreadMap::iterator iter(_threads.find(&(kam._thread)));
				if(iter == _threads.end()) {
					MTL_LOG(BaseLogMessage::WARNING,
						"KEEP_ALIVE_RESPONSE received from "
						<< name << ", but TM doesn't know about this thread");
				} else {
					iter->second._keep_alive_response_pending = false;
					iter->second._keep_alive_missed_count = 0;
					// FYI, keep-alive timestamp diff =
					// kam->_timestamp - iter->second._keep_alive_timestamp
				}
				break;
			}
			case MTL_Thread_Message::MTL_THREAD_CREATED: {
				ThreadCreatedMessage const& tcm = dynamic_cast<ThreadCreatedMessage&>(*msg_ptr);
				MTL_LOG(BaseLogMessage::INFO,
					"THREAD_CREATED " << tcm._thread.get_name());
				if(_threads.count(&(tcm._thread))) {
					// problem, name already exists!
					MTL_LOG(BaseLogMessage::WARNING,
						"thread " << tcm._thread.get_name() << " already exists!");
				}
				ThreadInfo thread_info = { ACTIVE, false };
				_threads[&(tcm._thread)] = thread_info;
				break;
			}
			case MTL_Thread_Message::MTL_DELETE_THREAD: {
				// A thread has stopped/exited, and can soon be deleted
				DeleteThreadMessage const& dtm = dynamic_cast<DeleteThreadMessage&>(*msg_ptr);
				std::string name(dtm._thread.get_name());
					// make a copy because we may use the name below after we delete the thread obj
				MTL_LOG(BaseLogMessage::INFO, "DELETE_THREAD " << name);
				const ThreadMap::iterator iter(_threads.find(&(dtm._thread)));
				if(iter == _threads.end()) {
					// problem, no thread with that address found!
					MTL_LOG(BaseLogMessage::WARNING,
						"MTL_DELETE_THREAD received from " 
						<< name << ", but " << this->_thread_name
						<< " doesn't know about that thread");
				} else {
					if(iter->second._state != ACTIVE) {
						MTL_LOG(BaseLogMessage::WARNING,
							"MTL_DELETE_THREAD received from " 
							<< name << ", but thread is not ACTIVE");
					}
					bool thread_exited = true;
					if(dtm._thread.joinable()) {
						if(!dtm._thread.try_join_for(boost::chrono::milliseconds(static_cast<int>(TIMED_JOIN_PERIOD)))) {
							// the thread has not exited yet;  we'll check again later
							thread_exited = false;
							_exiting_thread_still_running = true;
							// but don't send it any keep-alives
							iter->second._state = EXITING;
							MTL_LOG(BaseLogMessage::INFO,
								"thread did not exit yet: " << name);
						}
						// else, the thread has truly exited, from an OS standpoint
					} 
					// else, the thread has truly exited, from an OS standpoint
					if(thread_exited) { // delete it and remove all knowledge of it
						iter->second._thread_timer_map.clear();
						delete &(dtm._thread);
						_threads.erase(iter);
						MTL_LOG(BaseLogMessage::INFO,
							"deleted thread " << name);
					}
				}
				break;
			}
			case MTL_Thread_Message::MTL_REPORT_THREAD_QUEUE_INFO: {
				MTL_LOG(BaseLogMessage::INFO, "REPORT_THREAD_QUEUE_INFO");
				// report self
				ThreadQueueInfoMessage tq_info(_thread_name, this->size(),
					this->high_watermark(), this->max_queue_size());
				this->report_thread_queue_info(tq_info);
				// send request to all other (non-EXITING) threads
				ThreadMap::const_iterator iter(_threads.begin());
				const ThreadMap::const_iterator threads_end(_threads.end());
				for(;iter != threads_end; ++iter) {
					if(iter->second._state != EXITING) {
						// use post_high_priority() so that the user does not have to
						// wait long for a response, and so that we get an accurate
						// picture of what all the queue sizes are right now
						iter->first->post_high_priority(new BaseMessage(
							MTL_Thread_Message::MTL_REPORT_THREAD_QUEUE_INFO));
					}
				}
				break;
			}
			case MTL_Thread_Message::MTL_THREAD_QUEUE_INFO: {
				ThreadQueueInfoMessage const& tq_info = dynamic_cast<ThreadQueueInfoMessage&>(*msg_ptr);
				this->report_thread_queue_info(tq_info);
				break;
			}
			case MTL_Thread_Message::MTL_TIMER_EVENT_REQUEST: {
				ThreadTimerEventRequestMessage const& tterm = dynamic_cast<ThreadTimerEventRequestMessage&>(*msg_ptr);
				std::string const& name = tterm._thread.get_name();
				MTL_LOG(BaseLogMessage::INFO, "TIMER_EVENT_REQUEST (" << tterm._timer_type << ") from " << name);
				// Since all timers are monitored every TIMED_AND_WAIT_AND_POP_TIMEOUT_IN_SECONDS,
				// it is important for the timer value (set by the subscriber thread) to be
				// >= TIMED_AND_WAIT_AND_POP_TIMEOUT_IN_SECONDS. This logic is applicable for 
				// MTL_Timer_Type::EVENT_RELATIVE_TIMER only.
				const ThreadMap::iterator iter(_threads.find(&(tterm._thread)));
				if(iter == _threads.end()) {
					MTL_LOG(BaseLogMessage::WARNING,
						"TIMER_EVENT_REQUEST received from "
						<< name << ", but TM doesn't know about this thread");
				} else {
					ThreadInfo& thread_info = iter->second;
					// switch statement looks more elegant
					switch (tterm._timer_type) {
					case MTL_Timer_Type::EVENT_RELATIVE_TIMER: {
						if (tterm._timer_in_seconds < TIMED_AND_WAIT_AND_POP_TIMEOUT_IN_SECONDS) {
							MTL_LOG(BaseLogMessage::ERROR,
								"TIMER_EVENT_REQUEST (" << tterm._timer_type
								<< ") received from " << name
								<< ", but timed_wait_and_pop() timeout value (" << TIMED_AND_WAIT_AND_POP_TIMEOUT_IN_SECONDS
								<< ") > timer set by subscriber thread (" << tterm._timer_in_seconds
								<< ")");
							break;
						}
						// Add entry in ThreadTimerInfoMap
						ThreadTimerInfo thread_timer_info = {tterm._periodic_timer, tterm._thread_specific_timer_type, tterm._timer_in_seconds, 0};
						thread_info._thread_timer_map.insert(std::make_pair(MTL_Timer_Type::EVENT_RELATIVE_TIMER, thread_timer_info));
						break;
					}
					case MTL_Timer_Type::EVENT_NEW_DAY_TIMER: {
						// Only one new hour timer is adequate. Verify if there is an entry in 
						// ThreadTimerInfoMap
						const ThreadTimerInfoMap::const_iterator ttim_iter(thread_info._thread_timer_map.find(MTL_Timer_Type::EVENT_NEW_DAY_TIMER));
						if (ttim_iter == thread_info._thread_timer_map.end()) {
							// no entry found. Add entry in ThreadTimerInfoMap
							time_t now = time(0);
							struct tm timeinfo = *localtime(&now);
							ThreadTimerInfo thread_timer_info = {tterm._periodic_timer, tterm._thread_specific_timer_type, 0, 0, timeinfo.tm_yday};
							thread_info._thread_timer_map.insert(std::make_pair(MTL_Timer_Type::EVENT_NEW_DAY_TIMER, thread_timer_info));
						} // else silently ignore this request
						break;
					}										
					default: {
						MTL_LOG(BaseLogMessage::ERROR, 
							"Invalid TIMER_EVENT_REQUEST (" << tterm._timer_type
							<< ") from " << name);
						break;
					}
					}
				}
				break;
			}				
			case MTL_Thread_Message::MTL_DUMP_OBJECTS: {
				// dump self
				MTL_LOG(BaseLogMessage::INFO, "--DUMP--\n" << *this);
				// tell all other (non-EXITING) threads to dump selves
				ThreadMap::const_iterator iter(_threads.begin());
				for(;iter != _threads.end(); ++iter) {
					if(iter->second._state != EXITING) {
						iter->first->post(new BaseMessage(MTL_Thread_Message::MTL_DUMP_OBJECTS));
					}
				}
				break;
			}
			case MTL_Thread_Message::MTL_STOP:	// stop self
				MTL_LOG(BaseLogMessage::INFO, "STOP");
				if(_threads.size() > 1) {  // 1 is for the LoggerThread.
					// Not all of the threads have exited yet, so wait a bit.
					MTL_LOG(BaseLogMessage::INFO, "waiting for "
						<< (_threads.size() - 1) << " threads to exit...");
					_stop_pending = true;
				} else {
					should_run = false;
				}
				break;
			case MTL_Thread_Message::MTL_ABNORMAL_SHUTDOWN: {
				this->notify_main_thread_of_abnormal_shutdown();
				// notify all threads of imminent abnormal shutdown
				ThreadMap::const_iterator iter(_threads.begin());
				const ThreadMap::const_iterator threads_end(_threads.end());
				while(iter != threads_end) {
					if(iter->second._state != EXITING) {
						iter->first->abnormal_shutdown();
						// note, abnormal_shutdown() is a thread-safe method
					}
					++iter;
				}
				break;
			}
			case MTL_Thread_Message::MTL_THREAD_ERROR: {
				ThreadErrorMessage const& tem = dynamic_cast<ThreadErrorMessage&>(*msg_ptr);
				this->thread_error(tem._thread_error_type, tem._thread_name, tem._error_msg);
				break;
			}
			default:
				this->process_input_message(msg_ptr);
				break;
			// In process_input_message(), subclasses should include a 'default' case
			// with the following code:
			// default: {
			//   MTL_LOG(BaseLogMessage::WARNING, "UNKNOWN msg, type=" << msg_ptr->_type);
			//   break;
			// }
			} // end of switch()
		} catch(...) {
			ExceptionDispatcher::get_instance()->handle_exception();
		}
		delete msg_ptr; // we own it, so we must delete it
	}
	this->stop();
}
// ---------------------------------------------------------------------------------------
/// Logs input queue information for a thread to the info logfile.
/// Projects should override this method if thread queue info is reported via a CLI interface.
/// @param[in] tqi_msg_- message containing input queue information
/*virtual*/ void ThreadManager::report_thread_queue_info(ThreadQueueInfoMessage const& tqi_msg_) const {
	MTL_LOG(BaseLogMessage::INFO, "Thread " << tqi_msg_._thread_name
		<< ": queue size="		<< tqi_msg_._queue_size
		<< " high watermark="	<< tqi_msg_._high_watermark
		<< " max size="			<< tqi_msg_._max_size);
}
// ---------------------------------------------------------------------------------------
/// Posts MTL_TIMER_EVENT_NOTIFY message to threads
/// that subscribed to MTL_Event::MTL_TIMER_EVENT_REQUEST event
void ThreadManager::check_timer_expiry() {
	ThreadMap::iterator iter(_threads.begin());
	const ThreadMap::const_iterator threads_end(_threads.end());
	while(iter != threads_end) {
		ThreadInfo& thread_info = iter->second;
		BaseThread& app_thread = *(iter->first);
		if(thread_info._state == ACTIVE) {
			if(!thread_info._thread_timer_map.empty()) {
				// parse ThreadTimerInfoMap and increment counter to indicate time elapsed. If the 
				// counter indicates that a time has expired, the appropriate subscriber thread
				// is notified
				this->update_time_elapsed_and_notify_subscriber_threads(app_thread, thread_info._thread_timer_map);
			} // else thread has not subscribed to MTL_Event::MTL_TIMER_EVENT_REQUEST event
		} // else, thread is not ACTIVE
		++iter;
	}
}
// ---------------------------------------------------------------------------------------
/// Updates time elapsed for each timer that a thread subscribed and sends notification
/// MTL_TIMER_EVENT_NOTIFY message is sent only for those timers that expired.
/// If a one shot timer expires, the entry for that timer is removed from thread_timer_map_.
/// For that reason, thread_timer_map_ is not a const reference.
/// @param[in] app_thread_				- Application subscriber thread 
/// @param[in] thread_timer_map_	- map of ThreadTimerInfo for a specific timer subscriber thread
void ThreadManager::update_time_elapsed_and_notify_subscriber_threads(BaseThread& app_thread_, ThreadTimerInfoMap& thread_timer_map_) {
	ThreadTimerInfoMap::iterator iter(thread_timer_map_.begin());
	while(iter != thread_timer_map_.end()) {
		ThreadTimerInfo& thread_timer_info = iter->second;
		switch (iter->first) {
		case (MTL_Timer_Type::EVENT_RELATIVE_TIMER) : {
			if (++thread_timer_info._elapsed_time_in_seconds == thread_timer_info._timer_in_seconds) {
				// send MTL_TIMER_EVENT_NOTIFY notification to subscriber thread 
				app_thread_.post_high_priority(new ThreadTimerEventNotifyMessage(
					MTL_Timer_Type::EVENT_RELATIVE_TIMER,
					thread_timer_info._thread_specific_timer_type));
				if (!thread_timer_info._periodic_timer) {
					// one shot timer entry. delete entry
					thread_timer_map_.erase(iter++);
					continue;
				} else {
					// periodic timer
					thread_timer_info._elapsed_time_in_seconds = 0;
				}
			} // else nothing to do
			break;
		}
		case (MTL_Timer_Type::EVENT_NEW_DAY_TIMER) : {
			if (++thread_timer_info._elapsed_time_in_seconds == SECONDS_IN_A_MINUTE) {
				// Every minute check if it is a new day
				time_t now = time(0);
				struct tm timeinfo = *localtime(&now);
				if (timeinfo.tm_yday != thread_timer_info._day_count) {
					thread_timer_info._day_count = timeinfo.tm_yday;
					// send MTL_TIMER_EVENT_NOTIFY notification to subscriber thread 
					app_thread_.post_high_priority(new ThreadTimerEventNotifyMessage(
						MTL_Timer_Type::EVENT_NEW_DAY_TIMER,
						thread_timer_info._thread_specific_timer_type));
					if (!thread_timer_info._periodic_timer) {
						// one shot timer entry. delete entry
						thread_timer_map_.erase(iter++);
						continue;
					} // else nothing to do
				} // else same day. nothing to do
				thread_timer_info._elapsed_time_in_seconds = 0;
			} // else nothing to do
			break;
		}
		default: {
				throw std::logic_error("ThreadManager::update_time_elapsed_and_notify_subscriber_threads() - Invalid MTL_Timer_Type::Timer_Type ");
				break;	
		}
		}
		iter++;
	}
}
// ---------------------------------------------------------------------------------------
/// Checks for keep-alive reponses.  Sends new keep-alive messages.
/// This method is not thread-safe, and hence must only be called in the context of
/// ThreadManager's run() method.
/*virtual*/ void ThreadManager::process_keep_alives() {
	ThreadMap::iterator iter(_threads.begin());
	const ThreadMap::const_iterator threads_end(_threads.end());
	while(iter != threads_end) {
		ThreadInfo& thread_info = iter->second;
		BaseThread& app_thread = *(iter->first);
		if(thread_info._state == ACTIVE) {
			if(thread_info._keep_alive_response_pending) {
				// keep-alive response wasn't received
				thread_info._keep_alive_missed_count++;
				if((thread_info._keep_alive_missed_count == FIRST_LOG_KEEP_ALIVE_MISS_COUNT)
				 ||(thread_info._keep_alive_missed_count % PERIODIC_LOG_KEEP_ALIVE_MISS_COUNT == 0)) {
					if(thread_info._keep_alive_missed_count == FIRST_LOG_KEEP_ALIVE_MISS_COUNT) {
						MTL_LOG(BaseLogMessage::ERROR, 
							"keep-alive missed " 
							<< thread_info._keep_alive_missed_count
							<< " time(s) by thread " << app_thread.get_name());
					} else {
						// the thread may be stuck!
						std::ostringstream oss;
						oss << "keep-alive missed "  << thread_info._keep_alive_missed_count
							<< " time(s) by thread " << app_thread.get_name();
						this->thread_error(MTL_Thread_Error::THREAD_KEEP_ALIVE,
							BaseThread::unique_thread_str(), oss.str());
						if(thread_info._keep_alive_missed_count == PERIODIC_LOG_KEEP_ALIVE_MISS_COUNT) {
							// the first "periodic time" we miss, dump all objects
							// to the info log, as a debugging aid
							this->dump_objects();
						}
					}
					// send another keep-alive
					app_thread.post_high_priority(new KeepAliveRequestMessage(thread_info._keep_alive_counter++));
				}
			} else {
				// keep-alive response was received;  send another
				app_thread.post_high_priority(new KeepAliveRequestMessage(thread_info._keep_alive_counter++));
				// there could be considerable delay between the above line and the
				// next line (e.g., thread was time-sliced out), but it should be good
				// enough for our purposes.  We err on the side of a smaller clockdiff
				// later by recording the time after calling post().
				thread_info._keep_alive_timestamp = time(0);
				thread_info._keep_alive_response_pending = true;
			}
		} // else, thread is not ACTIVE so we no longer manage keep-alives for it
		++iter;
	}
	_last_keep_alives_sent_timestamp = time(0);
}
// ---------------------------------------------------------------------------------------
/// Checks to see if exiting thread(s) have exited.  If so, deletes the C++ associated
/// thread object.
/// This method is not thread-safe, and hence must only be called in the context of
/// ThreadManager's run() method.
/*virtual*/ void ThreadManager::check_exiting_threads() {
	// see if the exiting threads have finished
	size_t exiting_threads_still_running_count = 0;
	ThreadMap::iterator iter(_threads.begin());
	while(iter != _threads.end()) {
		ThreadInfo& thread_info = iter->second;
		if(thread_info._state == EXITING) {
			bool thread_exited = true;
			BaseThread& exiting_thread = *(iter->first);
			if(exiting_thread.joinable()) {
				if(!exiting_thread.try_join_for(boost::chrono::milliseconds(static_cast<int>(TIMED_JOIN_PERIOD)))) {
					// the thread has not exited yet;  we'll check again later
					thread_exited = false;
					exiting_threads_still_running_count++;
					MTL_LOG(BaseLogMessage::INFO,
						"thread STILL did not exit yet: "
						<< exiting_thread.get_name()
						<< " ID=" << exiting_thread.id());
				}
				// else, thread exited
			} 
			// else, thread exited
			if(thread_exited) {  // delete it and remove all knowledge of it
				std::string name(exiting_thread.get_name());
				delete &exiting_thread;
				_threads.erase(iter++);
				MTL_LOG(BaseLogMessage::INFO, "deleted thread " << name);
				continue;
			}
		} // else, thread is not EXITING, do nothing here
		++iter;
	}
	if(exiting_threads_still_running_count == 0) { // clear flag
		_exiting_thread_still_running = false;
	}
}
// ---------------------------------------------------------------------------------------
/// Posts message CHILD_THREAD_STOPPED to _parent_thread if _parent_thread is != 0.
/// Logs an error if any thread other than the Logger is still running.
/// This method is not thread-safe, and hence must only be called in the context of
/// ThreadManager's run() method.
/// overrides - ThreadWithInputQueue::stop() may post to ThreadManager, which must
///             not happen here
/*virtual*/ void ThreadManager::stop() {
	MTL_LOG(BaseLogMessage::INFO,"stopping");
	if(_parent_thread) {
		if(_queue.size() != 0) {
			MTL_LOG(BaseLogMessage::INFO,
				"stop() called, but input queue is not empty. Q size="
				<< this->size());
		}
		_parent_thread->post(new ThreadStoppedMessage(*this));
		// When the parent thread receives this message, it should
		// join() this child thread -– to ensure that is has completely
		// exited its run() method, and performed any other OS thread
		// cleanup -- and then delete this child thread object.
		// The thread manager can not delete itself, and it must
		// not be sent a DELETE_THREAD message for itself.
	} // else, no parent, so no thread needs to be notified
	if(_threads.size() > 1) {
		std::string running_threads;
		ThreadMap::iterator iter(_threads.begin());
		while(iter != _threads.end()) {
			std::string const& thread_name(iter->first->get_name());
			if(thread_name != _logger_thread_name_) {
				running_threads += thread_name;
				running_threads += " ";
			}
			++iter;
		}
		MTL_LOG(BaseLogMessage::WARNING, "stop() called"
			", but the following threads are still running: " << running_threads);
	}
	MTL_LOG(BaseLogMessage::INFO, "stopped");
}
// ---------------------------------------------------------------------------------------
/// Performs critical cleanup before the program exits.
/// This consists of calling abnormal_shutdown() on all threads, but we can't do that from
/// this method since it was called from some other thread.  Post an MTL_ABNORMAL_SHUTDOWN
/// message to the *front* of this thread's input queue, and hope it gets posted, and hope
/// it gets processed.  That's the best we can do/try.
/// This method is (and must remain) thread-safe.
/*virtual*/ void ThreadManager::abnormal_shutdown() {
	boost::mutex::scoped_lock lock(_mutex);
	if(_abnormal_shutdown_called) {
		// we can only handle one abnormal shutdown!
	} else {
		_abnormal_shutdown_called = true;
		_queue.push_front(_abnormal_shutdown_message);	// may throw bad_alloc
	}
}
// ---------------------------------------------------------------------------------------
/// Human readable representation of this object as a string.  Useful for debugging.
/// This method is not thread-safe, and hence must only be called in the context of
/// ThreadManager's run() method.
/// @param[in] os_ - ostream into which the string will be inserted
/*virtual*/ void ThreadManager::printOn(std::ostream& os_) const {
	os_	<< "ThreadManager[last_keep_alives_sent_timestamp="	  << _last_keep_alives_sent_timestamp
		<< ",exiting_thread_still_running="	<< std::boolalpha << _exiting_thread_still_running
		<< ",stop_guard_counter="			<< _stop_guard_counter
		<< ",stop_pending="					<< std::boolalpha << _stop_pending
		<< ",threads=";
	ThreadMap::const_iterator iter(_threads.begin());
	while(iter != _threads.end()) {
		ThreadInfo const& thread_info = iter->second;
		char buf[80];
		struct tm *ts;
		ts = localtime(&thread_info._keep_alive_timestamp);
		strftime(buf, sizeof(buf), "%a %m-%d-%Y %H:%M:%S %Z", ts);
		os_ << "\n  " << iter->first->get_name()
			<< ": ID=" << iter->first->id()
			<< ",state=" << thread_info._state
			<< ",keep_alive_response_pending=" << std::boolalpha << thread_info._keep_alive_response_pending
			<< ",keep_alive_counter=" << thread_info._keep_alive_counter
			<< ",keep_alive_timestamp=" << buf
			<< ",keep_alive_missed_count=" << thread_info._keep_alive_missed_count;
			os_ << "\nThreadTimerInfo=";
			ThreadTimerInfoMap::const_iterator thread_timer_iter(thread_info._thread_timer_map.begin());
			while(thread_timer_iter != thread_info._thread_timer_map.end()) {
				ThreadTimerInfo const& thread_timer_info = thread_timer_iter->second;
				os_ << "MTL_Timer_Type[" << thread_timer_iter->first << "]="
						<< "periodic_timer=" << std::boolalpha << thread_timer_info._periodic_timer
						<< ",thread_specific_timer_type=" << thread_timer_info._thread_specific_timer_type
						<< ",timer_in_seconds=" << thread_timer_info._timer_in_seconds
						<< ",day_count=" << thread_timer_info._day_count
						<< "\n";
				++thread_timer_iter;
			}
		++iter;
	}
	// _abnormal_shutdown_called and _abnormal_shutdown_message are purposely not printed
	os_ << "\n ,";
	this->ThreadWithInputQueue::printOn(os_);
	os_	<< ']';
}
