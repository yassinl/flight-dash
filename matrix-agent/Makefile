RGB_LIB     = ../rpi-rgb-led-matrix
RGB_INCDIR  = $(RGB_LIB)/include
RGB_LIBDIR  = $(RGB_LIB)/lib
RGB_LIBRARY = $(RGB_LIBDIR)/librgbmatrix.a

CXXFLAGS     = -Wall -O3 -std=c++17 -I$(RGB_INCDIR)
CXXFLAGS_CON = -Wall -O3 -std=c++17   # no matrix include needed
LDFLAGS      = -L$(RGB_LIBDIR) -lrgbmatrix -lrt -lm -lpthread -lcurl
LDFLAGS_CON  = -lrt -lm -lpthread -lcurl

OBJS = driver.o flight_data.o route_lookup.o

all: driver

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)

driver: $(OBJS) $(RGB_LIBRARY)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# Console-only test binary — no rpi-rgb-led-matrix dependency.
# Compile flight_data.cc with the console flags (no matrix include path needed).
test-console: test_console.cc flight_data.cc route_lookup.cc flight_data.h route_lookup.h
	$(CXX) $(CXXFLAGS_CON) test_console.cc flight_data.cc route_lookup.cc -o $@ $(LDFLAGS_CON)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f driver test-console $(OBJS) test_console.o

.PHONY: all test-console clean
