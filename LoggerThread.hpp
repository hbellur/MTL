#ifndef _LOGGER_THREAD_HPP_
#define _LOGGER_THREAD_HPP_
/** @file LoggerThread.hpp
 * @class LoggerThread
 * A singleton that buffers (pointers to) BaseLogMessage messages and eventually sends
 * them to one of two files:
 *   @li error logfile -- or to syslog if the error logfile is not opened
 *   @li info logfile  -- NOTICE, INFO, and DEBUG "errors" get logged here
 * Other objects call log() to buffer messages.
 * To handle errors that arise when attempting to post() or log(), these methods are
 * overridden -- the Logger can't be trying to log to itself.
 * Files are rotated daily, and are stored in _logfiles_path/yyyymmdd \n
 * Design considerations: \n
 * get_instance() is not thread safe, so the instance has to be created by the MAIN
 * thread before any other threads are spawned.
 */
/// Set to 1 to send all logs to std::cerr also
#define LOGGER_DEBUG 0

#include <ostream>
#include <sstream>
#include <fstream>
#include <string>
#include <boost/foreach.hpp>
#if defined(WIN32) || defined(LOGGER_DEBUG)
	#include <boost/date_time/local_time/local_time.hpp>
#endif
#ifdef WIN32
	#include <process.h>	// for _getpid()
#else
	#include <syslog.h>
#endif
#include <vector>
#include "ThreadWithInputQueue.hpp"
#include "ThreadManager.hpp"
#include "MTL_Common.hpp"

// =======================================================================================
// -- constants, in their own namespaces
namespace Logger_Thread_Timer_type {
	/// Unique timer types that identifies the timer subscribed
	const int NEW_DAY_TIMER		=  0;	///< new day timer
};

