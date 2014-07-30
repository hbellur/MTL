// To test the library on Linux:
// $ make RELEASE=1         # build the library
// $ make RELEASE=1 test    # build this test program
// $ release/testmtl
// or to use the debug version with symbols:
// $ make
// $ make test
// $ debug/testmtl

#include "TestThreadManager.hpp"
#include "LoggerThread.hpp"

int main(int argc_, char* argv_[]) {
	std::cout
		<< *ThreadManager::get_instance() << "\n"
		<< *LoggerThread::get_instance() << std::endl;
	return 0;
}
