default: sftpd sftp

sftpd:
	g++ src/FTPd.cpp src/Server.hpp -o ./bin/sftpd -Wall -Werror -std=c++11 -lpthread

sftp:
	g++ src/FTPClient.cpp -o ./bin/sftp -Wall -Werror -std=c++11

clean:
	@rm ./bin/sftpd ./bin/sftp
