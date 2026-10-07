package dev.countercraft;

import com.google.gson.*;
import com.mojang.blaze3d.systems.RenderSystem;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.Framebuffer;
import net.minecraft.client.render.Camera;
import net.minecraft.client.util.math.MatrixStack;
import net.minecraft.util.math.Vec3d;
import org.lwjgl.opengl.*;
import org.lwjgl.system.MemoryUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.*;
import java.nio.file.*;
import java.util.UUID;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

/** On-demand diagnostic readback, not a realtime shared-texture transport. */
public final class FrameCapture implements AutoCloseable {
    private static final Logger LOG = LoggerFactory.getLogger("CounterCraft-Capture");
    private static final int MAX_PIXELS = 4_194_304;
    private final Path root;
    private final AtomicReference<Job> pending = new AtomicReference<>();
    private final ExecutorService writer = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "CounterCraft-CaptureWriter");
        thread.setDaemon(true);
        return thread;
    });
    private volatile boolean closed;

    private static final class Job {
        final long epoch;
        final AtomicBoolean claimed = new AtomicBoolean();
        final CompletableFuture<JsonObject> result = new CompletableFuture<>();
        Job(long epoch) { this.epoch = epoch; }
    }

    public FrameCapture(Path root) { this.root = root.toAbsolutePath().normalize(); }

    public CompletableFuture<JsonObject> request(long epoch) {
        if (closed) throw new IllegalStateException("Capture service closed");
        Job job = new Job(epoch);
        if (!pending.compareAndSet(null, job)) throw new IllegalStateException("A capture is already pending");
        job.result.whenComplete((reply, failure) -> {
            if (!job.claimed.get()) pending.compareAndSet(job, null);
        });
        return job.result;
    }

    public void afterWorld(MinecraftClient client, Camera camera, double fov,
                           MatrixStack matrices, BridgeState.Pose pose) {
        Job job = pending.get();
        if (job == null || !job.claimed.compareAndSet(false, true)) return;
        if (job.result.isDone()) { pending.compareAndSet(job, null); return; }
        try {
            RenderSystem.assertOnRenderThread();
            BridgeState.World world = BridgeClient.STATE.world();
            if (!client.isInSingleplayer() || client.isPaused() || !world.offline()
                    || world.epoch() != job.epoch || System.nanoTime() - world.time() > BridgeState.TIMEOUT_NS)
                throw new IllegalStateException("Capture world changed or paused");

            Framebuffer framebuffer = client.getFramebuffer();
            int width = framebuffer.viewportWidth, height = framebuffer.viewportHeight;
            if (!framebuffer.useDepthAttachment || width < 1 || height < 1 || (long) width * height > MAX_PIXELS)
                throw new IllegalStateException("Capture requires depth and a framebuffer of at most 4M pixels");
            int pixels = width * height;
            byte[] rgba = new byte[pixels * 4];
            float[] depth = new float[pixels];
            readPixels(framebuffer, width, height, rgba, depth);

            JsonObject metadata = metadata(client, camera, fov, matrices, pose, world.epoch(), width, height, System.nanoTime());
            writer.execute(() -> save(job, width, height, rgba, depth, metadata));
        } catch (RuntimeException failed) {
            LOG.warn("World readback failed", failed);
            job.result.completeExceptionally(failed);
            pending.compareAndSet(job, null);
        }
    }

    static JsonObject metadata(MinecraftClient client, Camera camera, double fov, MatrixStack matrices,
                               BridgeState.Pose pose, long epoch, int width, int height, long capturedNanos) {
            JsonObject metadata = new JsonObject();
            metadata.addProperty("v", 1);
            metadata.addProperty("type", "world-frame");
            metadata.addProperty("epoch", epoch);
            metadata.addProperty("requestedFrame", pose == null ? -1 : pose.frame());
            metadata.addProperty("width", width);
            metadata.addProperty("height", height);
            metadata.addProperty("rowOrder", "top-to-bottom");
            metadata.addProperty("depthEncoding", "float32-le");
            metadata.addProperty("depthSpace", "opengl-window-z");
            metadata.addProperty("reversedZ", false);
            metadata.addProperty("includesHandHud", false);
            metadata.addProperty("includesSkyFog", true);
            metadata.addProperty("near", 0.05);
            metadata.addProperty("far", client.gameRenderer.getFarPlaneDistance());
            metadata.addProperty("monotonicNanos", capturedNanos);
            Vec3d position = camera.getPos();
            JsonObject actual = new JsonObject();
            actual.add("position", array(position.x, position.y, position.z));
            actual.add("rotation", array(camera.getYaw(), camera.getPitch(), 0));
            actual.addProperty("fov", fov);
            metadata.add("camera", actual);
            metadata.add("projectionColumnMajor", array(RenderSystem.getProjectionMatrix().get(new float[16])));
            metadata.add("viewRotationColumnMajor", array(matrices.peek().getPositionMatrix().get(new float[16])));
            return metadata;
    }

    private static JsonArray array(double... values) {
        JsonArray array = new JsonArray();
        for (double value : values) array.add(value);
        return array;
    }
    private static JsonArray array(float[] values) {
        JsonArray array = new JsonArray();
        for (float value : values) array.add(value);
        return array;
    }

    private static void readPixels(Framebuffer framebuffer, int width, int height, byte[] rgba, float[] depth) {
        int previousFramebuffer = GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING);
        int previousPackBuffer = GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        int[] parameters = {GL11.GL_PACK_ALIGNMENT, GL11.GL_PACK_ROW_LENGTH, GL11.GL_PACK_SKIP_ROWS,
                GL11.GL_PACK_SKIP_PIXELS, GL11.GL_PACK_SWAP_BYTES};
        int[] previous = new int[parameters.length];
        for (int i = 0; i < parameters.length; i++) previous[i] = GL11.glGetInteger(parameters[i]);
        ByteBuffer colorBuffer = MemoryUtil.memAlloc(rgba.length);
        FloatBuffer depthBuffer = null;
        int readBuffer = -1;
        try {
            depthBuffer = MemoryUtil.memAllocFloat(depth.length);
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, framebuffer.fbo);
            readBuffer = GL11.glGetInteger(GL11.GL_READ_BUFFER);
            GL11.glReadBuffer(GL30.GL_COLOR_ATTACHMENT0);
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, 0);
            for (int i = 0; i < parameters.length; i++) GL11.glPixelStorei(parameters[i], i == 0 ? 1 : 0);
            GL11.glReadPixels(0, 0, width, height, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, colorBuffer);
            GL11.glReadPixels(0, 0, width, height, GL11.GL_DEPTH_COMPONENT, GL11.GL_FLOAT, depthBuffer);
            colorBuffer.get(rgba);
            depthBuffer.get(depth);
        } finally {
            if (readBuffer != -1) GL11.glReadBuffer(readBuffer);
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, previousFramebuffer);
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, previousPackBuffer);
            for (int i = 0; i < parameters.length; i++) GL11.glPixelStorei(parameters[i], previous[i]);
            MemoryUtil.memFree(colorBuffer);
            if (depthBuffer != null) MemoryUtil.memFree(depthBuffer);
        }
    }

    private void save(Job job, int width, int height, byte[] rgba, float[] depth, JsonObject metadata) {
        try {
            if (job.result.isDone()) return;
            String id = UUID.randomUUID().toString();
            Path directory = root.resolve(id);
            Files.createDirectories(directory);
            BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
            ByteBuffer depthBytes = ByteBuffer.allocate(depth.length * 4).order(ByteOrder.LITTLE_ENDIAN);
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int source = (height - 1 - y) * width + x;
                    int i = source * 4;
                    image.setRGB(x, y, ((rgba[i + 3] & 255) << 24) | ((rgba[i] & 255) << 16)
                            | ((rgba[i + 1] & 255) << 8) | (rgba[i + 2] & 255));
                    depthBytes.putFloat(depth[source]);
                }
            }
            Path colorPath = directory.resolve("color.png");
            Path depthPath = directory.resolve("depth.f32");
            Path manifest = directory.resolve("frame.json");
            if (!ImageIO.write(image, "png", colorPath.toFile())) throw new IOException("PNG writer unavailable");
            Files.write(depthPath, depthBytes.array(), StandardOpenOption.CREATE_NEW);
            metadata.addProperty("captureId", id);
            metadata.addProperty("colorFile", "color.png");
            metadata.addProperty("depthFile", "depth.f32");
            // Publish the manifest by rename after colour/depth are complete.
            Path temporary = directory.resolve("frame.json.part");
            Files.writeString(temporary, new GsonBuilder().setPrettyPrinting().create().toJson(metadata), StandardOpenOption.CREATE_NEW);
            try {
                Files.move(temporary, manifest, StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException unsupported) {
                Files.move(temporary, manifest);
            }
            JsonObject reply = new JsonObject();
            reply.addProperty("type", "captured");
            reply.addProperty("manifest", manifest.toString());
            job.result.complete(reply);
        } catch (IOException | RuntimeException failed) {
            LOG.warn("Capture bundle write failed", failed);
            job.result.completeExceptionally(failed);
        } finally {
            pending.compareAndSet(job, null);
        }
    }

    @Override public void close() {
        closed = true;
        Job job = pending.getAndSet(null);
        if (job != null) job.result.cancel(false);
        writer.shutdownNow();
    }
}
