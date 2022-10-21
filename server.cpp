#include <bits/stdc++.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "helpers.h"

class Server {
 public:
    int portno;

    Server(char *port) {

        // La initializarea serverului setez port-ul
        portno = atoi(port);
        DIE(portno == 0, "atoi");
    }

    ~Server() {

        // Inchid socket-ul UDP
	    close(socketUDP);

        // Inchid socket-ul TCP pentru listen
        close(socketTCP);

        // Inchid toti socketii TCP
        for (int socket = 0; socket <= fdmax; socket++) {
            if (FD_ISSET(socket, &read_fds)) {
                close(socket);
            }
        }

        // Eliberez memoria alocata pentru clienti
        for (auto &client : TCP_clients) {
            for (auto &topic : client->topics) {
                delete topic;
            }
            delete client;
        }
    }

    /* Eroare de utilizare la pornirea serverului */
    static void usage(char *file) {
        fprintf(stderr, "Usage: %s server_port\n", file);
        exit(0);
    }

    /* Functia publica care ruleaza serverul */
    void run() {
        initialize();
        run_server();
    }

 private:

    // Clientii TCP care s-au conectat de la pornirea serverului
    std::vector<client_entry *> TCP_clients;

    // Mesajele de trimis pentru fiecare client TCP offline
    std::map<std::string, std::queue<TCPpacket>> forward_packets;

    fd_set read_fds;	// Multimea de citire folosita in select()
	fd_set tmp_fds;		// Multime folosita temporar
	int fdmax;			// Valoare maxima fd din multimea read_fds

    int running, ret;
    int socketUDP, socketTCP, newSocketTCP;
    struct sockaddr_in server_addr, client_addr, tcp_client_addr;
    socklen_t client_addr_size, tcp_client_addr_size;

