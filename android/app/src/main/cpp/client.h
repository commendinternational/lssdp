#ifndef CLIENT_H
#define CLIENT_H


#include <string>
#include <list>
#include "lssdp.h"

typedef enum {PAIRED, UNREACHABLE} t_state;


typedef struct device
{
	t_state state;
	std::string barcode;
	std::string location;
	long long update_time;
	int max_age;
} device;

lssdp_ctx lssdp;
typedef std::list<device*> t_device_list;
t_device_list device_list;
t_device_list paired_device_list;

#endif
