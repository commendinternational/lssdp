#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>     // select
#include <sys/time.h>   // gettimeofday
#include <list>
#include <algorithm>
#include <thread>
#include <iostream>
#include <curses.h>
#include <ncurses.h>

#include "client.h"

using namespace std;
char in_buf[255];
char out_buf[255];

void print_devices();
void render_screen();
int ping_device(struct device*target);
int add_device(lssdp_packet &packet);
int neighbor_list_changed(lssdp_ctx *ctx);
device* find_device(t_device_list *container, std::string barcode);


WINDOW *device_win;
WINDOW *local_win;

static bool done = false; // flag to request end of main loop thread


long long get_current_time() {
	struct timeval time = {};
	if (gettimeofday(&time, NULL) == -1) {
		return -1;
	}
	return (long long) time.tv_sec * 1000 + (long long) time.tv_usec / 1000;
}



//Find device in device list with matching barcode
//Returns Null if not found
device* find_device(t_device_list *container, std::string barcode) {

	t_device_list::iterator iter;
	for(iter = container->begin(); iter != container->end(); iter++) {
		if((*iter)->barcode == barcode) {
			return (*iter);
		}
	}
	return NULL;
}


bool is_device_unique(t_device_list *container,std::string barcode) {

	if(find_device(container,barcode))
		return false;
	return true;
}

int add_device(lssdp_nbr *nbr) {


	if(!nbr)
		return -1;

	std::string check_barcode = nbr->usn;
	if(!is_device_unique(&paired_device_list,check_barcode)) {
		return -1;
	}

	device *ptr = new device();
	ptr->state=UNREACHABLE;
	ptr->barcode=nbr->usn;
	ptr->update_time=nbr->update_time;
	ptr->max_age = nbr->max_age;
	paired_device_list.push_back(ptr);

	render_screen();
	return 0;
}


int ping_device(struct device*target) {
	return 0;
}



void ping_devices() {
	t_device_list::iterator iter;
	for(iter = paired_device_list.begin(); iter != paired_device_list.end();
	        iter++) {
		ping_device(*iter);
	}
}



void print_paired_devices() {

	int count = 0;
	t_device_list::iterator iter;
	for(iter = paired_device_list.begin(); iter != paired_device_list.end();
	        iter++) {
		count++;
		std::string status;
		if( (*iter)->state ==  PAIRED) {
			status = "PAIRED";
		} else {
			status = "UNREACHABLE";
		}
		wprintw(device_win," %d: %s %s\n" ,count ,(*iter)->barcode.c_str(),
		        status.c_str());
	}
}



lssdp_nbr* get_neighbor_index(int index) {

	int i = 0;
	lssdp_nbr * nbr;

	for (nbr = lssdp.neighbor_list; nbr != NULL; nbr = nbr->next) {
		if( i == index)
			return nbr;
		i++;
	}

	return NULL;
}

struct device * get_device(t_device_list *container ,unsigned int index) {

	if (container->size() > index)
	{
		t_device_list::iterator it = container->begin();
		std::advance(it, index);
		return *it;    // 'it' points to the element at index 'N'
	}


	return NULL;

}


int neighbor_list_changed(lssdp_ctx *ctx) {

	return 0;
}


void print_devices() {

	int i = 0;
	lssdp_nbr * nbr;

	for (nbr = lssdp.neighbor_list; nbr != NULL; nbr = nbr->next) {
		wprintw(device_win," %d   %-12s  Age: %d\n",
		        ++i,
		        nbr->usn,
		        (get_current_time() - nbr->update_time)/1000
		       );
	}
}


void render_local_screen() {
	wclear(local_win);
	wprintw(local_win,"\n");
	wprintw(local_win," Last command: %s\n", out_buf);
	wborder(local_win, '|', '|', '-', '-', '+', '+', '+', '+');
	wrefresh(local_win);
}

