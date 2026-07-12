package net.arikia.dev.drpc;

public class DiscordRichPresence {

    public static class Builder {

        public Builder(String state) {
        }

        public Builder setDetails(String details) {
            return this;
        }

        public DiscordRichPresence build() {
            return new DiscordRichPresence();
        }
    }
}
