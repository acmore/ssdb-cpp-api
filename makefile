DIR = ./ 
CXX = g++
AR = ar
CXXFLAGS = -g -O2 -Wall -pipe
INC = 
LIB = 

TARGET = libssdbclient.a

OBJS = buffer.o ssdb_client.o

all : $(TARGET)
$(TARGET) : $(OBJS)
	$(AR) rc $(TARGET) $(OBJS)
buffer.o: buffer.c
	$(CXX) -c $(CXXFLAGS) $< -o $@ $(INC)
ssdb_client.o: ssdb_client.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@ $(INC)
clean :
	@rm -f *.gch $(TARGET)
	@find $(DIR) -name '*.o' | xargs rm -f
