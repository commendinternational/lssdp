#ifndef __LSSDP_H
#define __LSSDP_H

#include <stdbool.h>  // bool, true, false
#include <stdint.h>   // uint32_t

#ifdef __cplusplus
extern "C"
{
#endif

/* Some platforms use IPV6_JOIN_GROUP instead if
 * IPV6_ADD_MEMBERSHIP. The semantics are same, though. */
#ifndef IPV6_ADD_MEMBERSHIP
#ifdef IPV6_JOIN_GROUP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif /* IPV6_JOIN_GROUP */
#endif /* !IPV6_ADD_MEMBERSHIP */
	
#define LSSDP_IP_LEN                128
#define LSSDP_FIELD_LEN             128
#define LSSDP_LOCATION_LEN          256


// LSSDP Log Level
enum LSSDP_LOG {
	LSSDP_LOG_DEBUG = 1 << 0,
	LSSDP_LOG_INFO  = 1 << 1,
	LSSDP_LOG_WARN  = 1 << 2,
	LSSDP_LOG_ERROR = 1 << 3
};



/** Global Variable **/
typedef struct lssdp_config {

	char* multicastPort;

	const char * ADDR_LOCALHOST;
	const char * ADDR_MULTICAST;
	const char * MULTICAST_IF;


} lssdp_config;

typedef struct lssdp_nbr {
	char	usn         [LSSDP_FIELD_LEN];          // Unique Service Name (Device Name or MAC)
	char    location    [LSSDP_LOCATION_LEN];       // URL or IP(:Port)

	/* Additional SSDP Header Fields */
	char        sm_id       [LSSDP_FIELD_LEN];
	char        device_type [LSSDP_FIELD_LEN];
	long long   update_time;
	int max_age;
	struct lssdp_nbr * next;
} lssdp_nbr;



struct t_location {                                     // Location (optional):
	char    prefix   [LSSDP_FIELD_LEN];  // Protocal: "https://" or "http://"
	char    domain   [LSSDP_FIELD_LEN];  // if domain is empty, using Interface IP as default
	char    suffix   [LSSDP_FIELD_LEN];  // URI or Port: "/index.html" or ":80"
};

/** Struct: lssdp_packet **/
typedef struct lssdp_packet {
	char method      [LSSDP_FIELD_LEN];      // M-SEARCH, NOTIFY, RESPONSE, BYE
	char st          [LSSDP_FIELD_LEN];      // Search Target
	char nts         [LSSDP_FIELD_LEN];      // Search Target
	char usn         [LSSDP_FIELD_LEN];      // Unique Service Name
	char location    [LSSDP_LOCATION_LEN];   // Location

	int max_age;
	long long       update_time;
} lssdp_packet;





struct t_header {
	/* SSDP Standard Header Fields */
	char search_target       [LSSDP_FIELD_LEN];  // Search Target
	char unique_service_name [LSSDP_FIELD_LEN];  // Unique Service Name: MAC or User Name
	int max_age;
	struct t_location location;
	char server              [LSSDP_FIELD_LEN]; // software product/version implementing LSSDP
};




typedef struct lssdp_ctx {
	int             sock;                   // SSDP listening socket
	unsigned short  port;                   // SSDP port (0x0000 ~ 0xFFFF)
	lssdp_nbr *     neighbor_list;          // SSDP neighbor list
	long            neighbor_timeout;       // milliseconds
	bool            debug;                  // show debug log
	struct lssdp_config config;
	char* multicastPortTest;
	struct t_header header;
	int (* neighbor_list_changed_callback)     (struct lssdp_ctx * lssdp);
	int (* packet_received_callback)           (struct lssdp_ctx * lssdp,
	        const char * packet, size_t packet_len);
} lssdp_ctx;



//Set default values of lssdp
void lssdp_init(lssdp_ctx * lssdp);


int lssdp_socket_create(lssdp_ctx * lssdp);

int lssdp_socket_close(lssdp_ctx * lssdp);

/*
 * lssdp_socket_read
 *
 * read SSDP socket.
 *
 * 1. if read success, packet_received_callback will be invoked.
 * 2. if received SSDP packet is match to Search Target (lssdp.header.search_target),
 *     - M-SEARCH: send RESPONSE back
 *     - NOTIFY/RESPONSE: add/update to SSDP neighbor list
 *
 * Note:
 *  - SSDP socket and port must be setup ready before call this function. (sock, port > 0)
 *
 * @param lssdp
 * @return = 0      success
 *         < 0      failed
 */
int lssdp_socket_read(lssdp_ctx * lssdp);
int lssdp_send_msearch(lssdp_ctx * lssdp);
int lssdp_send_byebye(lssdp_ctx * lssdp);
int lssdp_send_notify(lssdp_ctx * lssdp);

int lssdp_packet_parser(const char * data, size_t data_len,
                        lssdp_packet * packet);



int lssdp_neighbor_check_timeout(lssdp_ctx * lssdp);


void lssdp_set_log_callback(void (* callback)(const char * file,
                            const char * tag, int level, int line, const char * func,
                            const char * message));



#ifdef __cplusplus
}
#endif



#endif
