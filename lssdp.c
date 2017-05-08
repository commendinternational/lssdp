#define _POSIX_C_SOURCE 200112L // getaddrinfo
#define _DEFAULT_SOURCE // struct ip_mreq, newer Ubuntu/glib 16.04
#define _BSD_SOURCE // struct ip_mreq, older Ubuntu/glib 12.04

#include <sys/types.h>
#include <stdio.h>      // snprintf, vsnprintf
#include <stdlib.h>     // malloc, free
#include <stdarg.h>     // va_start, va_end, va_list
#include <string.h>     // memset, memcpy, strlen, strcpy, strcmp, strncasecmp, strerror
#include <strings.h>
#include <ctype.h>      // isprint, isspace
#include <errno.h>      // errno
#include <unistd.h>     // close
#include <sys/time.h>   // gettimeofday
#include <sys/ioctl.h>  // ioctl, FIONBIO
#include <net/if.h>     // struct ifconf, struct ifreq
#include <fcntl.h>      // fcntl, F_GETFD, F_SETFD, FD_CLOEXEC
#include <sys/socket.h> // struct sockaddr, AF_INET, SOL_SOCKET, socklen_t, setsockopt, socket, bind, sendto, recvfrom
#include <netinet/in.h> // struct sockaddr_in, struct ip_mreq, INADDR_ANY, IPPROTO_IP, also include <sys/socket.h>
#include <arpa/inet.h>  // inet_aton, inet_ntop, inet_addr, also include <netinet/in.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <netdb.h>


#include "lssdp.h"

#ifndef _SIZEOF_ADDR_IFREQ
#define _SIZEOF_ADDR_IFREQ sizeof
#endif

/** Definition **/
#define LSSDP_BUFFER_LEN    2048
#define lssdp_debug(fmt, agrs...) lssdp_log(LSSDP_LOG_DEBUG, __LINE__, __func__, fmt, ##agrs)
#define lssdp_info(fmt, agrs...)  lssdp_log(LSSDP_LOG_INFO,  __LINE__, __func__, fmt, ##agrs)
#define lssdp_warn(fmt, agrs...)  lssdp_log(LSSDP_LOG_WARN,  __LINE__, __func__, fmt, ##agrs)
#define lssdp_error(fmt, agrs...) lssdp_log(LSSDP_LOG_ERROR, __LINE__, __func__, fmt, ##agrs)

static const char * HEADER_MSEARCH = "M-SEARCH * HTTP/1.1\r\n";
static const char * HEADER_NOTIFY  = "NOTIFY * HTTP/1.1\r\n";
static const char * HEADER_RESPONSE = "HTTP/1.1 200 OK\r\n";
static const char * RESPONSE =  "RESPONSE";
static const char * MSEARCH  =  "M-SEARCH";
static const char * NOTIFY  =  "NOTIFY";


static void (* _log_callback)(const char * file, const char * tag, int level,
                              int line,
                              const char * func, const char * message) = NULL;


/** Internal Function **/
static int neighbor_list_add(lssdp_ctx * lssdp, const lssdp_packet packet);
static int send_multicast_data(const char * data , lssdp_ctx * lssdp);
static int lssdp_send_response(lssdp_ctx * lssdp, struct sockaddr_in6 address);
static int parse_field_line(const char * data, size_t start, size_t end,
                            lssdp_packet * packet);
static int get_colon_index(const char * string, size_t start, size_t end);
static int trim_spaces(const char * string, size_t * start, size_t * end);
static long long get_current_time();
static int lssdp_log(int level, int line, const char * func,
                     const char * format, ...);


void lssdp_init(lssdp_ctx * lssdp) {

	lssdp->config.multicastPort  = "1900";
	lssdp->config.ADDR_LOCALHOST = "127.0.0.1";
	lssdp->config.ADDR_MULTICAST = "239.255.255.250";
	lssdp->header.max_age = 10;
	lssdp->neighbor_list = NULL;
}


int lssdp_socket_close(lssdp_ctx * lssdp) {
	if (!lssdp)
		return -1;
	if (lssdp->sock >= 0)
		close(lssdp->sock);

	return 0;
}

