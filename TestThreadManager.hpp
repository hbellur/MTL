#include "TestThreadManager.hpp"

// =======================================================================================
const char* const TestThreadManager::_test_thread_manager_name_ = "TM";
// =======================================================================================
// implement ThreadManager's static get_instance() method
ThreadManager* ThreadManager::get_instance() {
	if(_instance_ == 0) {
		_instance_ = new TestThreadManager();
	}
	return _instance_;
}
