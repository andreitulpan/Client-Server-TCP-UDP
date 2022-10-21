#include <bits/stdc++.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "helpers.h"

class Subscriber {
 public:
	char id[10];
	char *eth_addr;
	int portno;

	Subscriber(char *id_client, char *addr, char *port) {
		// La initializarea clientului setez port-ul
		// si ip-ul serverului si id-ul de client
		memcpy(id, id_client, 10);
		eth_addr = addr;
        portno = atoi(port);
        DIE(portno == 0, "atoi");
	};

	~Subscriber() {
		// Inchid socket-ul TCP
		close(socketTCP);
	};

	/* Eroare de utilizare la pornirea clientului */
	static void usage(char *file) {
		fprintf(stderr, "Usage: %s server_address server_port\n", file);
		exit(0);
	}

	/* Functia publica care ruleaza clientul */
	void run() {
        initialize();
        run_client();
    }

 private:

	int n, ret, running, socketTCP;
	struct sockaddr_in serv_addr;

	fd_set read_fds;	// Multimea de citire folosita in select()
	fd_set tmp_fds;		// Multime folosita temporar
	int fdmax;			// Valoare maxima fd din multimea read_fds

	// Buffer-ul pentru primirea pachetelor
	int buffer_size;
	char buffer[sizeof(TCPpacket)];

