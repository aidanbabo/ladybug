a.out: main.cpp
	clang++ main.cpp -g --std=c++20 -lssl -lcrypto -lz
