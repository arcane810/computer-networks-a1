/* f20180193@hyderabad.bits-pilani.ac.in Akul Singhal */

/**
 * A program to fetch a webpage through a proxy
 *
 * Checks redirects by sending HEAD request and checking for 30X status
 * Gets the file using GET request
 * Searches for img url by looking for an image tag and parse url from it
 * All requests use connection: close to prevent the checking of content-length
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

const int BUFFER_SIZE = 10000;
const int REQ_SIZE = 5000;

struct url_path {
    char host[200];
    char path[1000];
};

// split url into host and relative path
struct url_path *split_url(char *url) {
    struct url_path *path = (struct url_path *)malloc(sizeof(struct url_path));
    char *st;
    if (strstr(url, "http://")) {
        st = url + 7;
    } else if (strstr(url, "https://")) {
        st = url + 8;
    } else {
        st = url;
    }
    int i = 0;
    while ((*st) != '/' && (*st) != '\0') {
        path->host[i++] = (*st);
        st++;
    }
    path->host[i] = '\0';
    i = 1;
    path->path[0] = '/';
    if ((*st) == '/')
        st++;
    while ((*st)) {
        path->path[i++] = (*st);
        st++;
    }
    path->path[i] = '\0';
    return path;
}

// Base64 Encode
char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
                         'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
                         'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g',
                         'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
                         's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2',
                         '3', '4', '5', '6', '7', '8', '9', '+', '/'};

char *base64_encode(char *input) {
    int n = strlen(input);
    int m = 4 * ((n + 2) / 3);
    char *encoded_input = (char *)malloc(m * sizeof(char));
    int j = 0;
    int remb = 0;
    int remn = 0;
    for (int i = 0; i < n;) {
        int cv1 = 0, cv2 = 0, cv3 = 0;
        if (i < n) {
            cv1 = input[i++];
        }
        if (i < n) {
            cv2 = input[i++];
        }
        if (i < n) {
            cv3 = input[i++];
        }
        int net = cv1 * (1 << 16) + cv2 * (1 << 8) + cv3;
        encoded_input[j++] = encoding_table[net / 64 / 64 / 64];
        encoded_input[j++] = encoding_table[(net / 64 / 64) % 64];
        encoded_input[j++] = encoding_table[(net / 64) % 64];
        encoded_input[j++] = encoding_table[(net) % 64];
    }
    if (n % 3) {
        encoded_input[j - 1] = '=';
    }
    if (n % 3 == 1) {
        encoded_input[j - 2] = '=';
    }
    encoded_input[j] = '\0';
    return encoded_input;
}

// Get Base64 Encoded Basic Auth Token
int getEncodedAuth(char *login, char *pass, char **b64text) {
    char combined[strlen(login) + strlen(pass) + 2];
    sprintf(combined, "%s:%s", login, pass);

    *b64text = base64_encode(combined);
    return 0;
}

// send head request
int send_get_head_request(int sockfd, char *site, char *authToken) {
    char req[REQ_SIZE];
    struct url_path *split = split_url(site);
    sprintf(req,
            "HEAD http://%s%s HTTP/1.1\r\n"
            "Proxy-Authorization: basic %s\r\n"
            "Connection: Close\r\n\r\n",
            split->host, split->path, authToken);
    // printf("REQ: %s\n", req);
    int sent_bytes = send(sockfd, req, strlen(req), 0);
    return 0;
}

// send get request
int send_get_request(int sockfd, char *site, char *authToken) {
    char req[REQ_SIZE];
    struct url_path *split = split_url(site);
    sprintf(req,
            "GET http://%s%s HTTP/1.1\r\n"
            "Proxy-Authorization: basic %s\r\n"
            "Connection: Close\r\n\r\n",
            split->host, split->path, authToken);
    int sent_bytes = send(sockfd, req, strlen(req), 0);
    return 0;
}

// save response to file
int get_get_response_to_file(int sockfd, FILE *file) {
    int len = 0;
    char buffer[BUFFER_SIZE];
    int tl = 0;
    int wrq = 0;
    int rl = 0;
    char header[BUFFER_SIZE];
    memset(header, 0, sizeof(header));
    while (1) {
        len = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (len < 0)
            return -1;
        buffer[len] = '\0';
        if (len < 0)
            return -1;
        else if (len == 0)
            break;
        if (wrq) {
            fputs(buffer, file);
        } else {
            for (int i = 0; i <= len; i++) {
                header[tl + i] = buffer[i];
            }
            char *st = strstr(header, "\r\n\r\n");
            if (st) {
                wrq = 1;
            }
            st += 4;
            if (st) {
                fputs(st, file);
                wrq = 1;
            }
        }
        tl += len;
        memset(buffer, 0, sizeof(buffer));
    }
    return 0;
}

// save image to file
int get_get_response_to_file_image(int sockfd, FILE *file) {
    int len = 0;
    unsigned char buffer[BUFFER_SIZE];
    int tl = 0;
    int wrq = 0;
    unsigned char header[BUFFER_SIZE];
    memset(header, 0, sizeof(header));
    while (1) {
        len = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (len < 0)
            return -1;
        buffer[len] = '\0';
        if (len < 0)
            return -1;
        else if (len == 0)
            break;
        if (wrq) {
            fwrite(buffer, 1, len, file);
            // printf("%s\n", buffer);
        } else {
            for (int i = 0; i <= len; i++) {
                header[tl + i] = buffer[i];
            }
            unsigned char *st = strstr(header, "\r\n\r\n");
            if (st) {
                wrq = 1;
            }
            st += 4;
            if (st) {
                fwrite(st, 1, header + tl + len - st, file);
                wrq = 1;
            }
        }
        tl += len;
        memset(buffer, 0, sizeof(buffer));
    }
    return 0;
}

// Check for Redirects and replace address in site with the redirect address
int check_redirect_replace(int sockfd, char *site, char *authToken,
                           char *header) {
    send_get_head_request(sockfd, site, authToken);
    memset(header, 0, 10000 * sizeof(char));
    char buffer[BUFFER_SIZE];
    int len;
    while (1) {
        len = recv(sockfd, buffer, BUFFER_SIZE - 1, 0);
        if (len < 0)
            return -1;
        else if (len == 0)
            break;
        strcat(header, buffer);
        memset(buffer, 0, sizeof(buffer));
    }
    // printf("%s\n", header);
    char *status_start = strcasestr(header, "HTTP/1.1 ");
    int status = 0;
    status_start += 9;
    while (*(status_start) == ' ') {
        status_start++;
    }
    while (*(status_start) != ' ') {
        status *= 10;
        status += *(status_start) - '0';
        status_start++;
    }
    if (status >= 300 && status < 310) {
        char *location_start = strcasestr(header, "Location");
        int i = 0;
        location_start += 9;
        while (*(location_start) == ' ') {
            location_start++;
        }
        while (*(location_start) != ' ' && (*location_start) != '\r' &&
               (*location_start) != '\n') {
            site[i++] = *(location_start);
            location_start++;
        }
        site[i] = '\0';
        return 1;
    }

    return 0;
}

// Reconnect to Proxy
int reconnect_proxy(int sockfd, struct addrinfo *p, char *site_addr) {
    close(sockfd);
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    connect(sockfd, p->ai_addr, p->ai_addrlen);
    return sockfd;
}

int main(int argc, char *argv[]) {

    // Arguments to variables
    char *site_addr = argv[1];
    char *proxy_addr = argv[2];
    char *proxy_port = argv[3];
    char *proxy_login = argv[4];
    char *proxy_pass = argv[5];
    char *home_page_name = argv[6];
    int need_logo = 0;
    char *logo_name;
    if (argc == 8) {
        need_logo = 1;
        logo_name = argv[7];
    }

    // Gets auth token
    char *authToken;
    getEncodedAuth(proxy_login, proxy_pass, &authToken);

    int sockfd, rv;
    struct addrinfo hints, *servinfo, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char s[INET_ADDRSTRLEN];

    if ((rv = getaddrinfo(proxy_addr, proxy_port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // Iterate addrinfo obtained from getaddrinfo
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
            -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return -1;
    }

    char site[2000];
    sprintf(site, site_addr);

    char header[10000];
    while (check_redirect_replace(sockfd, site, authToken, header) > 0) {
        printf("NEW SITE: %s\n", site);
        sockfd = reconnect_proxy(sockfd, p, site);
    }
    printf("FINAL SITE: %s\n", site);
    sockfd = reconnect_proxy(sockfd, p, site);
    send_get_request(sockfd, site, authToken);
    FILE *file = fopen(home_page_name, "w");
    get_get_response_to_file(sockfd, file);
    fclose(file);

    if (need_logo) {
        char line[20000];
        char imgurl[10000];
        struct url_path *split = split_url(site);
        sockfd = reconnect_proxy(sockfd, p, site);
        file = fopen(home_page_name, "r");
        int f = 1;
        // Read result html line by line to find image url where
        while (fgets(line, 20000, file)) {
            char *ist = line;
            if (f == 1) {
                ist = strcasestr(line, "img");
                if (ist)
                    f++;
            }
            if (f == 2) {
                ist = strcasestr(line, "src");
                if (ist)
                    f++;
            }
            if (f == 3) {
                while (((*ist) != '\'' && (*ist) != '\"' && (*ist) != '\n')) {
                    ist++;
                }
                if ((*ist) == '\n') {

                } else {
                    f++;
                }
            }
            if (f == 4) {
                ist++;
                int j = 0;
                while ((*ist) != '\'' && (*ist) != '\"' && (*ist) != '\n') {
                    imgurl[j++] = (*ist);
                    ist++;
                }
                imgurl[j] = '\0';
                break;
            }
        }
        printf("LOGO: %s\n", imgurl);
        char final_url[1000];
        if (strstr(imgurl, "http://") == imgurl) {
            strcpy(final_url, imgurl);
        } else {
            sprintf(final_url, "http://%s%s/%s", split->host, split->path,
                    imgurl);
        }
        file = fopen(logo_name, "wb");
        send_get_request(sockfd, final_url, authToken);
        get_get_response_to_file_image(sockfd, file);
    }

    close(sockfd);
    // SSL_CTX_free(ssl_ctx);
    freeaddrinfo(servinfo);

    return 0;
}