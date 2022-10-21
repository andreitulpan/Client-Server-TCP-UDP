#include <bits/stdc++.h>
#include <arpa/inet.h>

/* Functia converteste un string de tip char* intr-un string */
std::string convertToString(char* a, int size)
{
    int i;
    std::string s = "";
    for (i = 0; i < size; i++) {
		if (a[i] == '\n')
			return s;
        s = s + a[i];
    }
    return s;
}

/* Functia returneaza size-ul unui string de tip char* */
int char_size(char *str) {
    int i = 0;
    while (ntohs(str[i]) != '\0')
        i++;
    return i;
}

/* Functia imparte un string dupa delimitatori */
void split (char *str, char *delim, std::vector<std::string> &strings)
{
	char* token = strtok(str, delim);
    while (token != NULL) {
        strings.push_back(convertToString(token, char_size(token)));
        token = strtok(NULL, delim);
    }
}
