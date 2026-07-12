package de.robojumper.ststwitch;

/** Referenced only through TwitchPanel.getDefaultVoter(), never instantiated
 * (getDefaultVoter always returns Optional.empty). */
public class TwitchVoter {

    public static void registerListener(TwitchVoteListener listener) {
    }

    public boolean isVotingConnected() {
        return false;
    }

    public boolean initiateSimpleNumberVote(String[] options,
                                            java.util.function.Consumer<Integer> onResult) {
        return false;
    }

    public void endVoting(boolean announce) {
    }

    public TwitchVoteOption[] getOptions() {
        return new TwitchVoteOption[0];
    }

    public int getSecondsRemaining() {
        return 0;
    }
}
