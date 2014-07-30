/** @file LoggerThread.cpp */
#ifdef WIN32
	#include <winsock2.h>
	#include <direct.h>		// for mkdir
#else
	#include <arpa/inet.h>
#endif
#include <sys/stat.h>
#include <iostream>
#include "MTL_sw_version.hpp" 
#include "ExceptionDispatcher.hpp"
#include "LoggerThread.hpp"
#include "ConcurrentQueue.cpp"	// need to #include cpp file to get templatized definitions
// =======================================================================================
// definition of statics
LoggerThread* LoggerThread::_instance_ = 0;
// =======================================================================================
/// Attempts to create the logfiles_path_ directory, if it does not already exist.
/// If unable to create the directory, the application exits.
/// Otherwise, sets the logfiles path and log filenames.
/// This method is not thread-safe and must be called externally before this
/// thread is started.
/// @param[in] logfiles_path_		- path to logfiles directory
/// @param[in] errorlog_filename_	- name of error logfile
/// @param[in] infolog_filename_	- name of info logfile
/// @param[in] process_name_		- process name
/// @param[in] software_version_	- program's software version string
/// @return - false if logfiles_path_ directory doesn't exist and it can't be created;
///           true  otherwise
bool LoggerThread::configure(std::string const& logfiles_path_,
  std::string const& errorlog_filename_,  std::string const& infolog_filename_,
  std::string const& process_name_, std::string const& software_version_) {
	if(_thread.joinable()) {
		// Some external caller is calling this method after the thread has started.
		// This is not safe!
		throw std::logic_error("LoggerThread::configure() called after thread was started");
	}
	struct stat st;
	if(stat(logfiles_path_.c_str(), &st) == 0) {
		// directory exists
	} else {
		#ifdef WIN32
		  if(mkdir(logfiles_path_.c_str()) != 0) {
		#else
		  if(mkdir(logfiles_path_.c_str(), 0760) != 0) {
		#endif
			std::cerr << "ERROR: unable to create logfiles directory "
				<< logfiles_path_ << ": " << strerror(errno) << std::endl;
			return false;
		  }
	}
	_logfiles_path		= logfiles_path_;
	_errorlog_filename	= errorlog_filename_;
	_infolog_filename	= infolog_filename_;
	_process_name		= process_name_;
	_software_version	= software_version_;
	return true;
}
// ---------------------------------------------------------------------------------------
/// Opens/creates all logfiles.
/// This method is not thread-safe and must only be called externally before the Logger
/// thread is started.
void LoggerThread::open_logfiles(void) {
	this->open_error_logfile();
	this->open_info_logfile();
}
// ---------------------------------------------------------------------------------------
/// Opens error logfile for appending.  If the log file is opened  successfully, errors
/// will no longer be logged to syslog.
/// This method is not thread-safe and must only be called in the context of Logger's
/// run() method, or it can be called externally before the Logger thread is started.
void LoggerThread::open_error_logfile() {
	const bool logger_is_caller = (boost::this_thread::get_id() == this->id());
	if(!logger_is_caller && _thread.joinable()) {
		// Some external caller is calling this method after the thread has started.
		// This is not safe!
		throw std::logic_error("LoggerThread::open_error_logfile() called "
			"externally after thread was started");
	}
	this->create_directory_for_today();
	std::string full_file_path(_logfiles_date_path + '/' + _errorlog_filename);
	_error_logfile.open(full_file_path.c_str(), std::fstream::out | std::fstream::app);
	if(_error_logfile.fail()) {
		std::ostringstream oss;
		oss << "unable to open error log file " << full_file_path;
		if(logger_is_caller) {
			BaseLogMessage log_msg(BaseLogMessage::ERROR, _thread_name, oss.str());
			this->log_error(log_msg);
		} else {
			LoggerThread::get_instance()->log(
				new BaseLogMessage(BaseLogMessage::ERROR, "---", oss.str()));
		}
	} else {
		_log_to_syslog = false;
	}
}
// ---------------------------------------------------------------------------------------
/// Opens info logfile for appending.  The software release number is logged.
/// This method is not thread-safe and must only be called in the context of Logger's
/// run() method, or it can be called externally before the Logger thread is started.
void LoggerThread::open_info_logfile() {
	bool logger_is_caller = (boost::this_thread::get_id() == this->id());
	if(!logger_is_caller && _thread.joinable()) {
		// Some external caller is calling this method after the thread has started.
		// This is not safe!
		throw std::logic_error("LoggerThread::open_info_logfile() called "
			"externally after thread was started");
	}
	this->create_directory_for_today();
	std::string full_file_path(_logfiles_date_path + '/' + _infolog_filename);
	_info_logfile.open(full_file_path.c_str(), std::fstream::out | std::fstream::app);
	if(_info_logfile.fail()) {
		std::ostringstream oss;
		oss << "unable to open info log file " << full_file_path;
		if(logger_is_caller) {
			BaseLogMessage log_msg(BaseLogMessage::ERROR, _thread_name, oss.str());
			this->log_error(log_msg);
		} else {
			LoggerThread::get_instance()->log(
				new BaseLogMessage(BaseLogMessage::ERROR, "---", oss.str()));
		}
	} else {
		std::ostringstream mtl_oss;
		std::ostringstream app_oss;
		mtl_oss << "mtl software version=" << MTL_SW_VERSION;
		app_oss << "app software version=" << _software_version;
		if(logger_is_caller) {
			BaseLogMessage mtl_log_msg(BaseLogMessage::INFO, _thread_name, mtl_oss.str());
			this->log_info(mtl_log_msg);
			BaseLogMessage app_log_msg(BaseLogMessage::INFO, _thread_name, app_oss.str());
			this->log_info(app_log_msg);
		} else {
			LoggerThread::get_instance()->log(
				new BaseLogMessage(BaseLogMessage::INFO, BaseThread::unique_thread_str(),
					mtl_oss.str()));
			LoggerThread::get_instance()->log(
				new BaseLogMessage(BaseLogMessage::INFO, BaseThread::unique_thread_str(),
					app_oss.str()));
		}
	}
}
// ---------------------------------------------------------------------------------------
/// Checks for empty queue.
/// Unlike BaseThread::stop(), does not send DELETE_THREAD to the ThreadManager, since the
/// the Logger should be the last thread (other than the main thread) to exit.  So, the
/// ThreadManager should have already stopped/exited/terminated.
/// overrides - ThreadWithInputQueue::stop() calls log() which must not be called by
///   this thread
void LoggerThread::stop() {
	if(this->size() != 0) {
		// This shouldn't happen.  The main thread should have waited for all other
		// threads to exit, then it should post() a STOP to the logger.
		_info_logfile << "stop() called on " << _thread_name
			<< ", but input queue was not empty. Q size="
			<< this->size();
	}
	BaseLogMessage info_msg(BaseLogMessage::INFO, _thread_name, "stopped");
	this->log_info(info_msg);
}
// ---------------------------------------------------------------------------------------
/// If there is room, posts pointer msg_ptr_ to the back of this thread's input queue,
/// and signals the logger via the _condition_variable.
/// The caller MUST NOT use msg_ptr_ after calling this method.
/// Ownership of the message is transferred to the consumer of the input queue (i.e., the
/// logger).
/// If the message cannot be posted, it is simply deleted, and there will be no indication
/// that this has happened.  (Well, if you see a huge stream of log messages, maybe you
/// can infer that some are getting dropped.)
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
/// NOTE: changes made to this method likely require similar changes to base class method
/// ThreadWithInputQueue::post().
/// overrides - ThreadWithInputQueue::post(), which calls log() which is this method -- we
/// can't allow that!
/// @param[in] msg_ptr_ - pointer to the data/message to be posted
/// @return - true if msg_ptr_ was successfully posted; false otherwise
bool LoggerThread::post(BaseMessage* const& msg_ptr_) {
	const bool critical_msg = (msg_ptr_->_type < MTL_Thread_Message::MTL_CRITICAL_MESSAGE_END);
	const unsigned int msg_drop_counter = this->push_back(msg_ptr_, critical_msg);
	if(msg_drop_counter) {
		// push failed
		if(msg_drop_counter == 1) {
			// The watermark has been crossed for the first time, or the first time since
			// the counter was reset.  Write to log file now.  BUT, we can't do that since
			// log_msg() is not thread-safe.  So, without adding a mutex to log_msg(),
			// there will be no way to know that the system is dropping log() messages.
			// Oh well, it is what it is.
		}
		delete msg_ptr_;
		return false;
	}
	return true;
}
// ---------------------------------------------------------------------------------------
/// If there is room, posts pointer msg_ptr_ to the front of this thread's input queue,
/// and signals the logger via the _condition_variable.
/// The caller MUST NOT use msg_ptr_ after calling this method.
/// Ownership of the message is transferred to the consumer of the input queue (i.e., the
/// logger).
/// If the message cannot be posted, it is simply deleted, and there will be no indication
/// that this has happened.  (Well, if you see a huge stream of log messages, maybe you
/// caninfer that some are getting dropped.)
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
/// NOTE: changes made to this method likely require similar changes to base class method
/// ThreadWithInputQueue::post_high_priority().
/// overrides - ThreadWithInputQueue::post_high_priority(), which calls log() which is
/// this method -- we can't allow that!
/// @param[in] msg_ptr_ - pointer to the data/message to be posted
/// @return - true if msg_ptr_ was successfully posted; false otherwise
bool LoggerThread::post_high_priority(BaseMessage* const& msg_ptr_) {
	const bool critical_msg = (msg_ptr_->_type < MTL_Thread_Message::MTL_CRITICAL_MESSAGE_END);
	const unsigned int msg_drop_counter = this->push_front(msg_ptr_, critical_msg);
	if(msg_drop_counter) {
		// push failed
		if(msg_drop_counter == 1) {
			// The watermark has been crossed for the first time,
			// or the first time since the counter was reset.
			// Write to log file now.  BUT, we can't do that since
			// log_msg() is not thread-safe.
			// So, without adding a mutex to log_msg(), there will
			// be no way to know that the system is dropping log()
			// messages.  Oh well, it is what it is.
		}
		delete msg_ptr_;
		return false;
	}
	return true;
}
// ---------------------------------------------------------------------------------------
/// Convenience method for post(), except that a BaseLogMessage is required.
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
void LoggerThread::log(BaseLogMessage* const& log_msg_ptr_) {
	this->post(log_msg_ptr_);
}
// ---------------------------------------------------------------------------------------
/// Convenience method for post(), except that a LogMessageWithIp is required.
/// This method is (and must remain) thread-safe, supporting multiple simultaneous callers.
void LoggerThread::log(LogMessageWithIp* const& log_msg_ptr_) {
	this->post(log_msg_ptr_);
}
// ---------------------------------------------------------------------------------------
/// Logs message contained in log_msg_ to either the error logfile, or to syslog.
/// Message is also logged to std::cerr if _log_to_cerr_also flag is set.
/// This method is not thread-safe and must only be called in the context of Logger's
/// run() method.
/// NOTE that this method is overloaded, so most likely any changes to this method also
/// need to be applied to the other overloaded method.
/// @pre log_msg_._severity <= BaseLogMessage::WARNING
void LoggerThread::log_error(LogMessageWithIp const& log_msg_) {
	#if defined(WIN32) || defined(LOGGER_DEBUG)
		using namespace boost;
		using namespace local_time;
		using posix_time::time_duration;
		time_zone_ptr zone(new posix_time_zone("GMT-05"));
		#ifdef WIN32
			enum { LOG_EMERG = 0, LOG_ALERT, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_NOTICE,
				LOG_INFO, LOG_DEBUG };
		#endif
	#endif
	int log_severity;
	std::string severity_str = "UNKNOWN";
	switch (log_msg_._severity) {
		case BaseLogMessage::EMERGENCY:
			log_severity = LOG_EMERG;
			severity_str = "EMERGENCY";
			break;
		case BaseLogMessage::ALERT:
			log_severity = LOG_ALERT;
			severity_str = "ALERT";
			break;
		case BaseLogMessage::CRITICAL:
			log_severity = LOG_CRIT;
			severity_str = "CRITICAL";
			break;
		case BaseLogMessage::ERROR:
			log_severity = LOG_ERR;
			severity_str = "ERROR";
			break;
		case BaseLogMessage::WARNING:
			log_severity = LOG_WARNING;
			severity_str = "WARNING";
			break;
		default:
			severity_str = "UNKNOWN";
			log_severity = LOG_ERR;
		break;
	}
	if(_log_to_syslog || (!_error_logfile.is_open()) ) {
		#ifdef WIN32
			// since there is no syslog on Windows, always send to cerr
			std::cerr << "LOG - " << local_sec_clock::local_time(zone) << ':'
				<< _process_name << '[' << _pid << "]:"
				<< severity_str  << '('  << log_severity << "):"
				<< log_msg_._thread_name << ':';
			if(log_msg_._ipv4_addr.s_addr != 0) {
				std::cerr << "srcIP=" << this->ipv4_as_dotted_decimal(log_msg_._ipv4_addr);
				if (!log_msg_._hostname.empty()) {
					std::cerr << '(' << log_msg_._hostname << ')';
				}
				std::cerr << ':';
			}
			std::cerr << log_msg_._msg << std::endl;
		#else
			syslog(log_severity|LOG_LOCAL5, "%s;%s", severity_str.c_str(), log_msg_._msg.c_str());
			#if LOGGER_DEBUG
				std::cerr << "LOG - " << local_sec_clock::local_time(zone) << ':';
				std::cerr << severity_str << ':' << log_msg_._thread_name << ':';
				if(log_msg_._ipv4_addr != 0) {
					std::cerr << "srcIP=" << this->ipv4_as_dotted_decimal(log_msg_._ipv4_addr);
					if (!log_msg_._hostname.empty()) {
						std::cerr << '(' << log_msg_._hostname << ')';
					}
					std::cerr << ':';
				}
				std::cerr << log_msg_._msg << std::endl;
			#endif
		#endif
	} else {
		time_t now = time(0);
#ifdef WIN32
		char* date_time = ctime(&now);
#else
		char date_time[30];
		ctime_r(&now, date_time);
#endif
		date_time[strlen(date_time) - 1] = '\0';  // replace \n with \0
		_error_logfile << date_time << ':' << _process_name << '[' << _pid << "]:"
			<< severity_str << ':' << log_msg_._thread_name << ':';
		if(log_msg_._ipv4_addr.s_addr != 0) {
			_error_logfile << "srcIP=" << this->ipv4_as_dotted_decimal(log_msg_._ipv4_addr);
			if (!log_msg_._hostname.empty()) {
				_error_logfile << '(' << log_msg_._hostname << ')';
			}
			_error_logfile << ':';
		}
		_error_logfile << log_msg_._msg << std::endl;
		#if LOGGER_DEBUG
			std::cerr << "ERRORLOG - " << local_sec_clock::local_time(zone) << ':'
				<< _process_name << '[' << _pid << "]:";
			std::cerr << severity_str << ':' << log_msg_._thread_name << ':';
			if(log_msg_._ipv4_addr != 0) {
				std::cerr << "srcIP=" << this->ipv4_as_dotted_decimal(log_msg_._ipv4_addr);
				if (!log_msg_._hostname.empty()) {
					std::cerr << '(' << log_msg_._hostname << ')';
				}
				std::cerr << ':';
			}
			std::cerr << log_msg_._msg << std::endl;
		#endif
	}
	if(_log_to_cerr_also) {
		time_t now = time(0);
#ifdef WIN32
		char* date_time = ctime(&now);
#else
		char date_time[30];
		ctime_r(&now, date_time);
#endif
		date_time[strlen(date_time) - 1] = '\0';  // replace \n with \0
		std::cerr << date_time << ':' << _process_name << '[' << _pid << "]:"
			<< severity_str << ':' << log_msg_._thread_name << ':';
		if(log_msg_._ipv4_addr.s_addr != 0) {
			std::cerr << "srcIP=" << this->ipv4_as_dotted_decimal(log_msg_._ipv4_addr);
			if (!log_msg_._hostname.empty()) {
				std::cerr << '(' << log_msg_._hostname << ')';
			}
			std::cerr << ':';
		}
		std::cerr << log_msg_._msg << std::endl;
	}
}
// ---------------------------------------------------------------------------------------
/// Logs message contained in log_msg_ to either the error logfile, or to syslog.
/// Message is also logged to std::cerr if _log_to_cerr_also flag is set.
/// This method is not thread-safe and must only be called in the context of Logger's
/// run() method.
/// NOTE that this method is overloaded, so most likely any changes to this method also
/// need to be applied to the other overloaded method.
/// @pre log_msg_._severity <= BaseLogMessage::WARNING
void LoggerThread::log_error(BaseLogMessage const& log_msg_) {
	#if defined(WIN32) || defined(LOGGER_DEBUG)
		using namespace boost;
		using namespace local_time;
		using posix_time::time_duration;
		time_zone_ptr zone(new posix_time_zone("GMT-05"));
		#ifdef WIN32
			enum { LOG_EMERG = 0, LOG_ALERT, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_NOTICE,
				LOG_INFO, LOG_DEBUG };
		#endif
	#endif
	int log_severity;
	std::string severity_str = "UNKNOWN";
	switch (log_msg_._severity) {
		case BaseLogMessage::EMERGENCY:
			log_severity = LOG_EMERG;
			severity_str = "EMERGENCY";
			break;
		case BaseLogMessage::ALERT:
			log_severity = LOG_ALERT;
			severity_str = "ALERT";
			break;
		case BaseLogMessage::CRITICAL:
			log_severity = LOG_CRIT;
			severity_str = "CRITICAL";
			break;
		case BaseLogMessage::ERROR:
			log_severity = LOG_ERR;
			severity_str = "ERROR";
			break;
		case BaseLogMessage::WARNING:
			log_severity = LOG_WARNING;
			severity_str = "WARNING";
			break;
		default:
			severity_str = "UNKNOWN";
			log_severity = LOG_ERR;
		break;
	}
	if(_log_to_syslog || (!_error_logfile.is_open()) ) {
		#ifdef WIN32
			// since there is no syslog on Windows, always send to cerr
			std::cerr << "LOG - " << local_sec_clock::local_time(zone) << ':'
				<< _process_name << '[' << _pid << "]:"
				<< severity_str  << '('  << log_severity << "):"
				<< log_msg_._thread_name << ':'
				<< log_msg_._msg << std::endl;
		#else
			syslog(log_severity|LOG_LOCAL5, "%s;%s", severity_str.c_str(), log_msg_._msg.c_str());
			#if LOGGER_DEBUG
				std::cerr << "LOG - " << local_sec_clock::local_time(zone) << ':'
					<< severity_str << ':' << log_msg_._thread_name << ':'
					<< log_msg_._msg << std::endl;
			#endif
		#endif
	} else {
		time_t now = time(0);
#ifdef WIN32
		char* date_time = ctime(&now);
#else
		char date_time[30];
		ctime_r(&now, date_time);
#endif
		date_time[strlen(date_time) - 1] = '\0';  // replace \n with \0
		_error_logfile << date_time << ':' << _process_name << '[' << _pid << "]:"
			<< severity_str << ':' << log_msg_._thread_name << ':'
			<< log_msg_._msg << std::endl;
		#if LOGGER_DEBUG
			std::cerr << "ERRORLOG - " << local_sec_clock::local_time(zone) << ':'
				<< _process_name << '[' << _pid << "]:"
				<< severity_str << ':' << log_msg_._thread_name << ':'
				<< log_msg_._msg << std::endl;
		#endif
	}
	if(_log_to_cerr_also) {
		time_t now = time(0);
#ifdef WIN32
		char* date_time = ctime(&now);
#else
		char date_time[30];
		ctime_r(&now, date_time);
#endif
		date_time[strlen(date_time) - 1] = '\0';  // replace \n with \0
		std::cerr << date_time << ':' << _process_name << '[' << _pid << "]:"
			<< severity_str << ':' << log_msg_._thread_name << ':'
			<< log_msg_._msg << std::endl;
	}
}
// ---------------------------------------------------------------------------------------
/// Logs message contained in log_msg_ to the info logfile.
/// This method is not thread-safe and must only be called in the context of Logger's
/// run() method.
/// NOTE that this method is overloaded, so most likely any changes to this method also
/// need to be applied to the other overloaded method.
/// @pre log_msg_._severity >= BaseLogMessage::NOTICE
void LoggerThread::log_info(BaseLogMessage const& log_msg_) {
	if(_info_logfile.is_open()) {
		std::string severity_str = "UNKNOWN";
		switch (log_msg_._severity) {
		case BaseLogMessage::NOTICE:
			severity_str = "NOTICE";
			break;
		case BaseLogMessage::INFO:
			severity_str = "INFO";
			break;
		case BaseLogMessage::DEBUG:
			severity_str = "DEBUG";
			break;
		default:
			severity_str = "UNKNOWN";
			break;
		}
		time_t now = time(0);
#ifdef WIN32
		char* date_time = ctime(&now);
#else
		char date_time[30];
		ctime_r(&now, date_time);
#endif
		date_time[strlen(date_time) - 1] = '\0'; // replace \n with \0
		_info_logfile << date_time << ':' << _process_name << '[' << _pid << "]:"
			<< severity_str << ':' << log_msg_._thread_name << ':'
			<< log_msg_._msg << std::endl;
		#if LOGGER_DEBUG
			std::cerr << "INFOLOG  - " << date_time << ':'
				<< _process_name << '[' << _pid << "]:"
				<< severity_str << ':' << log_msg_._thread_name << ':'
				<< log_msg_._msg << std::endl;
		#endif
	}
}
// ---------------------------------------------------------------------------------------
/// Logs message contained in log_msg_ to the info logfile.
/// This method is not thread-safe and must only be called in the context of Logger's
/// run() method.
/// NOTE that this method is overloaded, so most likely any changes to this method also
/// need to be applied to the other overloaded method.
/// @pre log_msg_._severity >= BaseLogMessage::NOTICE
void LoggerThread::log_info(LogMessageWithIp const& log_msg_) {
	if(_info_logfile.is_open()) {
		std::string severity_str = "UNKNOWN";
		switch (log_msg_._severity) {
		case BaseLogMessage::NOTICE:
			severity_str = "NOTICE";
			break;
		case BaseLogMessage::INFO:
			severity_str = "INFO";
			break;
		case BaseLogMessage::DEBUG:
			severity_str = "DEBUG";
			break;
		default:
			severity_str = "UNKNOWN";
			break;
		}
		time_t now = time(0);
#ifdef WIN32
		char* date_time = ctime(&now);
#else
		char date_time[30];
		ctime_r(&now, date_time);
#endif
		date_time[strlen(date_time) - 1] = '\0'; // replace \n with \0
		_info_logfile << date_time << ':' << _process_name << '[' << _pid << "]:";
		_info_logfile << severity_str << ':' << log_msg_._thread_name << ':';
		if(log_msg_._ipv4_addr.s_addr != 0) {
			_info_logfile << "srcIP=" << this->ipv4_as_dotted_decimal(log_msg_._ipv4_addr);
			if (!log_msg_._hostname.empty()) {
				_info_logfile << '('
				<< log_msg_._hostname << ')';
			}
			_info_logfile << ':';
		}
		_info_logfile << log_msg_._msg << std::endl;
		#if LOGGER_DEBUG
			std::cerr << "INFOLOG  - " << date_time << ':'
				<< _process_name << '[' << _pid << "]:"
				<< severity_str << ':' << log_msg_._thread_name << ':';
			if(log_msg_._ipv4_addr != 0) {
				std::cerr << "srcIP=" << this->ipv4_as_dotted_decimal(log_msg_._ipv4_addr);
				if (!log_msg_._hostname.empty()) {
					std::cerr << '('
					<< log_msg_._hostname << ')';
				}
				std::cerr << ':';
			}
			std::cerr << log_msg_._msg << std::endl;
		#endif
	}
}
// ---------------------------------------------------------------------------------------
/// Processes this thread's input queue.
void LoggerThread::run() {
	this->block_all_signals();
	BaseThread::set_unique_thread_str(_thread_name);
	std::ostringstream oss;
	oss << "ID=" << std::hex << this->id() << std::dec << " started";
	BaseLogMessage info_msg(BaseLogMessage::INFO, _thread_name, oss.str());
	this->log_info(info_msg);
	// Inform Thread Manager that a notification must be sent to this thread
	// when the date changes
	ThreadManager::get_instance()->post(new ThreadTimerEventRequestMessage(
		*this, MTL_Timer_Type::EVENT_NEW_DAY_TIMER, Logger_Thread_Timer_type::NEW_DAY_TIMER, true, 0));	
	BaseMessage* msg_ptr = 0;
	bool should_run = true;
	while(should_run) {
		try {
			this->wait_and_pop(msg_ptr);
			switch(msg_ptr->_type) {
			case MTL_Thread_Message::MTL_LOG_MESSAGE:  {
				BaseLogMessage const& log_msg = dynamic_cast<BaseLogMessage &>(*msg_ptr);
				if(log_msg._severity >= BaseLogMessage::NOTICE) {
					this->log_info(log_msg);
				} else {
					this->log_error(log_msg);
				}
				break;
			}
			case MTL_Thread_Message::MTL_LOG_MESSAGE_WITH_IP: {
				BaseLogMessage const& log_msg_with_ip = dynamic_cast<LogMessageWithIp &>(*msg_ptr);
				if(log_msg_with_ip._severity >= BaseLogMessage::NOTICE) {
					this->log_info(log_msg_with_ip);
				} else {
					this->log_error(log_msg_with_ip);
				}
				break;
			}
			case MTL_Thread_Message::MTL_STOP: {
				// we can't call MTL_LOG() here because we are exiting (i.e., if we log
				// a message into logger's input queue, it will never be processed)
				BaseLogMessage info_msg(BaseLogMessage::INFO, _thread_name, "STOP");
				this->log_info(info_msg);
				should_run = false;
				break;
			}
			// common cases:
			case MTL_Thread_Message::MTL_KEEP_ALIVE_REQUEST: {
				KeepAliveRequestMessage const& ka = dynamic_cast<KeepAliveRequestMessage&>(*msg_ptr);
				ThreadManager::get_instance()->post(new KeepAliveResponseMessage(
					*this, ka._counter, time(0)));
				break;
			}
			case MTL_Thread_Message::MTL_TIMER_EVENT_NOTIFY: {
				ThreadTimerEventNotifyMessage const& ttenm = dynamic_cast<ThreadTimerEventNotifyMessage&>(*msg_ptr);
				switch (ttenm._timer_type) {
				case MTL_Timer_Type::EVENT_NEW_DAY_TIMER: {
					this->rotate_logfiles();
					break;
				}
				default: {
					MTL_LOG(BaseLogMessage::ERROR, 
						"unexpected event notification (" << ttenm._timer_type << ")");
						break;
				}
				}
				break;
			}
			case MTL_Thread_Message::MTL_REPORT_THREAD_QUEUE_INFO: {
				BaseLogMessage info_msg(BaseLogMessage::INFO, _thread_name,
					"REPORT_THREAD_QUEUE_INFO");
				this->log_info(info_msg);
				ThreadManager::get_instance()->post(new ThreadQueueInfoMessage(
					_thread_name, this->size(), this->high_watermark(), this->max_queue_size()));
				break;
			}
			case MTL_Thread_Message::MTL_DUMP_OBJECTS: {
				std::ostringstream oss;
				oss << "--DUMP--\n" << *this;
				BaseLogMessage info_msg(BaseLogMessage::INFO, _thread_name, oss.str());
				this->log_info(info_msg);
				break;
			}
			default: {
				std::ostringstream oss;
				oss << "unexpected msg, type=" << msg_ptr->_type;
				BaseLogMessage error_msg(BaseLogMessage::WARNING, _thread_name, oss.str());
				this->log_error(error_msg);
				break;
			}
			} // end of switch()
		} catch(...) {
			ExceptionDispatcher::get_instance()->handle_exception();
		}
		delete msg_ptr; // we own it, so we must delete it
	}
	this->stop();
}
// ----------------------------------------------------------------------------
/// Attempts to create path_to_directory_, if it does not already exist.
/// @param[in] path_to_directory_ - path to directory
/// @return - true if directory already existed or was successfully created; false otherwise
/*static*/ bool LoggerThread::create_directory(std::string const& path_to_directory_) {
	struct stat st;
	if(stat(path_to_directory_.c_str(), &st) == 0) {
		// directory or file already exists
		if (S_ISDIR(st.st_mode)) {
			// directory already exists
		} else {
			// is a file
			BaseLogMessage error_msg(BaseLogMessage::ERROR,
				_thread_name, " "	+ path_to_directory_ + "is a file and not a directory.");
			this->log_error(error_msg);
			return false;
		}
	} else {
		#ifdef WIN32
		  if(mkdir(dir.c_str()) != 0) {
		#else		
			if(mkdir(path_to_directory_.c_str(), 0760) != 0) {
		#endif
				if (errno != EEXIST) {
					BaseLogMessage error_msg(BaseLogMessage::ERROR,
						_thread_name, " "	+ path_to_directory_ + ": " + strerror(errno));
					this->log_error(error_msg);
					return false;
				} // else directory already exists
			} // else directory is created
	}
	return true;
}
// ---------------------------------------------------------------------------------------
/// Creates a date directory under the logfiles directory for the current date, if that 
/// directory does not yet exist.
/// The full path to that directory is stored in _logfiles_date_path.
void LoggerThread::create_directory_for_today() {
	time_t now = time(0);
	struct tm timeinfo = *localtime(&now);
	char date_buf[12] = {0};  // why doesn't [9] work?
	strftime(date_buf, sizeof(date_buf)-1, "%Y%m%d", &timeinfo);
	std::string dir(_logfiles_path + '/' + date_buf);
	if (!this->create_directory(dir)) {
		_log_to_syslog = true;
	}
	_logfiles_date = std::string(date_buf);
	_logfiles_date_path = dir;
}
// ---------------------------------------------------------------------------------------
/// Closes the logfiles and opens new ones, in a directory based on today's date.
void LoggerThread::rotate_logfiles() {
	// check if current logfiles path exists (it is possible that it might have been deleted)
	struct stat st;
	if(stat(_logfiles_path.c_str(), &st) == 0) {
		// the logfiles directory still exists	
		BaseLogMessage info_msg(BaseLogMessage::INFO, _thread_name, "rotating log files");
		this->log_info(info_msg);
	} else {
		// logfiles directory does not exist
		_log_to_syslog = true;
		BaseLogMessage error_msg(BaseLogMessage::ERROR,
			_thread_name, " "	+ _logfiles_path + ": " + strerror(errno));
		this->log_error(error_msg);
	}
	_error_logfile.close();
	_info_logfile.close();
	this->open_error_logfile();
	this->open_info_logfile();
}
/// --------------------------------------------------------------------------------------
/// converts IPv4 address to standard dotted-decimal format string.
/// @param[in] ipv4_address_ - ipv4 address in network order
/// @return - IPv4 address string in standard dotted-decimal format.
std::string LoggerThread::ipv4_as_dotted_decimal(struct in_addr const& ipv4_address_) const {
#ifdef WIN32		// MinGW doesn't support inet_ntop()
	return inet_ntoa(ipv4_address_);
}
#else
	char addr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &ipv4_address_, addr, INET_ADDRSTRLEN);
	return addr;
}
#endif
// ---------------------------------------------------------------------------------------
/// Human readable representation of this object as a string.  Useful for debugging.
/// This method is not thread-safe, but no harm (other than possibly incorrect output)
/// should result if called by more than one thread simultaneously.
/// @param[in] os_ - ostream into which the string will be inserted
void LoggerThread::printOn(std::ostream& os_) const {
	os_ << "LoggerThread[log_to_syslog="	<< std::boolalpha << _log_to_syslog
		<< ",log_to_cerr_also="				<< std::boolalpha << _log_to_cerr_also
		<< ",logfiles_path="				<< _logfiles_path
		<< ",logfiles_date_path="			<< _logfiles_date_path
		<< "\n ,errorlog_filename="			<< _errorlog_filename
		<< ",infolog_filename="				<< _infolog_filename
		<< "\n ,error logfile good="		<< std::boolalpha << _error_logfile.good()
		<< ",info logfile good="			<< std::boolalpha << _info_logfile.good()
		<< "\n ,process_name="				<< _process_name
		<< ",software_version="				<< _software_version
		<< "\n ,";
	this->ThreadWithInputQueue::printOn(os_);
	os_ << ']';
}
