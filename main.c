#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>

#define PACKET_SIZE 64
#define DEFAULT_COUNT 10
#define TIMEOUT 1
#define MAX_PAYLOAD 56

volatile sig_atomic_t running = 1;

typedef struct {
    int sent;
    int received;
    double min_time;
    double max_time;
    double total_time;
} Stats;

unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;
    
    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char *)buf;
    
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    
    return result;
}

void handle_signal(int sig) {
    running = 0;
}

void create_packet(char *packet, int seq) {
    struct icmphdr *icmp = (struct icmphdr *)packet;
    
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = getpid() & 0xFFFF;
    icmp->un.echo.sequence = seq;
    icmp->checksum = 0;
    
    for (size_t i = sizeof(*icmp); i < PACKET_SIZE; i++)
        packet[i] = rand() % 256;
    
    icmp->checksum = checksum((unsigned short *)icmp, PACKET_SIZE);
}

double send_ping(int sockfd, struct sockaddr_in *addr, int seq, Stats *stats) {
    char packet[PACKET_SIZE];
    struct timeval start, end;
    double rtt;
    
    create_packet(packet, seq);
    
    gettimeofday(&start, NULL);
    
    if (sendto(sockfd, packet, PACKET_SIZE, 0, (struct sockaddr *)addr, sizeof(*addr)) <= 0) {
        perror("sendto error");
        return -1;
    }
    
    stats->sent++;
    
    char recv_packet[PACKET_SIZE];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    
    if (select(sockfd + 1, &readfds, NULL, NULL, &tv) > 0) {
        if (recvfrom(sockfd, recv_packet, PACKET_SIZE, 0, (struct sockaddr *)&from, &from_len) > 0) {
            gettimeofday(&end, NULL);
            
            rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
            
            struct iphdr *ip = (struct iphdr *)recv_packet;
            struct icmphdr *icmp = (struct icmphdr *)(recv_packet + (ip->ihl << 2));
            
            if (icmp->type == ICMP_ECHOREPLY && icmp->un.echo.id == (getpid() & 0xFFFF)) {
                stats->received++;
                stats->total_time += rtt;
                
                if (rtt < stats->min_time || stats->min_time == 0)
                    stats->min_time = rtt;
                if (rtt > stats->max_time)
                    stats->max_time = rtt;
                
                return rtt;
            }
        }
    }
    
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <IP address> [packet count]\nCredits to https://stresser.cfd/\n", argv[0]);
        return 1;
    }
    
    int count = DEFAULT_COUNT;
    if (argc > 2) {
        count = atoi(argv[2]);
        if (count <= 0) count = DEFAULT_COUNT;
    }
    
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket error");
        return 1;
    }
    
    struct timeval tv;
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt error");
        close(sockfd);
        return 1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP address\n");
        close(sockfd);
        return 1;
    }
    
    signal(SIGINT, handle_signal);
    
    printf("PING %s: %d packets\nCredits to https://stresser.cfd/\n", argv[1], count);
    printf("---------------------------------------\n");
    
    Stats stats = {0, 0, 0, 0, 0};
    srand(time(NULL));
    
    for (int i = 1; i <= count && running; i++) {
        double rtt = send_ping(sockfd, &addr, i, &stats);
        
        if (rtt > 0) {
            printf("%d bytes from %s: seq=%d ttl=64 time=%.3f ms\n", 
                   PACKET_SIZE, argv[1], i, rtt);
        } else {
            printf("Request to %s: timeout\n", argv[1]);
        }
        
        sleep(1);
    }
    
    printf("---------------------------------------\n");
    printf("--- %s ping statistics ---\n", argv[1]);
    printf("%d packets transmitted, %d received, %.0f%% packet loss\n", 
           stats.sent, stats.received, 
           (stats.sent - stats.received) * 100.0 / stats.sent);
    
    if (stats.received > 0) {
        printf("Round-trip min/avg/max = %.3f/%.3f/%.3f ms\n",
               stats.min_time, stats.total_time / stats.received, stats.max_time);
    }
    
    printf("Credits to https://stresser.cfd/\n");
    close(sockfd);
    return 0;
}