class LoggerThread : public virtual ThreadWithInputQueue {
	// -----------------------------------------------------------------------------------
	// -- constants
protected:
	enum {
		/// max thread input queue size (100,000)
		MAX_QUEUE_SIZE = 100000,
		/// max number of subscribers to the New Day event 
		EVENT_NEW_DAY_SUBSCRIBERS_MAX = 25,
	};
	// -----------------------------------------------------------------------------------
	// -- typedefs
protected:
	/// typedef for pid
	#ifdef WIN32
		typedef int MTL_pid;
	#else
		typedef pid_t MTL_pid;
	#endif
	// -----------------------------------------------------------------------------------
	// -- class variables
protected:
	static LoggerThread* _instance_;	///< singleton object
	// -----------------------------------------------------------------------------------
	// -- class methods
public:
	/// The single instance of this class.  If the single instance does not exist it is
	/// created.  Note, this method is not thread-safe.  It must first be called by the
	/// main thread before any other threads are started.  
	/// Then it can be considered thread-safe.
	/// @return - the single instance of this class
	static LoggerThread* get_instance() {
		if(_instance_ == 0) {
			_instance_ = new LoggerThread();
		}
		return _instance_;
	}
	// -----------------------------------------------------------------------------------
	// -- instance variables
	bool 			_log_to_syslog;			///< indicates if logging to syslog or to a file
	bool 			_log_to_cerr_also;		///< indicates if logging should also go to std::cerr
	std::string		_logfiles_path;			///< path to dir where log files are stored
	std::string		_logfiles_date;			///< name of current date dir under _logfiles_path
	std::string		_logfiles_date_path;	///< path to current date dir 
	std::string		_errorlog_filename;		///< error log filename
	std::string		_infolog_filename;		///< info  log filename
	std::ofstream	_error_logfile;			///< error logfile
	std::ofstream	_info_logfile;			///< info  logfile
	std::string		_process_name;			///< process name
	std::string		_software_version;		///< program's software version
	MTL_pid			_pid;					///< process ID
	// -----------------------------------------------------------------------------------
	// -- instance methods
protected:
	/// Constructs the object.
	/// Error messages are sent to syslog until open_error_logfile() is called.
	/// Error messages are also sent to std::cerr until told not to.
	LoggerThread()
	: BaseThread(ThreadManager::_logger_thread_name_)
	, ConcurrentQueue<BaseMessage*>::ConcurrentQueue(MAX_QUEUE_SIZE)
	, ThreadWithInputQueue(ThreadManager::_logger_thread_name_)
	, _log_to_syslog(true)		// log to syslog until _error_logfile gets set
	, _log_to_cerr_also(true)	// log to std::cerr until told not to
	, _logfiles_path()
	, _logfiles_date()
	, _logfiles_date_path()
	, _errorlog_filename()
	, _infolog_filename()
	, _error_logfile()
	, _info_logfile()
	, _process_name()
	, _software_version()
	, _pid(
		#ifdef WIN32
			_getpid()
		#else
			getpid()
		#endif
	  ) {}
	// --------------------------------------------------------------
public:
	/// Destructor.  Following RAII, close the logfiles.
	virtual ~LoggerThread() {
		_error_logfile.close();
		_info_logfile.close();
	}
	// --------------------------------------------------------------
public:
	bool configure(std::string const& logfiles_path_,
		std::string const& errorlog_filename_,  std::string const& infolog_filename_,
		std::string const& process_name_, std::string const& software_version_);
	void open_logfiles(void);
	bool post(BaseMessage* const& msg_ptr_);
	bool post_high_priority(BaseMessage* const& msg_ptr_);
	void log(BaseLogMessage* const& log_msg_ptr_);
	void log(LogMessageWithIp* const& log_msg_ptr_);
	void run();
	void stop();
	/// Stops logging to std::cerr also
	void stop_logging_to_cerr() {
		_log_to_cerr_also = false;
	}
	std::string ipv4_as_dotted_decimal(struct in_addr const& ipv4_address_) const;
protected:
	void printOn(std::ostream& os_) const;
	bool create_directory(std::string const& path_to_directory_);
	void create_directory_for_today();
	void open_error_logfile();
	void open_info_logfile();
	void log_error(BaseLogMessage   const& log_msg_);
	void log_error(LogMessageWithIp const& log_msg_);
	void log_info (BaseLogMessage   const& log_msg_);
	void log_info (LogMessageWithIp const& log_msg_);
	void rotate_logfiles();
};
// =======================================================================================
// -- macros
/// Log()s stream_insertable_content_ to the Logger with severity_.
/// Thread name is added.
#define MTL_LOG(severity_, stream_insertable_content_) \
	{ \
		std::ostringstream logger_oss; \
		logger_oss << stream_insertable_content_; \
		LoggerThread::get_instance()->log(new BaseLogMessage( \
			severity_, BaseThread::unique_thread_str(), logger_oss.str())); \
	}
/// The following for for convenience 
#define MTL_LOG_INFO(stream_insertable_content_) \
	{ \
		MTL_LOG(BaseLogMessage::INFO, stream_insertable_content_); \
	}
#define MTL_LOG_WARN(stream_insertable_content_) \
	{ \
		MTL_LOG(BaseLogMessage::WARNING, stream_insertable_content_); \
	}
#define MTL_LOG_ERR(stream_insertable_content_) \
	{ \
		MTL_LOG(BaseLogMessage::ERROR, stream_insertable_content_); \
	}
/// Log()s stream_insertable_content_ to the Logger with severity_.
/// thread_name_ allows the log to look like it was logged from another thread.
#define MTL_LOG_WITH_THREAD_NAME(thread_name_, severity_, stream_insertable_content_) \
	{ \
		std::ostringstream logger_oss; \
		logger_oss << stream_insertable_content_; \
		LoggerThread::get_instance()->log(new BaseLogMessage(\
			severity_, thread_name_, logger_oss.str())); \
	}
/// Like MTL_LOG(), but also includes the _source_hostname and _source_ip.
/// Note that use of this macro requires that instance variables
/// _source_hostname and _source_ip be available.
#define MTL_LOG_WITH_SOURCE_IP(severity_, stream_insertable_content_) \
	{ \
		std::ostringstream logger_oss; \
		logger_oss << stream_insertable_content_; \
		LoggerThread::get_instance()->log(new LogMessageWithIp(\
			severity_, BaseThread::unique_thread_str(), logger_oss.str(),\
			_source_hostname, _source_ip)); \
	}

#endif
