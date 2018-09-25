#include "client.h"
#include "lssdp.h"

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
#include <jni.h>
#include <android/log.h>

#define RESEND_INTERVAL 2

using namespace std;

JNIEnv *jenv;
jobject jobj;
char deviceDetail [1024];

static bool done = false; // flag to request end of main loop thread

void deviceDiscovered(const char * device);
void clearDevicesDiscovered();
int ssdp_client_byebye_callback(lssdp_ctx * lssdp, const lssdp_packet packet);
void deviceSendByeBye(const char * uuid);

static void android_log(const char * file, const char * tag, int level,
                        int line,
                        const char * func, const char * message) {

    int prio = ANDROID_LOG_UNKNOWN;

    switch(level) {
        case LSSDP_LOG_DEBUG:
            prio = ANDROID_LOG_DEBUG;
            break;
        case LSSDP_LOG_INFO:
            prio = ANDROID_LOG_INFO;
            break;
        case LSSDP_LOG_WARN:
            prio = ANDROID_LOG_WARN;
            break;
        case LSSDP_LOG_ERROR:
            prio = ANDROID_LOG_ERROR;
            break;
    }
    __android_log_print(prio, tag, "%s", message);
}

static long long get_current_time() {
    struct timeval time = {};
    if (gettimeofday(&time, NULL) == -1) {
        return -1;
    }
    return (long long) time.tv_sec * 1000 + (long long) time.tv_usec / 1000;
}

int neighbor_list_changed(lssdp_ctx *ctx) {

    int i = 0;
    lssdp_nbr * nbr;

    if(done) {
        return 0;
    }

    clearDevicesDiscovered();
    if(jenv->ExceptionCheck()) {
        jenv->ExceptionClear();
        return 0;
    }

    for (nbr = lssdp.neighbor_list; nbr != NULL; nbr = nbr->next) {

        for(int a = 0; a < sizeof(deviceDetail); a++){
            deviceDetail[a] = 0;
        }
        
        deviceDiscovered(strcat(deviceDetail, nbr->location));
    }

    return 0;
}

int ssdp_client_byebye_callback(lssdp_ctx * lssdp, const lssdp_packet packet) {
    
    if(done) {
        return 0;
    }
    
    deviceSendByeBye((const char *)packet.usn);

    return 0;
}


int launchClient(const char* uuid, bool ipV6Enabled) {

    lssdp_set_log_callback(android_log);

    done = false;
    lssdp.debug = false;

    lssdp_init(&lssdp);

    lssdp.neighbor_list_changed_callback  = neighbor_list_changed;
    lssdp.neighbor_list_byebye_callback = &ssdp_client_byebye_callback;

    //  Interface
    lssdp.config.MULTICAST_IF = "wlan0";

    //  HOST
    if (ipV6Enabled) {
        lssdp.config.ADDR_MULTICAST = UPNP_IPV6;
    } else {
        lssdp.config.ADDR_MULTICAST = UPNP_IPV4;
    }
    lssdp.port = 1900;


    //  CACHE-CONTROL
    lssdp.header.max_age = 60;

    //  LOCATION IPv6
    strncpy(lssdp.header.location.prefix,"",LSSDP_FIELD_LEN);
    strncpy(lssdp.header.location.domain,"",LSSDP_FIELD_LEN);
    strncpy(lssdp.header.location.suffix,"",LSSDP_FIELD_LEN);

    //  Server
    strncpy(lssdp.header.server, "Android/1.0  UPnP/2.0 Android Mobile Client/1.0", LSSDP_FIELD_LEN);
    //  NT
    strncpy(lssdp.header.search_target,"urn:schemas-upnp-org:device:commend_cloud:1",LSSDP_FIELD_LEN);
    //  NTS
    //  USN
    strncpy(lssdp.header.unique_service_name,"uuid:",LSSDP_FIELD_LEN);
    strncat(lssdp.header.unique_service_name,uuid,LSSDP_FIELD_LEN);
    strncat(lssdp.header.unique_service_name,"::urn:schemas-upnp-org:device:commend_cloud:1",LSSDP_FIELD_LEN);


    if(lssdp_socket_create(&lssdp) != 0) {
        return EXIT_FAILURE;
    }
    if(lssdp_send_msearch(&lssdp) != 0) {
        return EXIT_FAILURE;
    }

    while (!done) {
        
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(lssdp.sock, &fs);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000;

        if(lssdp_neighbor_check_timeout(&lssdp) != 0) {
            return EXIT_FAILURE;
        }

        int ret = select(lssdp.sock + 1, &fs, NULL, NULL, &tv);
        if (ret > 0) {
            if(lssdp_socket_read(&lssdp) != 0) {
                return EXIT_FAILURE;
            }
        } else if (ret < 0) {
            return EXIT_FAILURE;
        }
    }

    if(lssdp_socket_close(&lssdp) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void clearDevicesDiscovered() {

    jclass cls = jenv->GetObjectClass(jobj);
    jmethodID mid = jenv->GetMethodID(cls, "clearDevicesDiscovered", "()V");
    jenv->CallVoidMethod(jobj, mid);

    jenv->DeleteLocalRef(cls);
}

void deviceDiscovered(const char * device) {

    jclass cls = jenv->GetObjectClass(jobj);
    jmethodID mid = jenv->GetMethodID(cls, "deviceDiscovered", "(Ljava/lang/String;)V");
    jstring deviceString = jenv->NewStringUTF(device);
    jenv->CallVoidMethod(jobj, mid, deviceString);

    jenv->DeleteLocalRef(cls);
    jenv->DeleteLocalRef(deviceString);
}

void deviceSendByeBye(const char * uuid) {

    jclass cls = jenv->GetObjectClass(jobj);
    jmethodID mid = jenv->GetMethodID(cls, "deviceSendByeBye", "(Ljava/lang/String;)V");
    jstring deviceString = jenv->NewStringUTF(uuid);
    jenv->CallVoidMethod(jobj, mid, deviceString);

    jenv->DeleteLocalRef(cls);
    jenv->DeleteLocalRef(deviceString);
}


extern "C" {
void
Java_com_commend_lssdplib_Lssdp_startLssdpClient(
        JNIEnv *env,
        jobject obj,
        jstring uuid,
        jboolean ipV6Enabled) {

    jobj = obj;
    jenv = env;
    const char* uuidNative = env->GetStringUTFChars(uuid, 0);
    bool ipV6EnabledNative = (bool) ipV6Enabled;

    launchClient(uuidNative, ipV6EnabledNative);
}

void
Java_com_commend_lssdplib_Lssdp_stopLssdpClient(
        JNIEnv *env,
        jobject obj) {

    done = true;
}
}

