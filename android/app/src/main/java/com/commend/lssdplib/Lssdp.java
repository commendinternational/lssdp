package com.commend.lssdplib;

import java.util.LinkedList;
import java.util.List;

public class Lssdp {

    private static Lssdp instance;
    private List<LssdpListener> lssdpListeners  = new LinkedList<>();

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("lssdp-lib");
    }

    /**
     * Private contuctor to prevent instantiation.
     */
    private Lssdp() {

    }

    /**
     * Access point to Lssdp singleton.
     * @return singleton of Lssdp
     */
    public static synchronized Lssdp getInstance() {

        if(instance == null) {
            instance = new Lssdp();
        }

        return instance;
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native void startLssdpClient(String uuid, boolean ipV6Enabled);
    public native void stopLssdpClient();
    public native int startLssdpService(String uuid, String ipv6Address, String ipv4Address, int localPort, boolean ipV6Enabled);
    public native void stopLssdpService();

    /**
     * Method called when devices are cleared.
     */
    private void clearDevicesDiscovered() {

        for (LssdpListener lssdpListener : lssdpListeners) {
            lssdpListener.OnDeviceListClear();
        }
    }

    /**
     * Method called when a device is found.
     * @param device device discovered
     */
    private void deviceDiscovered(String device) {

        for (LssdpListener lssdpListener : lssdpListeners) {
            lssdpListener.OnDeviceFound(device);
        }
    }

    /**
     * Add listener to Lssdp.
     * @param lssdpListener listener to be added
     */
    public void addLssdpListener(LssdpListener lssdpListener) {

        lssdpListeners.add(lssdpListener);
    }

    /**
     * Remove listener from Lssdp.
     * @param lssdpListener listener to be removed
     */
    public void removeLssdpListener(LssdpListener lssdpListener) {

        lssdpListeners.remove(lssdpListener);
    }

    /**
     * Interface definition for a callback to be invoked when a device is found or the list of devices cleared.
     */
    public interface LssdpListener {

        void OnDeviceFound(String device);

        void OnDeviceListClear();
    }

}
