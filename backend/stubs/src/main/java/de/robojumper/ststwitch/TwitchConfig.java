package de.robojumper.ststwitch;

import java.util.Optional;

/** Twitch integration shadow: reports "no twitch.properties", so the game
 * takes its existing disabled path (TopPanel checks configFileExists first). */
public class TwitchConfig {

    public static boolean configFileExists() {
        return false;
    }

    public static void createConfig() {
    }

    public static Optional<TwitchConfig> readConfig() {
        return Optional.empty();
    }

    public boolean isEnabled() {
        return false;
    }
}
