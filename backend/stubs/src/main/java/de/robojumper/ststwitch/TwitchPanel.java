package de.robojumper.ststwitch;

/** Never instantiated at runtime (TwitchConfig.configFileExists() is false;
 * TwitchConnection's ctor throws). Members exist only to satisfy the linker;
 * grown as it reports call sites. */
public class TwitchPanel {

    public TwitchPanel(TwitchConnection connection) {
    }

    public void update() {
    }

    public void render(com.badlogic.gdx.graphics.g2d.SpriteBatch sb) {
    }

    public void unhover() {
    }

    public void setPosition(float x, float y) {
    }

    public java.util.Optional<TwitchVoter> getDefaultVoter() {
        return java.util.Optional.empty();
    }
}
