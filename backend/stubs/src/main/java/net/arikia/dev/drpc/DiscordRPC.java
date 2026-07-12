package net.arikia.dev.drpc;

import java.io.File;

/** discord-rpc shadow: no Discord on a Vita. Signatures match the fork
 * bundled in the game jar (DiscordIntegration decompile). */
public final class DiscordRPC {

    public static void discordInitialize(String applicationId, DiscordEventHandlers handlers,
                                         boolean autoRegister, String steamId, File unpackPath) {
    }

    public static void discordUpdatePresence(DiscordRichPresence presence) {
    }

    public static void discordRunCallbacks() {
    }

    public static void discordShutdown() {
    }

    private DiscordRPC() {}
}
