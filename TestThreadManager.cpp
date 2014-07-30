#ifndef _TEST_THREAD_MANAGER_HPP_
#define _TEST_THREAD_MANAGER_HPP_

#include "ThreadManager.hpp"

class TestThreadManager : public virtual ThreadManager {
public:
	// -----------------------------------------------------------------------------------
	// -- class variables
	// use char* instead of std::string to ensure static initialization, see
	// http://stackoverflow.com/questions/459942/defining-class-string-constants-in-c
	static const char* const _test_thread_manager_name_;	///< thread's unique name
	// -----------------------------------------------------------------------------------
	// -- instance methods
	/// Constructor.
	TestThreadManager()
	: BaseThread(_test_thread_manager_name_)
	, ConcurrentQueue<BaseMessage*>::ConcurrentQueue()
	, ThreadWithInputQueue(_test_thread_manager_name_)
	, ThreadManager(_test_thread_manager_name_) {}
public:
	virtual unsigned int keep_alive_period() const {
		return 4;
	}
protected:
	virtual void thread_error(MTL_Thread_Error::ThreadError thread_error_type_,
		std::string const& thread_name_, std::string const& msg_) const {
		// do nothing
	};
	virtual void dump_objects() const {
		// do nothing
	};
	virtual void notify_main_thread_of_abnormal_shutdown() const {
		// do nothing
	};
	virtual void process_input_message(BaseMessage *msg_ptr_) {
		// do nothing
	};
	virtual void printOn(std::ostream& os_) const {
		os_	<< "TestThreadManager[";
		this->ThreadManager::printOn(os_);
		os_	<< ']';
	};
};

#endif
