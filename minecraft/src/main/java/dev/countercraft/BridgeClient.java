package dev.countercraft;

import net.fabricmc.api.ClientModInitializer;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.util.math.Vec3d;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import net.minecraft.client.render.Camera;
import net.minecraft.client.util.math.MatrixStack;

public final class BridgeClient implements ClientModInitializer {
    public static final BridgeState STATE = new BridgeState();
    private static final Logger LOG = LoggerFactory.getLogger("CounterCraft");
    private static ClientWorld previous;
    private static long epoch;
    private static boolean enabled;
    private static volatile BridgeState.Pose framePose;
    private static FrameCapture captures;
    private static FrameStream frames;

    @Override public void onInitializeClient() {
        if (!Boolean.getBoolean("countercraft.enabled")) {
            LOG.info("Camera lab disabled. Opt in with -Dcountercraft.enabled=true");
            return;
        }
        try {
            captures = new FrameCapture(clientCaptureRoot());
            frames = new FrameStream();
            HostServer server = new HostServer(STATE, Integer.getInteger("countercraft.port", 37122), captures::request, frames);
            enabled = true;
            Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                try { server.close(); } catch (Exception ignored) { }
                captures.close();
                frames.stop();
            }, "CounterCraft-Shutdown"));
            LOG.info("Camera lab listening on 127.0.0.1:{}; single-player only", server.port());
        } catch (Exception e) {
            if (captures != null) captures.close();
            LOG.error("Cannot start camera lab; vanilla camera retained", e);
        }
    }

    public static void tick(MinecraftClient client) {
        if (!enabled) return;
        if (previous != client.world) { previous = client.world; epoch++; STATE.release(); }
        boolean offline = client.world != null && client.player != null
                && client.isInSingleplayer() && !client.isPaused();
        Vec3d pos = client.player == null ? Vec3d.ZERO : client.player.getEyePos();
        STATE.update(new BridgeState.World(epoch, offline, pos.x, pos.y, pos.z, System.nanoTime()));
        if (!offline) STATE.release();
    }

    public static void beginFrame() {
        MinecraftClient client = MinecraftClient.getInstance();
        if (enabled) frames.beginFrame(client);
        framePose = enabled && client.world != null && client.world == previous
                && client.isInSingleplayer() && !client.isPaused() ? STATE.live(System.nanoTime()) : null;
    }
    public static BridgeState.Pose framePose() { return framePose; }

    public static boolean keepRunningUnfocused() {
        MinecraftClient client = MinecraftClient.getInstance();
        return enabled && Boolean.getBoolean("countercraft.background") && client.isInSingleplayer();
    }

    private static java.nio.file.Path clientCaptureRoot() {
        return MinecraftClient.getInstance().runDirectory.toPath().resolve("countercraft/captures");
    }

    public static void captureWorld(Camera camera, double fov, MatrixStack matrices) {
        if (enabled) {
            frames.afterWorld(MinecraftClient.getInstance(), camera, fov, matrices, framePose);
            captures.afterWorld(MinecraftClient.getInstance(), camera, fov, matrices, framePose);
        }
    }
    public static void closeRender() {
        if (enabled) frames.closeRender();
    }
}
