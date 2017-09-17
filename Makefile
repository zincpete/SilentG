
CXXFLAGS=-std=c++11

all: Curtains

Curtains: SilentG.o Curtains.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $+ -o $@ -lwiringPi

clean:
	$(RM) *.o Curtains

