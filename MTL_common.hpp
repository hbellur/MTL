#ifndef _MTL_COMMON_HPP_
#define _MTL_COMMON_HPP_
/** @file MTL_Common.hpp
 * Defines message classes used by inter-thread messaging.
 * Note, 'MTL' = reusable multi-threading (library)
 */
#ifdef WIN32
	#include <winsock2.h>
#else
	#include <netinet/in.h>
#endif
#include <stdint.h>
#include <boost/noncopyable.hpp>

typedef uint16_t BaseMessageType;	///< Base Message Type

class BaseThread;
// =======================================================================================
// -- constants, in their own namespaces
namespace MTL_Thread_Message {
	/// Message types.  Values 0 - 100 are reserved for the MTL.
	/// Values above 9999 are deemed "not critical" -- see ThreadWithInputQueue::post().
	const BaseMessageType MTL_UNDEFINED					=  1;	///< used for default construction only
	const BaseMessageType MTL_THREAD_CREATED				= 10;	///< thread -> TM
	const BaseMessageType MTL_STOP							= 20;	///< parent -> child
	const BaseMessageType MTL_CHILD_THREAD_STOPPED			= 21;	///< child  -> parent
	const BaseMessageType MTL_THREAD_STOPPED_ABNORMALLY	= 22;	///< child  -> parent
	const BaseMessageType MTL_DELETE_THREAD				= 23;	///< parent or thread -> TM
	const BaseMessageType MTL_KEEP_ALIVE_REQUEST			= 30;	///< TM     -> thread
	const BaseMessageType MTL_KEEP_ALIVE_RESPONSE			= 31;	///< thread -> TM
	const BaseMessageType MTL_LOG_MESSAGE					= 40;	///< thread -> Logger
	const BaseMessageType MTL_LOG_MESSAGE_WITH_IP			= 41;	///< thread -> Logger
	const BaseMessageType MTL_THREAD_ERROR					= 45;	///< thread -> TM
	const BaseMessageType MTL_ABNORMAL_SHUTDOWN			= 50;	///< exception-> TM, TM -> other threads
	const BaseMessageType MTL_REPORT_THREAD_QUEUE_INFO		= 60;	///< thread -> TM
	const BaseMessageType MTL_THREAD_QUEUE_INFO			= 61;	///< thread -> TM
	const BaseMessageType MTL_DUMP_OBJECTS					= 70;	///< thread -> TM
	const BaseMessageType MTL_TIMER_EVENT_REQUEST		= 71;	///< thread -> TM
	const BaseMessageType MTL_TIMER_EVENT_NOTIFY		= 72;	///< TM -> thread
	/// Other libraries and projects must use values above 100 for defining
	/// additional message types.  They should use the UMSG() macro. 
	const BaseMessageType MTL_USER_MESSAGE_START           = 101;	///< thread -> TM
	const BaseMessageType MTL_CRITICAL_MESSAGE_END			= 9999;	///< end of critical msgs
};
// ---------------------------------------------------------------------------------------
namespace MTL_Timer_Type {
	/// Event types.
	enum Timer_Type {
		EVENT_RELATIVE_TIMER,		///< timer starts when the Thread Manager receives MTL_TIMER_EVENT_REQUEST
		EVENT_NEW_DAY_TIMER			///< Thread Manager notifies the subscriber threads when a new day arrives
	};
};
// ---------------------------------------------------------------------------------------
namespace MTL_Thread_Error {
	enum ThreadError {
		INPUT_QUEUE_DROPPED_CRITICAL_MSG,
		INPUT_QUEUE_DROPPED_MSG,
		THREAD_KEEP_ALIVE,
	};
};
// =======================================================================================
// -- classes
/// Base class for all messages that are post()ed to threads' input queues.
/// For efficiency reasons, do not allow message copying or assignment.
class BaseMessage : boost::noncopyable {
public:
	BaseMessageType _type;		///< message type
	// -------------------------------------------
	/// default constructor
	BaseMessage()
	: _type(MTL_Thread_Message::MTL_UNDEFINED) {}
	// -------------------------------------------
	/// constructor with type_
	/// @param[in] type_ - message type
	explicit BaseMessage(BaseMessageType type_)
	: _type(type_) {}
	// -------------------------------------------
	/// does nothing
	virtual ~BaseMessage() {}
};
// ---------------------------------------------------------------------------------------
/// Base class for messages that are sent to the system-wide logging thread.
/// For efficiency reasons, do not allow message copying or assignment.
class BaseLogMessage : public virtual BaseMessage {
public:
	#ifdef WIN32
		#undef ERROR
	#endif
	/// error/syslog severities, same as syslog
	enum MTL_ErrorSeverity {
		 EMERGENCY = 0
		,ALERT
		,CRITICAL
		,ERROR		/// @todo: does this cause a problem under MinGW on Windows?
		,WARNING
		,NOTICE
		,INFO
		,DEBUG
	};
	const MTL_ErrorSeverity	_severity;		///< message severity
	const std::string		_thread_name;	///< name of thread logging the error
	const std::string		_msg;			///< human-readable message to log
	// -------------------------------------------
	/// For messages intended for the error log or info log
	/// @param[in] severity_		- severity for the error or info, must be <= WARNING
	/// @param[in] thread_name_		- name of thread logging the error
	/// @param[in] msg_				- message to be logged
	BaseLogMessage(MTL_ErrorSeverity severity_, std::string const& thread_name_,
	  std::string const& msg_)
	: BaseMessage(MTL_Thread_Message::MTL_LOG_MESSAGE)
	, _severity(severity_)
	, _thread_name(thread_name_)
	, _msg(msg_) {}
	// -------------------------------------------
	virtual ~BaseLogMessage() {}
};
// ---------------------------------------------------------------------------------------
/// Used for messages with IP information that are sent to the system-wide
/// logging thread.
/// For efficiency reasons, do not allow message copying or assignment.
class LogMessageWithIp : public virtual BaseLogMessage {
public:
	const std::string		_hostname;	///< hostname
	const struct in_addr	_ipv4_addr;	///< IPv4 address
	// -------------------------------------------
	/// For messages intended for the error log or info log
	/// @param[in] severity_	- severity for the error or info, must be <= WARNING
	/// @param[in] thread_name_	- name of thread logging the error
	/// @param[in] msg_			- message to be logged
	/// @param[in] hostname_	- hostname
	/// @param[in] ipv4_addr_	- IPv4 address
	LogMessageWithIp(MTL_ErrorSeverity severity_, std::string const& thread_name_,
	  std::string const& msg_, std::string const& hostname_, struct in_addr ipv4_addr_)
	: BaseMessage(MTL_Thread_Message::MTL_LOG_MESSAGE_WITH_IP)
	, BaseLogMessage(severity_, thread_name_, msg_)
	, _hostname(hostname_)
	, _ipv4_addr(ipv4_addr_) {}
	// -------------------------------------------
	virtual ~LogMessageWithIp() {}
};
// ---------------------------------------------------------------------------------------
/// For notifying the thread manager that a new thread has been created
class ThreadCreatedMessage : public virtual BaseMessage {
public:
	BaseThread& _thread;	///< the thread that was created
	// -------------------------------------------
	/// constructor
	/// @param[in] thread_ - the thread that was created
	explicit ThreadCreatedMessage(BaseThread& thread_)
	: BaseMessage(MTL_Thread_Message::MTL_THREAD_CREATED)
	, _thread(thread_) {}
	// -------------------------------------------
	virtual ~ThreadCreatedMessage() {}
};
// ---------------------------------------------------------------------------------------
/// For notifying a parent thread that a child thread has stopped/exited.
/// Note that the thread might still be running.  join() must be used
/// to ensure that the thread has stopped/exited completely.
class ThreadStoppedMessage : public virtual BaseMessage {
public:
	BaseThread& _thread;	///< the thread has stopped/exited (is exiting)
	// -------------------------------------------
	/// constructor
	/// @param[in] thread_ - the thread that has stopped/exited (is exiting)
	explicit ThreadStoppedMessage(BaseThread& thread_)
	: BaseMessage(MTL_Thread_Message::MTL_CHILD_THREAD_STOPPED)
	, _thread(thread_) {}
	// -------------------------------------------
	virtual ~ThreadStoppedMessage() {}
};
// ---------------------------------------------------------------------------------------
/// For notifying the thread manager that a child thread has stopped/exited
/// and it is now ready to be join()ed and then deleted.
/// VERY IMPORTANT NOTE: after the sender sends this message, the sender
/// MUST consider the thread object to no longer be valid (since
/// the Thread Manager may run, process the DeleteThreadMessage
/// and delete the object)!
class DeleteThreadMessage : public virtual BaseMessage {
public:
	BaseThread& _thread;	///< the thread to be deleted
	// -------------------------------------------
	/// constructor
	/// @param[in] thread_ - the thread to be deleted
	explicit DeleteThreadMessage(BaseThread& thread_)
	: BaseMessage(MTL_Thread_Message::MTL_DELETE_THREAD)
	, _thread(thread_) {}
	// -------------------------------------------
	virtual ~DeleteThreadMessage() {}
};
// ---------------------------------------------------------------------------------------
/// Notifies the parent thread that a child UDP thread has stopped/exited abnormally.
/// Note that the thread might still be running.  join() must be used
/// to ensure that the thread has stopped/exited completely.
class ThreadStoppedAbnormallyMessage : public virtual BaseMessage {
public:
	BaseThread& _thread;	///< the thread that has stopped/exited (is exiting)
	// -------------------------------------------
	/// constructor
	/// @param[in] thread_ - the thread that has stopped/exited (is exiting)
	explicit ThreadStoppedAbnormallyMessage(BaseThread& thread_)
	: BaseMessage(MTL_Thread_Message::MTL_THREAD_STOPPED_ABNORMALLY)
	, _thread(thread_) {}
	// -------------------------------------------
	virtual ~ThreadStoppedAbnormallyMessage() {}
};
// ---------------------------------------------------------------------------------------
/// KeepAlive request from thread manager
class KeepAliveRequestMessage : public virtual BaseMessage {
public:
	unsigned int	_counter;	///< counter; associates a request with a response
	// -------------------------------------------
	/// constructor
	/// @param[in] counter_   - counter that was in the keep-alive request
	explicit KeepAliveRequestMessage(unsigned int counter_)
	: BaseMessage(MTL_Thread_Message::MTL_KEEP_ALIVE_REQUEST)
	, _counter(counter_) {}
	// -------------------------------------------
	virtual ~KeepAliveRequestMessage() {}
};
// ---------------------------------------------------------------------------------------
/// KeepAlive reponse to thread manager
class KeepAliveResponseMessage : public virtual BaseMessage {
public:
	BaseThread&			_thread;	///< thread responding to the keep-alive request
	const unsigned int	_counter;	///< counter that was in the keep-alive request
	const time_t		_timestamp;	///< time when the responding thread sent this message
	// -------------------------------------------
	/// constructor
	/// @param[in] thread_    - thread responding to keep-alive requeuest
	/// @param[in] counter_   - counter that was in the keep-alive request
	/// @param[in] timestamp_ - time when the responding thread sent this message
	KeepAliveResponseMessage(BaseThread& thread_, unsigned int counter_, time_t const& timestamp_)
	: BaseMessage(MTL_Thread_Message::MTL_KEEP_ALIVE_RESPONSE)
	, _thread(thread_)
	, _counter(counter_)
	, _timestamp(timestamp_) {}
	// -------------------------------------------
	virtual ~KeepAliveResponseMessage() {}
};
// ---------------------------------------------------------------------------------------
/// Contains information for one thread's input queue
class ThreadQueueInfoMessage : public virtual BaseMessage {
public:
	const std::string	_thread_name;		///< thread name
	const size_t		_queue_size;		///< thread's current queue size
	const size_t		_high_watermark;	///< queue high watermark
	const size_t		_max_size;			///< max size queue can grow to
	// -------------------------------------------
	/// constructor
	/// @param[in] thread_name_ - thread's unique
	/// @param[in] thread_queue_size_ - current size of thread's input queue
	/// @param[in] thread_queue_high_watermark_ - thread's input queue high watermark
	/// @param[in] thread_queue_max_size_ - max allowable size of thread's input queue
	ThreadQueueInfoMessage(std::string const& thread_name_, size_t thread_queue_size_,
		size_t thread_queue_high_watermark_, size_t thread_queue_max_size_)
	: BaseMessage(MTL_Thread_Message::MTL_THREAD_QUEUE_INFO)
	, _thread_name(thread_name_) 
	, _queue_size(thread_queue_size_)
	, _high_watermark(thread_queue_high_watermark_)
	, _max_size(thread_queue_max_size_) {}
	// -------------------------------------------
	virtual ~ThreadQueueInfoMessage() {}
};
// ---------------------------------------------------------------------------------------
/// Used for Timer event subscription 
class ThreadTimerEventRequestMessage : public virtual BaseMessage {
public:
	BaseThread&	_thread;	///< thread sending event subscription
	MTL_Timer_Type::Timer_Type _timer_type;	///< EVENT_RELATIVE_TIMER or EVENT_NEW_DAY_TIMER
	const int		_thread_specific_timer_type;	///< unique value that helps threads to identify the timer.
																	///< This helps when a thread subscribes to multiple timers. Upon 
																	///< receiving timer expiration notification (ThreadTimerEventNotifyMessage),
																	///< the subscriber thread will take appropriate action based on this
																	///< unique value.
	bool				_periodic_timer;		///< true, if this is a periodic timer. false, if it is a one shot timer
	const int 	_timer_in_seconds;	///< must be >= keep alive period value the application has set. This value
																	///< is ignored if _timer_type is EVENT_NEW_DAY_TIMER
	// -------------------------------------------
	/// constructor
	/// @param[in] thread_											- thread sending timer event subscription
	/// @param[in] timer_type_									- EVENT_RELATIVE_TIMER or EVENT_NEW_DAY_TIMER
	/// @param[in] thread_specific_timer_type_	- unique value to identify the timer
	/// @param[in] periodic_timer_							- true, if this is a periodic timer. false, if it is a one shot timer
	/// @param[in] timer_in_seconds_						- must be >= keep alive period value the application has set. This value
	///																						is ignored if timer_type_ is EVENT_NEW_DAY_TIMER
	ThreadTimerEventRequestMessage(BaseThread& thread_, MTL_Timer_Type::Timer_Type timer_type_,
		int thread_specific_timer_type_, bool periodic_timer_, int timer_in_seconds_)
	: BaseMessage(MTL_Thread_Message::MTL_TIMER_EVENT_REQUEST)
	, _thread(thread_)
	, _timer_type(timer_type_)
	, _thread_specific_timer_type(thread_specific_timer_type_)
	, _periodic_timer(periodic_timer_)
	, _timer_in_seconds(timer_in_seconds_) {}
	// -------------------------------------------
	virtual ~ThreadTimerEventRequestMessage() {}
};
/// Used for Timer event Notification 
/// If a thread has subscribed to multiple timers, upon receiving this notification,
/// _timer_in_seconds value will be used to distinguish which timer expired.
class ThreadTimerEventNotifyMessage: public virtual BaseMessage {
public:
	MTL_Timer_Type::Timer_Type _timer_type;	///< EVENT_RELATIVE_TIMER or EVENT_NEW_DAY_TIMER
	const int		_thread_specific_timer_type;	///< unique value that helps threads to identify which timer expired.
	// -------------------------------------------
	/// constructor
	/// @param[in] timer_type_									- EVENT_RELATIVE_TIMER or EVENT_NEW_DAY_TIMER
	/// @param[in] thread_specific_timer_type_	- unique value to identify the timer
	ThreadTimerEventNotifyMessage(MTL_Timer_Type::Timer_Type timer_type_,
		int thread_specific_timer_type_)
	: BaseMessage(MTL_Thread_Message::MTL_TIMER_EVENT_NOTIFY)
	, _timer_type(timer_type_)
	, _thread_specific_timer_type(thread_specific_timer_type_) {}
	// -------------------------------------------
	virtual ~ThreadTimerEventNotifyMessage() {}
};
// ---------------------------------------------------------------------------------------
/// Contains information for one thread's input queue
class ThreadErrorMessage : public virtual BaseMessage {
public:
	const MTL_Thread_Error::ThreadError	_thread_error_type;	///< thread error type
	const std::string					_thread_name;		///< thread name
	const std::string					_error_msg;			///< error message
	// -------------------------------------------
	/// constructor
	/// @param[in] thread_error_type_ - thread error type
	/// @param[in] thread_name_       - name of errored thread 
	/// @param[in] error_msg_         - error message
	ThreadErrorMessage(MTL_Thread_Error::ThreadError thread_error_type_,
		std::string const& thread_name_, std::string const& error_msg_)
	: BaseMessage(MTL_Thread_Message::MTL_THREAD_ERROR)
	, _thread_error_type(thread_error_type_) 
	, _thread_name(thread_name_)
	, _error_msg(error_msg_) {}
	// -------------------------------------------
	virtual ~ThreadErrorMessage() {}
};

#endif
