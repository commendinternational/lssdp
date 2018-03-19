# Introduction 

Lssdp is a light library that will detect other devices in network. It will implement SSDP protocol and will run under IPv6.
This module, creates a aar library with lssdb bin and a java class with JNI bindings and library loading.

Lssdp originally developed on:
https://github.com/commendinternational/lssdp

current project is an adaptation of that library for Android.

# Getting Started

To create aar just open project in Android Studio and follow this steps:
1. Open Gradle Tool window.
2. Expand lssdp_lib -> Tasks -> build.
3. Right click on assembleDebug or assemsbleRelease.
4. Click on Run option.

# Build and Test

To use the library on any application, just import the class commend.com.lssdplib.Lssdp.
The loading of library is made automatically. 
The Lssdp is a Singleton so, to use it just call Lssdp.getInstance() and the following methods:

```java
/**
 * Starts running the lssdp client in background.
 */
public native void startLssdpClient();

/**
 * Stops the lssdp background thread.
 */
public native void stoptLssdpClient();
    
```

When a device is discovered or list is clean the LssdpListener registered in singleton are called:

```java
/**
 * Interface definition for a callback to be invoked when a device is found or the list of devices cleared.
 */
public interface LssdpListener {

    void OnDeviceFound(String device);

    void OnDeviceListClear();
}
```


# Contribute
TODO: Explain how other users and developers can contribute to make your code better. 

If you want to learn more about creating good readme files then refer the following [guidelines](https://www.visualstudio.com/en-us/docs/git/create-a-readme). You can also seek inspiration from the below readme files:
- [ASP.NET Core](https://github.com/aspnet/Home)
- [Visual Studio Code](https://github.com/Microsoft/vscode)
- [Chakra Core](https://github.com/Microsoft/ChakraCore)