#include "service.h"
#include "lssdp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>     // select
#include <sys/time.h>   // gettimeofday
#include <string>
#include <signal.h>
#include <jni.h>
#include <android/log.h>


#define RESEND_INTERVAL 2

JNIEnv *jenv_service;
static bool done = false; // flag to request end of main loop thread

long long get_current_time() {
    struct timeval time = {};
    if (gettimeofday(&time, NULL) == -1) {
        printf("gettimeofday failed, errno = %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    return (long long) time.tv_sec * 1000 + (long long) time.tv_usec / 1000;
}

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


int launchServer(const char* uuid, const char* ipv6Address, const char* ipv4Address, int localPort, bool ipV6Enabled) {

    lssdp_set_log_callback(android_log);

    done = false;
    lssdp_ctx lssdp;

    lssdp_init(&lssdp);

    lssdp.debug = false;

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
    lssdp.header.max_age = RESEND_INTERVAL;
    //  LOCATION IPv6
    strncpy(lssdp.header.location.prefix,"http://",LSSDP_FIELD_LEN);
    strncpy(lssdp.header.location.domain,ipv4Address,LSSDP_FIELD_LEN);
    snprintf(lssdp.header.location.suffix,LSSDP_FIELD_LEN,":%d",localPort);
    strcat(lssdp.header.location.suffix,"/device.xml");
    //  Server
    strncpy(lssdp.header.server, "Android/1.0  UPnP/2.0 Android Mobile Client/1.0", LSSDP_FIELD_LEN);
    //  NT
    strncpy(lssdp.header.search_target,"urn:schemas-upnp-org:device:commend_cloud:1",LSSDP_FIELD_LEN);
    //  NTS ssdp:alive
    //  USN
    strncpy(lssdp.header.unique_service_name,"uuid:",LSSDP_FIELD_LEN);
    strncat(lssdp.header.unique_service_name,uuid,LSSDP_FIELD_LEN);
    strncat(lssdp.header.unique_service_name,"::urn:schemas-upnp-org:device:commend_cloud:1",LSSDP_FIELD_LEN);



    if(lssdp_socket_create(&lssdp) != 0) {
        return EXIT_FAILURE;
    }
    if(lssdp_send_notify(&lssdp) != 0) {
        return EXIT_FAILURE;
    }

    long long timelast = get_current_time();

    while (!done) {
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(lssdp.sock, &fs);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = (RESEND_INTERVAL * 10) * 1000;

        if((get_current_time() - timelast) > RESEND_INTERVAL*1000) {
            timelast = get_current_time();
            if(lssdp_send_notify(&lssdp) != 0) {
                return EXIT_FAILURE;
            }

        }
    }

    if(lssdp_send_byebye(&lssdp) != 0) {
        return EXIT_FAILURE;
    }

    if(lssdp_socket_close(&lssdp) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

extern "C" {
int
Java_com_commend_lssdplib_Lssdp_startLssdpService(
        JNIEnv *env,
        jobject obj,
        jstring uuid,
        jstring ipv6Address,
        jstring ipv4Address,
        jint    localPort,
        jboolean ipV6Enabled) {

    jenv_service = env;
    const char* ipv6AddressNative = env->GetStringUTFChars(ipv6Address, 0);
    const char* ipv4AddressNative = env->GetStringUTFChars(ipv4Address, 0);
    const char* uuidNative = env->GetStringUTFChars(uuid, 0);
    int localPortNative = (int)localPort;
    bool ipV6EnabledNative = (bool) ipV6Enabled;

    int proc = launchServer(uuidNative, ipv6AddressNative, ipv4AddressNative, localPortNative, ipV6EnabledNative);

    return proc;
}

void
Java_com_commend_lssdplib_Lssdp_stopLssdpService(
        JNIEnv *env,
        jobject obj) {

    done = true;
}
}