    /* Functia initializeaza serverul pentru o buna functionare */
    void initialize() {

        // Se goleste multimea de descriptori de citire si multimea temporara
        FD_ZERO(&read_fds);
        FD_ZERO(&tmp_fds);

        // Initializez socket-ul UDP pentru primire de pachete
        socketUDP = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        DIE(socketUDP < 0, "socket UDP");

        // Initializez socket-ul TCP pentru listen
        socketTCP = socket(AF_INET, SOCK_STREAM, 0);
        DIE(socketTCP < 0, "socket");

        // Dezactivez algoritmul Nagle
        int one = NAGLE_OFF;
        ret = setsockopt(socketTCP, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        DIE(ret < 0, "nagle algo error");


        // Initializez socket-ul ethernet
        memset((char *) &server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(portno);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        // Initializez dimensiunea socket-urilor
        client_addr_size = sizeof(client_addr);
        tcp_client_addr_size = sizeof(tcp_client_addr);

        // Pun datele ethernet in socketi
        ret = bind(socketUDP, (struct sockaddr *) &server_addr, sizeof(struct sockaddr));
        DIE(ret < 0, "bind UDP");
        ret = bind(socketTCP, (struct sockaddr *) &server_addr, sizeof(struct sockaddr));
        DIE(ret < 0, "bind TCP");

        // Ascult pe socket-ul TCP pentru conexiuni noi
        ret = listen(socketTCP, MAX_CLIENTS);
        DIE(ret < 0, "listen TCP");

        // Se adauga socket-urile pe care se asculta conexiunea si stdin-ul in multimea read_fds
        FD_SET(socketUDP, &read_fds);
        FD_SET(socketTCP, &read_fds);
        FD_SET(STDIN_FILENO,&read_fds);

        if (socketUDP >= socketTCP) {
            fdmax = socketUDP;
        } else {
            fdmax = socketTCP;
        }

        // Se seteaza starea TRUE pentru a rula serverul
        running = TRUE;

    }

    /* Functia executa rularea serverului */
    void run_server() {
        while (running) {
            tmp_fds = read_fds;

            // Folosesc functia select pentru multiplexare intrarilor
            ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
            DIE(ret < 0, "select");

            // Iterez prin multimea de citire
            for (int socket = 0; socket <= fdmax; socket++) {
                if (FD_ISSET(socket, &tmp_fds)) {
                    // Se verifica daca se citeste input
                    if (socket == STDIN_FILENO) {
                        if (close_server())
                            break;
                    // Se verifica daca se primesc mesaje pe UDP si redirectioneaza
                    } else if (socket == socketUDP) {
                        receive_UDP(socket);
                    // Se verifica daca se primesc conexiuni noi pe TCP
                    } else if (socket == socketTCP) {
                        new_connection_TCP(socket);
                    // Se verifica daca se primeste mesaj pe TCP
                    } else {
                        connection_TCP(socket);
                    }
                }
            }
		}
    }

    /* Functia receptioneaza mesajele primite pe socket-ul
       UDP si le redirectioneaza catre clientii TCP */
    void receive_UDP(int socket) {

        // Receptionez pachetele trimise pe UDP
        UDPpacket udp_msg;
        memset(&udp_msg, 0, sizeof(UDPpacket));
        int n = recvfrom(socket, &udp_msg, sizeof(UDPpacket), 0, (struct sockaddr*)&client_addr, &client_addr_size);
        DIE(n < 0, "recv");

        // Verific daca pachetul nu este gol
        if (n != 0) {
            // Creez un pachet TCP cu datele venite pe UDP
            TCPpacket tcp_msg;
            memset(&tcp_msg, 0, sizeof(TCPpacket));
            tcp_msg.source = client_addr;
            tcp_msg.payload = udp_msg;
            tcp_msg.size = sizeof(tcp_msg.size) + sizeof(tcp_msg.source) + n;

            // Verific clientii TCP care au topic-ul primit
            for (auto &client: TCP_clients) {
                int found = 0;
                int sf = 0;
                for (auto &topic : client->topics) {
                    if (!strcmp(topic->topic.c_str(), udp_msg.topic)) {
                        found = 1;
                        sf = topic->sf;
                        break;
                    }
                }

                // Daca am gasit topic pentru client ii trimit mesajul
                if (found) {

                    // Daca este conectat il trimit acum, in caz
                    // contrar e offline si are sf-ul 1 il adaug in coada
                    if (client->connected) {
                        n = send(client->socket, &tcp_msg, tcp_msg.size, MSG_NOSIGNAL);
                        DIE(n < 0, "send tcp msg error");
                    } else {
                        if (sf == 1) {
                            forward_packets[client->id].push(tcp_msg);
                        }
                    }
                }
            }
        }
    }

    /* Functia accepta conexiuni noi pe socket-ul TCP
       si salveaza clientii care s-au conectat*/
    void new_connection_TCP(int socket) {

        // Primesc o conexiune noua pe socketul TCP
        newSocketTCP = accept(socket, (struct sockaddr *) &tcp_client_addr, &tcp_client_addr_size);
        DIE(newSocketTCP < 0, "accept");

        // Dezactivez algoritmul Nagle
        int one = NAGLE_OFF;
		ret = setsockopt(newSocketTCP, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		DIE(ret < 0, "nagle algo error");

        // Astept mesaj cu ID-ul clientului pe TC
        TCPrequest tcp_req;
        int n = recv(newSocketTCP, &tcp_req, sizeof(TCPrequest), 0);
        DIE(n < 0, "recv id error");

        // Verific daca mesajul este pentru connect
        if (tcp_req.type == NEW_CLIENT) {
            client_entry *client = new client_entry();
            client->connected = FALSE;

            // Caut clientul dupa ID in lista de clienti
            int found = 0;
            for (auto &c : TCP_clients) {
                if (!strcmp(c->id, tcp_req.id)) {
                    delete client;
                    client = c;
                    found = 1;
                }
            }

            // Daca clientul nu este neconectat il conectez la server
            if (!client->connected) {

                // Adaug noul socket la multimea descriptorilor de citire
                FD_SET(newSocketTCP, &read_fds);
                if (newSocketTCP > fdmax) {
                    fdmax = newSocketTCP;
                }

                // Updatez socketul clientului
                client->socket = newSocketTCP;
                // Ii setez statusul nou
                client->connected = TRUE;

                // Daca este client vechi si are mesaje neprimite i le trimite
                if (found) {
                    TCPpacket tcp_msg;
                    while (forward_packets[client->id].size()) {
                        tcp_msg = forward_packets[client->id].front();
                        n = send(client->socket, &tcp_msg, tcp_msg.size, MSG_NOSIGNAL);
                        DIE(n < 0, "send tcp msg error");
                        forward_packets[client->id].pop();
                    }
                }

                // Daca este client nou il adaug in lista de clienti
                if (!found) {
                    memcpy(client->id, tcp_req.id, sizeof(client->id));
                    TCP_clients.push_back(client);
                }
                printf("New client %s connected from %s:%u.\n", client->id, inet_ntoa(tcp_client_addr.sin_addr), ntohs(tcp_client_addr.sin_port));
            } else {
                // Daca este conectat deja printez un mesaj de eroare si inchid socket-ul
                printf("Client %s already connected.\n", client->id);
                close(newSocketTCP);
            }
        }
    }

    /* Functia primeste pachete pe socketii TCP cu
       comenzi de la client (close/subscribe/unsubscribe) */
    void connection_TCP(int socket ) {

        // S-au primit pachete TCP de la clienti
        TCPrequest tcp_req;
        int n = recv(socket, &tcp_req, sizeof(TCPrequest), 0);
        DIE(n < 0, "recv");

        // Daca pachetul este gol clientul s-a deconectat
        if (n == 0) {
            for (auto client : TCP_clients) {
                // Gasesc clientul ii updatez statusul si ii inchid socketul
                if (client->socket == socket && client->connected == TRUE) {
                    close(socket);
                    FD_CLR(socket, &read_fds);
                    client->connected = FALSE;
                    printf("Client %s disconnected.\n", client->id);
                }
            }
        }

        // Verific daca mesajul este pentru subscribe
        if (tcp_req.type == SUBSCRIBE) {
            topic_entry *topic_add = new topic_entry();

            char topic[TOPIC_SIZE];
            int size = ntohl(tcp_req.data_size) - sizeof(char)
                    - sizeof(tcp_req.id) - sizeof(tcp_req.data_size);
            memset(topic, 0, sizeof(topic));
            memcpy(topic, tcp_req.payload, size);

            // Extrag flag-ul store-and-forward
            char *sf = (char *)malloc(2 * sizeof(char));
            memcpy(sf, tcp_req.payload + size , sizeof(char));
            std::string topic_str = convertToString(topic, size);

            // Caut clientul in lista de clienti
            for (auto &client : TCP_clients) {
                if (!strcmp(client->id, tcp_req.id)) {
                    int found = 0;
                    for (auto &t : client->topics) {
                        if (t->topic == topic_str) {
                            delete topic_add;
                            topic_add = t;
                            found = 1;
                        }
                    }

                    topic_add->sf = atoi(sf);

                    // Daca clientul exista ii adaug clientului topic-ul cerut
                    if (!found) {
                        topic_add->topic = topic_str;
                        client->topics.push_back(topic_add);
                    }
                }
            }

        // Verific daca mesajul este pentru unsubscribe
        } else if (tcp_req.type == UNSUBSCRIBE) {

            char topic[TOPIC_SIZE];
             int size = ntohl(tcp_req.data_size) - sizeof(tcp_req.id)
                        - sizeof(tcp_req.data_size);
            memset(topic, 0, sizeof(topic));
            memcpy(topic, tcp_req.payload, size);

            // Caut clientul in lista de clientis
            for (auto c : TCP_clients) {
                if (!strcmp(c->id, tcp_req.id)) {

                    // Ii caut topic-ul cerut pentru unsubscribe si il sterg daca exista
                    for (auto t = c->topics.begin(); t < c->topics.end(); t++) {
                        std::cout << (*t)->topic.c_str() << " " << topic << std::endl;
                        if (!strcmp((*t)->topic.c_str(), topic)) {
                            c->topics.erase(t);
                        }
                    }
                }
            }
        }
    }

    /* Functia citeste date de la stdin si
       opreste serverul pentru comanda "exit" */
    bool close_server() {
        char buf[128];
        if (!fgets(buf, sizeof(buf), stdin))
            DIE(ferror(stdin), "stdin error");

        // Daca se primeste comanda exit se inchide serverul
        if (!strcmp(buf, "exit\n")) {
            running = FALSE;
            return true;
        }
        return false;
    }

};

int main(int argc, char *argv[]) {

    // Eroare daca programul nu primeste port-ul
    if (argc < 2) {
        Server::usage(argv[0]);
    }

    // Am dezactivat buffering-ul la afisare
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Initializez o instanta pentru server
    auto* server = new (std::nothrow) Server(argv[1]);
    if (!server) {
        std::cerr << "new failed!\n";
        return -1;
    }

    // Pornesc rularea serverului
    server->run();

    // Dealoc memoria alocata pentru server
    delete server;

	return 0;
}
