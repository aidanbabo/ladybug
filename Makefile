a.out: main.cpp
	clang++ main.cpp --std=c++20 -lssl -lcrypto
