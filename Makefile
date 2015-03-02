
CXX=arm-none-linux-gnueabi-g++

all : pitest

VC := /opt/raspberry/sysroot/vc
INCLUDES := -I$(VC)/include -I$(VC)/include/interface/vcos/pthreads -I$(VC)/include/interface/vmcs_host/linux
LIBS := -L$(VC)/lib -lGLESv2 -lEGL -lbcm_host -lvchiq_arm -lvcos

pitest : main.cpp
	$(CXX) -std=c++11 $(LIBS) $(INCLUDES) -opitest main.cpp
