
PROJECT := insaned

CXXFLAGS=-Wall -Wextra -pedantic -pipe -O2 -std=c++11 -Isrc -g3


all : $(PROJECT)

$(PROJECT) : src/insaned.o src/InsaneDaemon.o src/InsaneException.o
	g++ $(CXXFLAGS) $^ -lsane -o $@

src/%.o : src/%.cpp src/%.h
	g++ $(CXXFLAGS) -c $< -o $@


.PHONY : clean

clean :
	rm -rf src/*.o $(PROJECT)

