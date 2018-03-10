all:
	g++ -std=c++11 -Wall -Wextra -pedantic -o client client.cpp
	g++ -std=c++11 -Wall -Wextra -pedantic -o server server.cpp
