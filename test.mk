# makefile fragment to make test.exe, the unit test program.
TEST_SOURCES = $(wildcard test/*.cpp)
## This is a list of full paths to the .o files we want to build
TEST_OBJECTS = $(patsubst %, build_test/%.o, $(TEST_SOURCES))

CXXFLAGS += -fPIC

build_test/%.cpp.o: %.cpp
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# this probably goes away with 0.6.1
# but for now our execs need to dynlink to pthread
mytest : LDFLAGS += -lpthread

test : mytest

## cleantest will clean out all the test and perf build products
cleantest :
	rm -rfv build_test
	rm -fv mytest

mytest : $(TEST_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)
