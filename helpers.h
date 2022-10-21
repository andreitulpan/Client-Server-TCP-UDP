#ifndef _HELPERS_H
#define _HELPERS_H 1

#define TRUE 1
#define FALSE 0
#define MAX_CLIENTS 10
#define NEW_CLIENT 1
#define SUBSCRIBE 2
#define UNSUBSCRIBE 3
#define TOPIC_SIZE 50
#define NAGLE_OFF 1


// Macro de verificare a erorilor
#define DIE(assertion, call_description)	\
	do {									\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
					__FILE__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);				\
		}									\
	} while(0)
#endif

// Structura pentru pachete UDP
#pragma pack(push, 1)
struct UDPpacket {
  char topic[50];
  uint8_t type;
  char content[1500];
};
#pragma pack(pop)
typedef struct UDPpacket UDPpacket;

// Structura pentru pachete TCP
#pragma pack(push, 1)
struct TCPpacket {
    int size;
    struct sockaddr_in source;
    UDPpacket payload;
};
#pragma pack(pop)
typedef struct TCPpacket TCPpacket;

// Structura pentru un request TCP
#pragma pack(push, 1)
struct TCPrequest {
    uint8_t type;
    int data_size;
    char id[10];
    char payload[64];
};
#pragma pack(pop)
typedef struct TCPrequest TCPrequest;

// Structura pentru topic-ul unui client
struct topic_entry {
    std::string topic;
    int sf;
};
typedef struct topic_entry topic_entry;

// Structura pentru salvarea clientilor
struct client_entry {
    char id[10];
    int socket;
    bool connected;
    std::vector<topic_entry *> topics;
};
typedef struct client_entry client_entry;

// Functia de convert a string-urilor char* in string-uri
std::string convertToString(char* a, int size);

// Functia de impartire a unui string dupa delimitatori
void split (char *str, char *delim, std::vector<std::string> &strings);
