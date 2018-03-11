all:
	g++ -std=c++11 -Wall -Wextra -pedantic -o client client.cpp network.cpp utility.cpp file.cpp
	g++ -std=c++11 -Wall -Wextra -pedantic -o server/server server/server.cpp network.cpp utility.cpp file.cpp