// 02. lssdp_socket_create
int lssdp_socket_create(lssdp_ctx * lssdp) {

	if (lssdp == NULL) {
		return -1;
	}


	struct addrinfo   hints  = { 0 };    /* Hints for name lookup */
	struct addrinfo*  multicastAddr = NULL;     /* Multicast Address */
	struct addrinfo*  localAddr = NULL;         /* Local address to bind to */
	int yes=1;


	/* Resolve the multicast group address */
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags  = AI_NUMERICHOST;
	int status;
	if ((status = getaddrinfo(lssdp->config.ADDR_MULTICAST, NULL, &hints,
	                          &multicastAddr)) != 0)
	{
		lssdp_error("Failed to get multicast address: %s\n", gai_strerror(status));
		goto fail;
	}



	/* Get a local address with the same family (IPv4 or IPv6) as our multicast group */
	hints.ai_family   = multicastAddr->ai_family;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags    = AI_PASSIVE; /* Return an address we can bind to */
	if ((status = getaddrinfo(NULL, lssdp->config.multicastPort, &hints,
	                          &localAddr)) != 0 ) {
		lssdp_error("Failed to get local address for destination: %s\n",
		            gai_strerror(status));
		goto fail;
	}

	if ( (lssdp->sock = socket(localAddr->ai_family, localAddr->ai_socktype,
	                           0)) < 0 ) {
		lssdp_error("Failed to create socket\n");
		goto fail;
	}

	/* lose the pesky "Address already in use" error message */
	if (setsockopt(lssdp->sock,SOL_SOCKET,SO_REUSEADDR,(char*)&yes,
	               sizeof(int)) == -1) {
		lssdp_error("Failed to set socket option\n");
		goto fail_and_close;
	}

	unsigned char loop = 1;
	setsockopt(lssdp->sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

	// set non-blocking
	int opt = 1;
	if (ioctl(lssdp->sock, FIONBIO, &opt) != 0) {
		lssdp_error("ioctl FIONBIO failed, errno = %s (%d)\n", strerror(errno), errno);
		goto fail_and_close;
	}

	/* Bind to the multicast port */
	if ( bind(lssdp->sock, localAddr->ai_addr, localAddr->ai_addrlen) != 0 ) {
		lssdp_error("Failed to bind to multicast port\n");
		goto fail_and_close;
	}


	/* Join the multicast group. We do this seperately depending on whether we
	*  are using IPv4 or IPv6.
	*/
	if ( multicastAddr->ai_family  == PF_INET &&
	        multicastAddr->ai_addrlen == sizeof(struct sockaddr_in6) ) /* IPv4 */
	{
		lssdp_debug("Binding IPv4\n");
		struct ip_mreq multicastRequest;  /* Multicast address join structure */

		/* Specify the multicast group */
		memcpy(&multicastRequest.imr_multiaddr,
		       &((struct sockaddr_in6*)(multicastAddr->ai_addr))->sin6_addr,
		       sizeof(multicastRequest.imr_multiaddr));

		/* Accept multicast from any interface */
		multicastRequest.imr_interface.s_addr = htonl(INADDR_ANY);

		/* Join the multicast address */
		if ( setsockopt(lssdp->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		                (char*) &multicastRequest, sizeof(multicastRequest)) != 0 ) {
			lssdp_error("Failed to join ipv4 multicast address\n");
			goto fail_and_close;
		}
	}
	else if ( multicastAddr->ai_family  == PF_INET6 &&
	          multicastAddr->ai_addrlen == sizeof(struct sockaddr_in6) ) /* IPv6 */
	{
		lssdp_debug("Binding IPv6\n");
		struct ipv6_mreq multicastRequest;  /* Multicast address join structure */

		/* Specify the multicast group */
		memcpy(&multicastRequest.ipv6mr_multiaddr,
		       &((struct sockaddr_in6*)(multicastAddr->ai_addr))->sin6_addr,
		       sizeof(multicastRequest.ipv6mr_multiaddr));

		/* Accept multicast from any interface */
		multicastRequest.ipv6mr_interface = 0;

		/* Join the multicast address */
		if ( setsockopt(lssdp->sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
		                (char*) &multicastRequest, sizeof(multicastRequest)) != 0 ) {
			lssdp_error("Failed to join ipv6 multicast address\n");
			goto fail_and_close;
		}
	}
	else {
		lssdp_error("Failed to join multicast address\n");
		goto fail_and_close;
	}


	freeaddrinfo(localAddr);
	freeaddrinfo(multicastAddr);

	return 0;

fail_and_close:
	close(lssdp->sock);
fail:
	freeaddrinfo(localAddr);
	freeaddrinfo(multicastAddr);

	return -1;
}

// 04. lssdp_socket_read
int lssdp_socket_read(lssdp_ctx * lssdp) {
	if (lssdp == NULL) {
		lssdp_error("lssdp should not be NULL\n");
		return -1;
	}

	// check socket and port
	if (lssdp->sock < 0) {
		lssdp_error("SSDP socket (%d) has not been setup.\n", lssdp->sock);
		return -1;
	}

	char buffer[LSSDP_BUFFER_LEN] = {};
	struct sockaddr_in6 address = {};
	socklen_t address_len = sizeof(struct sockaddr_in6);

	ssize_t recv_len = recvfrom(lssdp->sock, buffer, sizeof(buffer), 0,
	                            (struct sockaddr *)&address, &address_len);
	if (recv_len == -1) {
		//lssdp_error("recvfrom fd %d failed, errno = %s (%d)\n", lssdp->sock, strerror(errno), errno);
		return -1;
	}

	// parse SSDP packet to struct
	lssdp_packet packet = {};
	if (lssdp_packet_parser(buffer, recv_len, &packet) != 0) {
		return 0;
	}

	// check search target
	if (strcmp(packet.st, lssdp->header.search_target) != 0) {
		// search target is not match
		if (lssdp->debug) {
			lssdp_info("RECV <- %-8s   not match with %-14s %s\n", packet.method,
			           lssdp->header.search_target, packet.location);
		}
		return 0;
	}

	// M-SEARCH: send RESPONSE back
	if (strcmp(packet.method, MSEARCH) == 0) {
		lssdp_debug("Sending response\n");
		lssdp_send_response(lssdp, address);
		lssdp_debug("Response sent\n");
		return 0;
	}

	if (strcmp(packet.method, NOTIFY) == 0) {
		if(strcmp("ssdp:byebye",packet.nts) == 0) {
			packet.max_age = 0;
			neighbor_list_add(lssdp, packet);
		} else {
			neighbor_list_add(lssdp, packet);
		}
	}

	if (strcmp(packet.method, RESPONSE) == 0) {
		neighbor_list_add(lssdp, packet);
	}

	// invoke packet received callback
	if (lssdp->packet_received_callback != NULL) {
		lssdp->packet_received_callback(lssdp, buffer, recv_len);
	}

	return 0;
}

// 05. lssdp_send_msearch
int lssdp_send_msearch(lssdp_ctx * lssdp) {
	if (lssdp == NULL) {
		lssdp_error("lssdp should not be NULL\n");
		return -1;
	}


	// 1. set M-SEARCH packet
	char msearch[LSSDP_BUFFER_LEN] = {};
	snprintf(msearch, sizeof(msearch),
	         "%s"
	         "HOST:%s:%d\r\n"
	         "MAN:\"ssdp:discover\"\r\n"
	         "MX:1\r\n"
	         "ST:%s\r\n"
	         "USER-AGENT:OS/version product/version\r\n"
	         "\r\n",
	         HEADER_MSEARCH,              // HEADER
	         lssdp->config.ADDR_MULTICAST, lssdp->port, // HOST
	         lssdp->header.search_target         // ST (Search Target)
	        );

	// 2. send M-SEARCH to each interface
	int ret = send_multicast_data(msearch, lssdp);
	if (ret < 0) {
		lssdp_error("failed to send multicast data\n");
		return -1;
	}

	return 0;
}



int lssdp_send_byebye(lssdp_ctx * lssdp) {
	if (lssdp == NULL) {
		lssdp_error("lssdp should not be NULL\n");
		return -1;
	}


	// set notify packet
	char notify[LSSDP_BUFFER_LEN] = {};
	// not used: char * domain = lssdp->header.location.domain;
	snprintf(notify, sizeof(notify),
	         "%s"
	         "HOST:%s:%d\r\n"
	         "CACHE-CONTROL:max-age=%d\r\n"
	         "LOCATION:%s%s%s\r\n"
	         "SERVER:OS/version product/version\r\n"
	         "NT:%s\r\n"
	         "NTS:ssdp:byebye\r\n"
	         "USN:%s\r\n"
	         "SM_ID:%s\r\n"
	         "DEV_TYPE:%s\r\n"
	         "\r\n",
	         HEADER_NOTIFY,                              // HEADER
	         lssdp->config.ADDR_MULTICAST, lssdp->port,  // HOST
	         lssdp->header.max_age,              // LOCATION
	         lssdp->header.location.prefix,              // LOCATION
	         lssdp->header.location.domain,              // LOCATION
	         lssdp->header.location.suffix,
	         lssdp->header.search_target,                // NT (Notify Type)
	         lssdp->header.unique_service_name,          // USN
	         lssdp->header.sm_id,                        // SM_ID    (addtional field)
	         lssdp->header.device_type                   // DEV_TYPE (addtional field)
	        );

	// send NOTIFY
	int ret = send_multicast_data(notify, lssdp);
	if (ret < 0) {
		lssdp_error("failed to send multicast data\n");
		return -1;
	}

	return 0;
}



// 06. lssdp_send_notify
int lssdp_send_notify(lssdp_ctx * lssdp) {
	if (lssdp == NULL) {
		lssdp_error("lssdp should not be NULL\n");
		return -1;
	}


	// set notify packet
	char notify[LSSDP_BUFFER_LEN] = {};
	// not used: char * domain = lssdp->header.location.domain;
	snprintf(notify, sizeof(notify),
	         "%s"
	         "HOST:%s:%d\r\n"
	         "CACHE-CONTROL:max-age=%d\r\n"
	         "LOCATION:%s%s%s\r\n"
	         "SERVER:OS/version product/version\r\n"
	         "NT:%s\r\n"
	         "NTS:ssdp:alive\r\n"
	         "USN:%s\r\n"
	         "SM_ID:%s\r\n"
	         "DEV_TYPE:%s\r\n"
	         "\r\n",
	         HEADER_NOTIFY,                              // HEADER
	         lssdp->config.ADDR_MULTICAST, lssdp->port,  // HOST
	         lssdp->header.max_age,
	         lssdp->header.location.prefix,              // LOCATION
	         lssdp->header.location.domain,              // LOCATION
	         lssdp->header.location.suffix,
	         lssdp->header.search_target,                // NT (Notify Type)
	         lssdp->header.unique_service_name,          // USN
	         lssdp->header.sm_id,                        // SM_ID    (addtional field)
	         lssdp->header.device_type                   // DEV_TYPE (addtional field)
	        );

	// send NOTIFY
	int ret = send_multicast_data(notify, lssdp);
	if (ret < 0) {
		lssdp_error("failed to send multicast data\n");
		return -1;
	}

	return 0;
}


// 08. lssdp_set_log_callback
void lssdp_set_log_callback(void (* callback)(const char * file,
                            const char * tag, int level, int line, const char * func,
                            const char * message)) {
	_log_callback = callback;
}


/** Internal Function **/

static int send_multicast_data(const char * data , lssdp_ctx*lssdp) {


	if (data == NULL) {
		lssdp_error("data should not be NULL\n");
		return -1;
	}

	size_t data_len = strlen(data);
	if (data_len == 0) {
		lssdp_error("data length should not be empty\n");
		return -1;
	}


	struct addrinfo*  multicastAddr2 = NULL;     /* Multicast Address */
	struct addrinfo hints = { 0 };    /* Hints for name lookup */
	int  multicastTTL =255;           /* Arg: TTL of multicast packets */

	int sock;
	hints.ai_family   = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags    = AI_NUMERICHOST;


	int status;
	if ((status = getaddrinfo(lssdp->config.ADDR_MULTICAST,
	                          lssdp->config.multicastPort, &hints,
	                          &multicastAddr2)) != 0 )
	{
		lssdp_error("getaddrinfo: %s\n", gai_strerror(status));
		return -1;
	}


	lssdp_info("Using %s\n",
	           multicastAddr2->ai_family == PF_INET6 ? "IPv6" : "IPv4");


	/* Create socket for sending multicast datagrams */
	if ( (sock = socket(multicastAddr2->ai_family, multicastAddr2->ai_socktype,
	                    0)) < 0 ) {
		lssdp_error("Cannot create multicast socket: ");
		goto fail;
	}


	int loop = 1;
	setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop))  ;

	/* Set TTL of multicast packet */

	if ( setsockopt(sock,
	                multicastAddr2->ai_family == PF_INET6 ? IPPROTO_IPV6        : IPPROTO_IP,
	                multicastAddr2->ai_family == PF_INET6 ? IPV6_MULTICAST_HOPS : IP_MULTICAST_TTL,
	                (char*) &multicastTTL, sizeof(multicastTTL)) != 0 ) {
		lssdp_error("Cannot set multicast ttl: ");
		goto fail_and_close;
	}


	/* set the sending interface */
	in_addr_t iface = INADDR_ANY;

	if(setsockopt (sock,
	               multicastAddr2->ai_family == PF_INET6 ? IPPROTO_IPV6 : IPPROTO_IP,
	               multicastAddr2->ai_family == PF_INET6 ? IPV6_MULTICAST_IF : IP_MULTICAST_IF,
	               (char*)&iface, sizeof(iface)) != 0)  {
		lssdp_error("Cannot set multicast interface");
		goto fail_and_close;
	}




	// 5. send data
	if (sendto(sock, data, data_len, 0, multicastAddr2->ai_addr,
	           multicastAddr2->ai_addrlen) == -1) {
		lssdp_error("Error sending multicast data: ");
		goto fail_and_close;
	}


	close(sock);
	freeaddrinfo(multicastAddr2);

	return 0;

