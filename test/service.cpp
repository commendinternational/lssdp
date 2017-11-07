#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>     // select
#include <sys/time.h>   // gettimeofday
#include "lssdp.h"
#include <string>
#include <signal.h>


#define RESEND_INTERVAL 30

static volatile int keepRunning = 1;

void intHandler(int dummy) {
	keepRunning = 0;
}


void log_callback(const char * file, const char * tag, int level, int line,
                  const char * func, const char * message) {
	std::string level_name = "DEBUG";
	if (level == LSSDP_LOG_INFO)   level_name = "INFO";
	if (level == LSSDP_LOG_WARN)   level_name = "WARN";
	if (level == LSSDP_LOG_ERROR)  level_name = "ERROR";

	printf("[%-5s][%s] %s", level_name.c_str(), tag, message);
}

long long get_current_time() {
	struct timeval time = {};
	if (gettimeofday(&time, NULL) == -1) {
		printf("gettimeofday failed, errno = %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	return (long long) time.tv_sec * 1000 + (long long) time.tv_usec / 1000;
}



void print_lssdp_packet(lssdp_packet &parsed_packet) {


	printf("---------------------------------------------------\n");
	printf("METHOD: %s\n",parsed_packet.method);
	printf("ST: %s\n",parsed_packet.st);
	printf("USN: %s\n",parsed_packet.usn);
	printf("LOCATION: %s\n",parsed_packet.location);
	printf("NTS: %s\n",parsed_packet.nts);
	printf("---------------------------------------------------\n");
}





int show_ssdp_packet(struct lssdp_ctx * lssdp, const char * packet,
                     size_t packet_len) {
	printf("%s", packet);



	lssdp_packet parsed_packet = {};
	if (lssdp_packet_parser(packet, packet_len, &parsed_packet) == 0) {

		print_lssdp_packet(parsed_packet);
	}


	return 0;
}

int main( int argc, char *argv[] ) {

	lssdp_ctx lssdp;
	lssdp.port = 1900;
	lssdp.debug = true;
	lssdp.packet_received_callback = show_ssdp_packet;

	lssdp_init(&lssdp);
	lssdp_set_log_callback(log_callback);
	lssdp.config.ADDR_LOCALHOST = "::1";
	lssdp.config.ADDR_MULTICAST = "FF02::C";
	lssdp.header.max_age = RESEND_INTERVAL*2;

	strncpy(lssdp.header.location.prefix,"http://\0",LSSDP_FIELD_LEN);
	strncpy(lssdp.header.location.domain,"test_location",LSSDP_FIELD_LEN);
	strncpy(lssdp.header.location.suffix,":8082\0",LSSDP_FIELD_LEN);

	strncpy(lssdp.header.search_target,"commend_switchbox\0",LSSDP_FIELD_LEN);
	if(argc >= 2) {
		strncpy(lssdp.header.unique_service_name,argv[1],LSSDP_FIELD_LEN);
	} else {
		strncpy(lssdp.header.unique_service_name,"DUMMY_BARCODE",LSSDP_FIELD_LEN);

	}

	if(argc >= 3) {
		strncpy(lssdp.header.location.domain,argv[2],LSSDP_FIELD_LEN);
	}

	if (lssdp_socket_create(&lssdp) < 0)
		exit(EXIT_FAILURE);

	lssdp_send_notify(&lssdp);

	//Capture CTRL-C and send bye before exiting
	signal(SIGINT, intHandler);

	long long timelast = get_current_time();

	// Main Loop
	while (keepRunning) {
		fd_set fs;
		FD_ZERO(&fs);
		FD_SET(lssdp.sock, &fs);
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 500 * 1000;   // 500 ms

		int ret = select(lssdp.sock + 1, &fs, NULL, NULL, &tv);
		if (ret < 0) {
			printf("select error, ret = %d\n", ret);
			break;
		}
		lssdp_neighbor_check_timeout(&lssdp);

		if((get_current_time() - timelast) > RESEND_INTERVAL*1000) {
			timelast = get_current_time();
			lssdp_send_notify(&lssdp);

		}

		if (ret > 0) {
			lssdp_socket_read(&lssdp);
		}

	}

	lssdp_send_byebye(&lssdp);

	return EXIT_SUCCESS;
}
