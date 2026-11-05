package com.stsvita.build;

import java.io.File;
import org.teavm.tooling.TeaVMProblemRenderer;
import org.teavm.tooling.TeaVMTargetType;
import org.teavm.tooling.TeaVMTool;
import org.teavm.tooling.TeaVMToolLog;
import org.teavm.vm.TeaVMOptimizationLevel;

/**
 * Drives TeaVM's C backend: user's desktop-1.0.jar (+ our backend and stubs,
 * already on the classpath) -> portable C11 in work/generated_c/.
 *
 * Classpath shadowing does the surgery: stub classes (steamworks4j, discord,
 * twitch, JNA) come first, then the Vita backend, then the user's jar, so the
 * desktop-only code inside the jar is never reachable and gets tree-shaken.
 *
 * Milestone M2. The TeaVM tooling API calls below are written against 0.15.0
 * and will be exercised/corrected when the transpile milestone starts.
 */
public final class BuildVita {

    public static void main(String[] argv) throws Exception {
        if (argv.length != 2 || argv[0].isEmpty()) {
            System.err.println("usage: BuildVita <path/to/desktop-1.0.jar> <out dir>");
            System.exit(2);
        }
        File gameJar = new File(argv[0]);
        File outDir = new File(argv[1]);
        if (!gameJar.isFile()) {
            System.err.println("game jar not found: " + gameJar);
            System.exit(2);
        }
        outDir.mkdirs();

        TeaVMTool tool = new TeaVMTool();
        tool.setTargetType(TeaVMTargetType.C);
        tool.setMainClass("com.stsvita.VitaLauncher");
        tool.setTargetDirectory(outDir);
        tool.setObfuscated(false);
        tool.setOptimizationLevel(TeaVMOptimizationLevel.ADVANCED);
        // malloc commits this entire Java heap up front. The game gets 209 MiB
        // of user memory; at the menu an 80 MiB heap left 80 MiB of it unused,
        // with ~13 MiB of other native use and ~41 MiB of Java in use. 128 MiB
        // still leaves ~29 MiB for native growth plus the OSK's 8 MiB.
        // RSX textures and render targets use the separate graphics memory.
        // Keep min == max to prevent repeated shrink/grow collection cycles.
        tool.setMinHeapSize(128 * 1024 * 1024);
        tool.setMaxHeapSize(128 * 1024 * 1024);
        tool.getTransformers().add(ClasslibShims.class.getName());

        // The game jar rides in via the classloader below; stubs/backend are
        // already on the JVM classpath of this build process.
        java.net.URLClassLoader cl = new java.net.URLClassLoader(
                new java.net.URL[]{gameJar.toURI().toURL()},
                BuildVita.class.getClassLoader());
        tool.setClassLoader(cl);

        tool.generate();
        TeaVMProblemRenderer.describeProblems(
                tool.getDependencyInfo().getCallGraph(), tool.getProblemProvider(), log());
        if (!tool.getProblemProvider().getSevereProblems().isEmpty()) {
            System.err.println("transpile FAILED — unresolved classlib/linkage problems above");
            System.exit(1);
        }
        System.out.println("transpile OK -> " + outDir);
    }

    private static TeaVMToolLog log() {
        return new org.teavm.tooling.ConsoleTeaVMToolLog(true);
    }

    private BuildVita() {}
}
