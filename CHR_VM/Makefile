src = $(wildcard *.cpp)
obj = $(src:.c=.o)

LDFLAGS = -lpthread -ltbb

chrvm: $(obj)
	$(CXX) -o $@ $^ $(LDFLAGS) -std=c++14 -DNOGTEST -O2 -fopenmp -g

.PHONY: clean
clean:
	rm chrvm
	rm *.o