fail_and_close:
	close(sock);

fail:
	freeaddrinfo(multicastAddr2);

	return -1;
}

static int lssdp_send_response(lssdp_ctx * lssdp, struct sockaddr_in6 address) {
	lssdp_info("Now sending response Packet\n");

	// 2. set response packet
	char response[LSSDP_BUFFER_LEN] = {};
	// not used: char * domain = lssdp->header.location.domain;
	int response_len = snprintf(response, sizeof(response),
	                            "%s"
	                            "CACHE-CONTROL:max-age=%d\r\n"
	                            "DATE:\r\n"
	                            "EXT:\r\n"
	                            "LOCATION:%s%s%s\r\n"
	                            "SERVER:OS/version product/version\r\n"
	                            "ST:%s\r\n"
	                            "USN:%s\r\n"
	                            "SM_ID:%s\r\n"
	                            "DEV_TYPE:%s\r\n"
	                            "\r\n",
	                            HEADER_RESPONSE,                     // HEADER
	                            lssdp->header.max_age,
	                            lssdp->header.location.prefix,              // LOCATION
	                            lssdp->header.location.domain,
	                            lssdp->header.location.suffix,
	                            lssdp->header.search_target,                // ST (Search Target)
	                            lssdp->header.unique_service_name,          // USN
	                            lssdp->header.sm_id,                        // SM_ID    (addtional field)
	                            lssdp->header.device_type                   // DEV_TYPE (addtional field)
	                           );

	printf("Setting Port Address\n");

	// 3. set port to address
	address.sin6_port = htons(lssdp->port);

	// 4. send data
	if (sendto(lssdp->sock, response, response_len, 0, (struct sockaddr *)&address,
	           sizeof(struct sockaddr_in6)) == -1) {
		//    lssdp_error("send RESPONSE to %s failed, errno = %s (%d)\n", msearch_ip, strerror(errno), errno);
		lssdp_error("send Response Failed: ");
		return -1;
	}

	return 0;
}

