package com.codedisaster.steamworks;

public class SteamException extends Exception {

    public SteamException(String message) {
        super(message);
    }

    public SteamException(Throwable cause) {
        super(cause);
    }
}
