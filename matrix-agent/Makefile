RGB_LIB     = ../rpi-rgb-led-matrix
RGB_INCDIR  = $(RGB_LIB)/include
RGB_LIBDIR  = $(RGB_LIB)/lib
RGB_LIBRARY = $(RGB_LIBDIR)/librgbmatrix.a

CXXFLAGS = -Wall -O3 -std=c++17 -I$(RGB_INCDIR)
LDFLAGS  = -L$(RGB_LIBDIR) -lrgbmatrix -lrt -lm -lpthread -lcurl

OBJS = driver.o flight_data.o

all: driver

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)

driver: $(OBJS) $(RGB_LIBRARY)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f driver $(OBJS)

.PHONY: all clean