int lssdp_packet_parser(const char * data, size_t data_len,
                        lssdp_packet * packet) {

	lssdp_debug("Trying to parse packet\n");

	if (data == NULL) {
		lssdp_error("data should not be NULL\n");
		return -1;
	}

	if (data_len != strlen(data)) {
		lssdp_error("data_len (%zu) is not match to the data length (%zu)\n", data_len,
		            strlen(data));
		return -1;
	}

	if (packet == NULL) {
		lssdp_error("packet should not be NULL\n");
		return -1;
	}

	// 1. compare SSDP Method Header: M-SEARCH, NOTIFY, RESPONSE
	size_t i;
	if ((i = strlen(HEADER_MSEARCH)) < data_len
	        && memcmp(data, HEADER_MSEARCH, i) == 0) {
		strcpy(packet->method, MSEARCH);
	} else if ((i = strlen(HEADER_NOTIFY)) < data_len
	           && memcmp(data, HEADER_NOTIFY, i) == 0) {
		strcpy(packet->method, NOTIFY);
	} else if ((i = strlen(HEADER_RESPONSE)) < data_len
	           && memcmp(data,HEADER_RESPONSE, i) == 0) {
		strcpy(packet->method, RESPONSE);
	} else {
		lssdp_warn("received unknown SSDP packet\n");
		lssdp_debug("%s\n", data);
		return -1;
	}

	// 2. parse each field line
	size_t start = i;
	for (i = start; i < data_len; i++) {
		if (data[i] == '\n' && i - 1 > start && data[i - 1] == '\r') {
			parse_field_line(data, start, i - 2, packet);
			start = i + 1;
		}
	}

	// 3. set update_time
	long long current_time = get_current_time();
	if (current_time < 0) {
		lssdp_error("got invalid timestamp %lld\n", current_time);
		return -1;
	}
	packet->update_time = current_time;
	return 0;
}

