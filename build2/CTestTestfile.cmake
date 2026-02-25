# CMake generated Testfile for 
# Source directory: /home/runner/work/dingusppc/dingusppc
# Build directory: /home/runner/work/dingusppc/dingusppc/build2
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(testppc "/home/runner/work/dingusppc/dingusppc/build2/bin/testppc")
set_tests_properties(testppc PROPERTIES  WORKING_DIRECTORY "/home/runner/work/dingusppc/dingusppc/build2/bin" _BACKTRACE_TRIPLES "/home/runner/work/dingusppc/dingusppc/CMakeLists.txt;254;add_test;/home/runner/work/dingusppc/dingusppc/CMakeLists.txt;0;")
subdirs("core")
subdirs("cpu/ppc")
subdirs("debugger")
subdirs("devices")
subdirs("machines")
subdirs("utils")
subdirs("thirdparty/loguru")
subdirs("thirdparty/cubeb")