void render_screen() {

	wclear(device_win);

	wprintw(device_win,
	        "\n--------------------------- Avaliable Device List -----------------------------\n");
	print_devices();
	wprintw(device_win,
	        "\n---------------------------- End Device List ----------------------------------\n");


	wprintw(device_win,"\n\n\n");

	wprintw(device_win,
	        "----------------------------- Paired Device List --------------------------------\n");
	print_paired_devices();
	wprintw(device_win,
	        "\n---------------------------- End Device List ----------------------------------\n");

	wborder(device_win, '|', '|', '-', '-', '+', '+', '+', '+');

	wrefresh(device_win);			/* Print it on to the real screen */
}



void disconnect_device(struct device *ptr) {

	if(!ptr)
		return;

	t_device_list::iterator iter;
	for(iter = paired_device_list.begin(); iter != paired_device_list.end();
	        iter++) {
		if((*iter)->barcode == ptr->barcode) {
			paired_device_list.remove(*iter);
			return;
		}
	}
}

void mainLoop() {

	lssdp.port = 1900;
	lssdp.debug = false;
	lssdp.packet_received_callback  = NULL;
	lssdp.neighbor_list_changed_callback  = neighbor_list_changed;

	lssdp_init(&lssdp);

	lssdp.config.ADDR_LOCALHOST = "::1";
	lssdp.config.ADDR_MULTICAST = "FF02::C";

	strncpy(lssdp.header.location.prefix,"http://\0",LSSDP_FIELD_LEN);
	strncpy(lssdp.header.location.domain,"test_location",LSSDP_FIELD_LEN);
	strncpy(lssdp.header.location.suffix,":8082\0",LSSDP_FIELD_LEN);

	strncpy(lssdp.header.search_target,"commend_switchbox\0",LSSDP_FIELD_LEN);
	strncpy(lssdp.header.unique_service_name,"Barcode",LSSDP_FIELD_LEN);
	lssdp_socket_create(&lssdp);
	lssdp_send_msearch(&lssdp);


	while (!done) {
		fd_set fs;
		FD_ZERO(&fs);
		FD_SET(lssdp.sock, &fs);
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 500 * 1000;   // 500 ms


		lssdp_neighbor_check_timeout(&lssdp);

		render_screen();

		int ret = select(lssdp.sock + 1, &fs, NULL, NULL, &tv);
		if (ret < 0) {
			break;
		}

		if (ret > 0) {
			lssdp_socket_read(&lssdp);
		}
	}

	lssdp_socket_close(&lssdp);
}


int main() {


	initscr();			/* Start curses mode 		  */
	clear();


	int startx, starty, width, height;
	height = 30;
	width  = 80;
	starty = 0;
	startx = 0;

	device_win = newwin(height, width, starty, startx);
	local_win = newwin(4, width, starty+height+4, startx);

	render_screen();
	render_local_screen();

	std::thread t_MainThread(mainLoop);
	while(true) {
		std::string input;
		wgetstr(local_win,in_buf);
		char*pos;
		if((pos = strstr(in_buf,"add")) != 0) {

			lssdp_nbr* nbr = get_neighbor_index( atoi(pos+3)-1);
			int res = add_device(nbr);
			if(res == 0) {
				snprintf(out_buf,255,"Added device %d",atoi(pos+3));
			}
			else {
				snprintf(out_buf,255,"Cannot Add device");
			}
		} else if ((pos = strstr(in_buf,"rm")) != 0) {
			struct device *tmp = get_device(&paired_device_list, atoi(pos+2)-1);
			if(tmp) {
				disconnect_device(tmp);
				snprintf(out_buf,255,"Removed device %d",atoi(pos+2));
			}
			else {
				snprintf(out_buf,255,"Cannot Remove device");
			}
		} else if ((pos = strstr(in_buf,"quit")) != 0) {
			done = true;
			break;
		} else {
			snprintf(out_buf,255,"Unknown command");
		}
		render_screen();
		render_local_screen();
	}

	t_MainThread.join();

	endwin();

	return EXIT_SUCCESS;
}