	/* Functia initializeaza serverul pentru o buna functionare */
	void initialize() {

		// Initializez buffer-ul pentru primirea pachetelor
		buffer_size = 0;
		memset(buffer, 0, sizeof(TCPpacket));
		// Initializez socket-ul TCP
		socketTCP = socket(AF_INET, SOCK_STREAM, 0);
		DIE(socketTCP < 0, "socket");

		// Dezactivez algoritmul Nagle
		int one = NAGLE_OFF;
		ret = setsockopt(socketTCP, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		DIE(ret < 0, "nagle algo error");

		// Socket-ul de ethernet pentru server
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(portno);
		ret = inet_aton(eth_addr, &serv_addr.sin_addr);
		DIE(ret == 0, "inet_aton");

		// Ma conectez la server
		ret = connect(socketTCP, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
		DIE(ret < 0, "connect");

		// Se trimite ID-ul catre server
		send_id_connect();

		 // Se goleste multimea de descriptori de citire si multimea temporara
		FD_ZERO(&read_fds);
		FD_ZERO(&tmp_fds);

		// Se adauga socket-urile pe care se asculta conexiunea si stdin-ul in multimea read_fds
		FD_SET(socketTCP, &read_fds);
		FD_SET(STDIN_FILENO,&read_fds);
		fdmax = socketTCP + STDIN_FILENO;

		// Se seteaza starea TRUE pentru a rula clientul
		running = TRUE;
	}

	/* Functia executa rularea clientului */
	void run_client() {
		while (running) {
			tmp_fds = read_fds;

			// Folosesc functia select pentru multiplexare intrarilor
			int rc = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
			DIE(rc < 0, "select");

			// Se verifica daca se citeste input
			if(FD_ISSET(STDIN_FILENO, &tmp_fds)) {
				char buf[256];
				if (!fgets(buf, sizeof(buf), stdin))
					DIE(ferror(stdin), "stdin error");

				// Inchid server-ul daca se primeste comanda exit
				if (close_client(buf))
					break;

				// Impart comanda in substring-uri
				std::vector<std::string> strings;
				char delim[3] = " \n";
				split(buf, delim, strings);

				// Verific daca s-a primit comanda de subscribe
				if (strings.size() == 3 && !strings[0].compare("subscribe")) {
					// Trimit comanda de subscribe catre server
					subscribe(strings[1], strings[2]);
				// Verific daca s-a primit comanda de unsubscribe
				} else if (strings.size() == 2 && !strings[0].compare("unsubscribe")) {
					// Trimit comanda de unsubscribe catre server
					unsubscribe(strings[1]);
				}

			// Se verifica daca se primit pachete pe socket-ul TCP
			} else if (FD_ISSET(socketTCP, &tmp_fds)) {
				if (receive())
					break;

			}
		}
	}

	/* Functia verifica daca se primeste comanda
	   exit si inchide rularea clientului */
	bool close_client(char *buf) {
		if (!strcmp(buf, "exit\n")) {
			// Inchid rularea clientului
			running = FALSE;
			return true;
		}
		return false;
	}

	/* Functia trimite ID-ul catre server la conectare */
	void send_id_connect() {
		TCPrequest tcp_req;
		memcpy(&tcp_req.id, id, sizeof(id));
		tcp_req.type = 1;
		memset(&tcp_req.payload, 0, sizeof(tcp_req.payload));
		tcp_req.data_size = 0;

		n = send(socketTCP, &tcp_req, sizeof(id), 0);
		DIE(n < 0, "send id error");
	}

	/* Functia trimite comanda de subscribe catre server */
	void subscribe(std::string input, std::string type) {
		TCPrequest tcp_req;
		memcpy(&tcp_req.id, id, sizeof(id));
		tcp_req.type = 2;
		char *sf = (char *)malloc(2 * sizeof(char));
		strcpy(sf, "0");
		if (type == "1")
			strcpy(sf, "1");
		tcp_req.data_size = htonl(sizeof(char) + input.size()
							+ sizeof(tcp_req.id) + sizeof(tcp_req.data_size));
		memset(&tcp_req.payload, 0, sizeof(tcp_req.payload));
		memcpy(&tcp_req.payload, input.c_str(), input.size());
		strcat(tcp_req.payload, sf);

		n = send(socketTCP, &tcp_req, sizeof(TCPrequest), 0);
		DIE(n < 0, "send subscribe error");

		free(sf);
		printf("Subscribed to topic.\n");
	}

	/* Functia trimite comanda de unsubscribe catre server */
	void unsubscribe(std::string input) {
		TCPrequest tcp_req;
		memcpy(&tcp_req.id, id, sizeof(id));
		tcp_req.type = 3;
		tcp_req.data_size = htonl(input.size() + sizeof(tcp_req.id)
							+ sizeof(tcp_req.data_size));
		memset(&tcp_req.payload, 0, sizeof(tcp_req.payload));
		memcpy(&tcp_req.payload, input.c_str(), input.size());

		n = send(socketTCP, &tcp_req, sizeof(TCPrequest), 0);
		DIE(n < 0, "send subscribe error");

		printf("Unsubscribed from topic.\n");
	}

	/* Functia primeste mesaje pe socket-ul TCP */
	bool receive() {

		// Se receptioneaza packet-ul TCP
		char *buf = buffer;
		int rc = recv(socketTCP, buffer + buffer_size, sizeof(TCPpacket), 0);
		DIE(rc < 0, "recv");

		// Daca se primeste 0 serverul s-a inchis si inchid clientul
		if(rc == 0) {
			running = FALSE;
			return true;
		}

		// Verific daca sunt mai multe mesaj concatenate
		int size_msg = *((int *)(buffer));
		while(rc >=  size_msg - buffer_size && rc >= 4) {
			// Printez mesajele complete
			TCPpacket *tcp_msg = (TCPpacket *) buf;
			print_received(*tcp_msg);
			buf = buf + size_msg;
			rc -= size_msg - buffer_size;
			size_msg = *((int *)buf);
			buffer_size = 0;
		}

		// Daca buffer-ul mai are ceva in el
		// mut totul la inceputul bufferului
		if (buf == buffer) {
			buffer_size += rc;
		} else {
			memmove(buffer, buf, rc);
			buffer_size = rc;
		}

		return false;
	}

	/* Functia printeaza mesajele primite pe
	   pe TCP sub forma de string-uri */
	void print_received(TCPpacket tcp_msg) {
		printf ("%s:%i - %s - ",
		inet_ntoa(tcp_msg.source.sin_addr), ntohs(tcp_msg.source.sin_port), tcp_msg.payload.topic);

		// INT
		if (tcp_msg.payload.type == 0) {
			uint8_t sign = tcp_msg.payload.content[0];
			uint32_t value;
			memcpy(&value, tcp_msg.payload.content + sizeof(sign), 4);
			int x = ntohl(value);
			if (sign)
				x = -x;
			std::string x_str = std::to_string(x);
			printf("INT - %s\n", x_str.c_str());

		// SHORT INT
		} else if (tcp_msg.payload.type == 1) {
			uint16_t value;
			memcpy(&value, tcp_msg.payload.content, 2);
			double x = ntohs(value);
			x = x / 100;
			std::stringstream stream;
			stream << std::fixed << std::setprecision(2) << x;
			std::string x_str = stream.str();
			printf("SHORT_REAL - %s\n", x_str.c_str());

		// FLOAT
		} else if (tcp_msg.payload.type == 2) {
			uint8_t sign = tcp_msg.payload.content[0];
			uint32_t value;
			uint8_t power;
			memcpy(&value, tcp_msg.payload.content + sizeof(sign), 4);
			memcpy(&power, tcp_msg.payload.content + sizeof(sign) + sizeof(value), 1);
			double x = (double)ntohl(value) / pow(10, power);
			if (sign)
				x = -x;
			std::string x_str = std::to_string(x);
			x_str.erase(x_str.find_last_not_of('0') + 1, std::string::npos );
			printf("FLOAT - %s\n", x_str.c_str());

		// STRING
		} else if (tcp_msg.payload.type == 3) {
			int size = tcp_msg.size - sizeof(TCPpacket) + 1500;
			char *string = (char *)malloc(size * sizeof(char));
			memset(string, 0, size);
			memmove(string, tcp_msg.payload.content, size);
			printf("STRING - %s\n", string);
		}
	}
};



int main(int argc, char *argv[]) {

    // Eroare daca programul nu primeste parametrii corespunzatori
	if (argc < 3) {
		Subscriber::usage(argv[0]);
	}

    // Am dezactivat buffering-ul la afisare
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	// Initializez o instanta pentru CLIENT
	auto* subscriber = new (std::nothrow) Subscriber(argv[1], argv[2], argv[3]);
    if (!subscriber) {
        std::cerr << "new failed!\n";
        return -1;
    }

    // Pornesc rularea clientului
    subscriber->run();

    // Dealoc memoria alocata pentru client
    delete subscriber;

	return 0;
}