static int parse_field_line(const char * data, size_t start, size_t end,
                            lssdp_packet * packet) {
	// 1. find the colon
	if (data[start] == ':') {
		lssdp_warn("the first character of line should not be colon\n");
		lssdp_debug("%s\n", data);
		return -1;
	}

	int colon = get_colon_index(data, start + 1, end);
	if (colon == -1) {
		lssdp_warn("there is no colon in line\n");
		lssdp_debug("%s\n", data);
		return -1;
	}

	if (colon == end) {
		// value is empty
		return -1;
	}


	// 2. get field, field_len
	size_t i = start;
	size_t j = colon - 1;
	if (trim_spaces(data, &i, &j) == -1) {
		return -1;
	}
	const char * field = &data[i];
	size_t field_len = j - i + 1;


	// 3. get value, value_len
	i = colon + 1;
	j = end;
	if (trim_spaces(data, &i, &j) == -1) {
		return -1;
	};
	const char * value = &data[i];
	size_t value_len = j - i + 1;


	// 4. set each field's value to packet
	if (field_len == strlen("st") && strncasecmp(field, "st", field_len) == 0) {
		memcpy(packet->st, value,
		       value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("nt") && strncasecmp(field, "nt", field_len) == 0) {
		memcpy(packet->st, value,
		       value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("nts") && strncasecmp(field, "nts", field_len) == 0) {
		memcpy(packet->nts, value,
		       value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("usn") && strncasecmp(field, "usn", field_len) == 0) {
		memcpy(packet->usn, value,
		       value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("location")
	        && strncasecmp(field, "location", field_len) == 0) {
		memcpy(packet->location, value,
		       value_len < LSSDP_LOCATION_LEN ? value_len : LSSDP_LOCATION_LEN - 1);
		return 0;
	}

	if (field_len == strlen("sm_id")
	        && strncasecmp(field, "sm_id", field_len) == 0) {
		memcpy(packet->sm_id, value,
		       value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("dev_type")
	        && strncasecmp(field, "dev_type", field_len) == 0) {
		memcpy(packet->device_type, value,
		       value_len < LSSDP_FIELD_LEN ? value_len : LSSDP_FIELD_LEN - 1);
		return 0;
	}

	if (field_len == strlen("CACHE-CONTROL")
	        && strncasecmp(field, "CACHE-CONTROL", field_len) == 0) {
		if (strncasecmp(value, "max-age=", 8) == 0) {
			int max_packet_age = atoi(value+8);
			if ( max_packet_age == 0)
				max_packet_age = 10;
			packet->max_age = max_packet_age;
		}
		return 0;
	}

	lssdp_error("FIELD: %s not handled\n",field);
	// the field is not in the struct packet
	return 0;
}



static int neighbor_list_add(lssdp_ctx * lssdp, const lssdp_packet packet) {
	lssdp_nbr * last_nbr = lssdp->neighbor_list;

	bool is_changed = false;
	lssdp_nbr * nbr;
	for (nbr = lssdp->neighbor_list; nbr != NULL; last_nbr = nbr, nbr = nbr->next) {
		if (strcmp(nbr->usn , packet.usn) != 0) {
			// location is not match
			continue;
		}

		/* location is not found in SSDP list: update neighbor */

		// usn
		if (strcmp(nbr->usn, packet.usn) != 0) {
			lssdp_debug("neighbor usn is changed. (%s -> %s)\n", nbr->usn, packet.usn);
			memcpy(nbr->usn, packet.usn, LSSDP_FIELD_LEN);
			is_changed = true;
		}

		// sm_id
		if (strcmp(nbr->sm_id, packet.sm_id) != 0) {
			lssdp_debug("neighbor sm_id is changed. (%s -> %s)\n", nbr->sm_id,
			            packet.sm_id);
			memcpy(nbr->sm_id, packet.sm_id, LSSDP_FIELD_LEN);
			is_changed = true;
		}

		// device type
		if (strcmp(nbr->device_type, packet.device_type) != 0) {
			lssdp_debug("neighbor device_type is changed. (%s -> %s)\n", nbr->device_type,
			            packet.device_type);
			memcpy(nbr->device_type, packet.device_type, LSSDP_FIELD_LEN);
			is_changed = true;
		}

		// update_time
		nbr->update_time = packet.update_time;
		nbr->max_age = packet.max_age;
		goto end;
	}


	/* location is not found in SSDP list: add to list */

	// 1. memory allocate lssdp_nbr
	nbr = (lssdp_nbr *) malloc(sizeof(lssdp_nbr));
	if (nbr == NULL) {
		lssdp_error("malloc failed, errno = %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	// 2. setup neighbor
	memcpy(nbr->usn,         packet.usn,         LSSDP_FIELD_LEN);
	memcpy(nbr->sm_id,       packet.sm_id,       LSSDP_FIELD_LEN);
	memcpy(nbr->device_type, packet.device_type, LSSDP_FIELD_LEN);
	memcpy(nbr->location,    packet.location,    LSSDP_LOCATION_LEN);
	nbr->update_time = packet.update_time;
	nbr->max_age = packet.max_age;
	nbr->next = NULL;

	// 3. add neighbor to the end of list
	if (last_nbr == NULL) {
		// it's the first neighbor
		lssdp->neighbor_list = nbr;
	} else {
		last_nbr->next = nbr;
	}

	is_changed = true;
end:
	// invoke neighbor list changed callback
	if (lssdp->neighbor_list_changed_callback != NULL && is_changed == true) {
		lssdp->neighbor_list_changed_callback(lssdp);
	}

	return 0;
}


// 07. lssdp_neighbor_check_timeout
int lssdp_neighbor_check_timeout(lssdp_ctx * lssdp) {
	if (lssdp == NULL) {
		lssdp_error("lssdp should not be NULL\n");
		return -1;
	}

	long long current_time = get_current_time();
	if (current_time < 0) {
		lssdp_error("got invalid timestamp %lld\n", current_time);
		return -1;
	}

	bool is_changed = false;
	lssdp_nbr * prev = NULL;
	lssdp_nbr * nbr  = lssdp->neighbor_list;


	while (nbr != NULL) {
		long pass_time = current_time - nbr->update_time;
		if (pass_time < nbr->max_age*1000) {
			prev = nbr;
			nbr  = nbr->next;
			continue;
		}

		is_changed = true;
		fprintf(stderr,"remove timeout SSDP neighbor: %s (%s) (%ldms) maxAGE:%d\n",
		        nbr->sm_id, nbr->location, pass_time, nbr->max_age);

		if (prev == NULL) {
			// it's first neighbor in list
			lssdp->neighbor_list = nbr->next;
			free(nbr);
			nbr = lssdp->neighbor_list;
		} else {
			prev->next = nbr->next;
			free(nbr);
			nbr = prev->next;
		}
	}

	// invoke neighbor list changed callback
	if (is_changed == true && lssdp->neighbor_list_changed_callback != NULL) {
		lssdp->neighbor_list_changed_callback(lssdp);
	}
	return 0;
}



static int get_colon_index(const char * string, size_t start, size_t end) {
	size_t i;
	for (i = start; i <= end; i++) {
		if (string[i] == ':') {
			return i;
		}
	}
	return -1;
}

static int trim_spaces(const char * string, size_t * start, size_t * end) {
	int i = *start;
	int j = *end;

	while (i <= *end   && (!isprint(string[i]) || isspace(string[i]))) i++;
	while (j >= *start && (!isprint(string[j]) || isspace(string[j]))) j--;

	if (i > j) {
		return -1;
	}

	*start = i;
	*end   = j;
	return 0;
}

static long long get_current_time() {
	struct timeval time = {};
	if (gettimeofday(&time, NULL) == -1) {
		lssdp_error("gettimeofday failed, errno = %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	return (long long) time.tv_sec * 1000 + (long long) time.tv_usec / 1000;
}

static int lssdp_log(int level, int line, const char * func,
                     const char * format, ...) {
	if (_log_callback == NULL) {
		return -1;
	}

	char message[LSSDP_BUFFER_LEN] = {};

	// create message by va_list
	va_list args;
	va_start(args, format);
	vsnprintf(message, LSSDP_BUFFER_LEN, format, args);
	va_end(args);

	// invoke log callback function
	_log_callback(__FILE__, "SSDP", level, line, func, message);
	return 0;
}



