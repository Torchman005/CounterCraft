package dev.countercraft;

import com.google.gson.JsonObject;
import com.mojang.blaze3d.systems.RenderSystem;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gl.Framebuffer;
import net.minecraft.client.render.Camera;
import net.minecraft.client.util.math.MatrixStack;
import org.lwjgl.opengl.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Comparator;
import java.util.concurrent.atomic.AtomicLong;

/** Three PBOs, zero-timeout fence polls. All GL operations stay on the render thread. */
public final class FrameStream implements HostServer.StreamProvider {
    private static final Logger LOG = LoggerFactory.getLogger("CounterCraft-Stream");
    private volatile Session active;
    private Session rendered;
    private static final class Slot {
        int pbo, bytes;
        long fence, sequence, capturedNanos;
        JsonObject metadata;
        boolean pendingColor;
    }
    private static final class Session {
        final long epoch, interval;
        final int fps;
        final FrameFeed feed;
        final boolean fullClient;
        final Slot[] slots = {new Slot(), new Slot(), new Slot()};
        final AtomicLong issued = new AtomicLong();
        final AtomicLong maxReadbackNs = new AtomicLong(), maxHookNs = new AtomicLong();
        long nextIssue;
        Session(long epoch, int fps, boolean fullClient) throws IOException {
            this.fullClient = fullClient;
            this.epoch = epoch; this.fps = fps; interval = 1_000_000_000L / fps; feed = new FrameFeed();
        }
    }
    @Override public synchronized JsonObject start(long epoch, int fps) {
        return start(epoch, fps, false);
    }
    @Override public synchronized JsonObject start(long epoch, int fps, boolean fullClient) {
        if (fps < 1 || fps > 30) throw new IllegalArgumentException("FPS must be 1..30");
        if (active != null && !active.feed.closed()) throw new IllegalStateException("Stream already running");
        try {
            Session next = new Session(epoch, fps, fullClient);
            JsonObject reply = next.feed.status();
            reply.addProperty("type", "stream-started");
            reply.addProperty("host", "127.0.0.1");
            reply.addProperty("port", next.feed.port());
            reply.addProperty("fps", fps);
            reply.addProperty("epoch", epoch);
            reply.addProperty("fullClient", fullClient);
            active = next;
            return reply;
        } catch (IOException failed) { throw new IllegalStateException("Cannot open frame stream", failed); }
    }
    @Override public JsonObject status() {
        Session s = active;
        JsonObject result = s == null ? new JsonObject() : s.feed.status();
        if (s == null) result.addProperty("running", false);
        else {
            result.addProperty("epoch", s.epoch);
            result.addProperty("fps", s.fps);
            result.addProperty("issued", s.issued.get());
            result.addProperty("maxReadbackMs", s.maxReadbackNs.get() / 1e6);
            result.addProperty("maxHookMs", s.maxHookNs.get() / 1e6);
        }
        return result;
    }
    @Override public synchronized void stop() {
        if (active != null) active.feed.close();
    }
    public void beginFrame(MinecraftClient client) {
        RenderSystem.assertOnRenderThread();
        Session s = active;
        BridgeState.World w = BridgeClient.STATE.world();
        if (s != null && !s.feed.closed() && (!w.offline() || w.epoch() != s.epoch || client.isPaused()
                || !client.isInSingleplayer() || System.nanoTime() - w.time() > BridgeState.TIMEOUT_NS))
            s.feed.fail("World changed, paused or stale");
        if (rendered != s || (rendered != null && rendered.feed.closed())) {
            if (rendered != null) destroy(rendered);
            rendered = s != null && !s.feed.closed() ? s : null;
        }
    }
    public void afterWorld(MinecraftClient client, Camera camera, double fov,
                           MatrixStack matrices, BridgeState.Pose pose) {
        Session s = rendered;
        if (s == null || !s.feed.ready()) return;
        long started = System.nanoTime();
        int previousPack = GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        try {
            Framebuffer framebuffer = client.getFramebuffer();
            int width = framebuffer.viewportWidth, height = framebuffer.viewportHeight;
            if (!framebuffer.useDepthAttachment || width < 1 || height < 1
                    || (long) width * height > FrameFeed.MAX_PIXELS)
                throw new IllegalStateException("Stream requires depth and <=4M pixels");
            int bytes = width * height * 8;
            // Ring slot numbers are not issue order. Preserve sequence when several fences signal together.
            Slot[] ordered = s.slots.clone();
            Arrays.sort(ordered, Comparator.comparingLong(slot -> slot.sequence));
            for (Slot slot : ordered) {
                if (slot.fence == 0) continue;
                int ready = GL32.glClientWaitSync(slot.fence, 0, 0);
                if (ready == GL32.GL_WAIT_FAILED) throw new IllegalStateException("GPU fence failed");
                if (ready != GL32.GL_ALREADY_SIGNALED && ready != GL32.GL_CONDITION_SATISFIED) continue;
                GL32.glDeleteSync(slot.fence); slot.fence = 0;
                long age = System.nanoTime() - slot.capturedNanos;
                byte[] copy = slot.bytes == bytes && age <= FrameFeed.MAX_AGE_NS ? s.feed.acquire(bytes) : null;
                if (copy == null) { s.feed.drop(); continue; }
                boolean offered = false;
                try {
                    GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, slot.pbo);
                    ByteBuffer mapped = GL30.glMapBufferRange(GL21.GL_PIXEL_PACK_BUFFER, 0, bytes, GL30.GL_MAP_READ_BIT);
                    if (mapped == null) throw new IllegalStateException("GPU mapping failed");
                    boolean intact;
                    try { mapped.get(copy); } finally { intact = GL15.glUnmapBuffer(GL21.GL_PIXEL_PACK_BUFFER); }
                    if (!intact) throw new IllegalStateException("GPU mapping corrupted");
                    slot.metadata.addProperty("readbackNanos", System.nanoTime() - slot.capturedNanos);
                    s.maxReadbackNs.accumulateAndGet(System.nanoTime() - slot.capturedNanos, Math::max);
                    s.feed.offer(slot.sequence, slot.capturedNanos, slot.metadata, copy);
                    offered = true;
                } finally { if (!offered) s.feed.recycle(copy); }
            }
            long now = System.nanoTime();
            if (now < s.nextIssue) return;
            s.nextIssue = now + s.interval;
            Slot available = null;
            for (Slot slot : s.slots) if (slot.fence == 0 && !slot.pendingColor) { available = slot; break; }
            if (available == null) { s.feed.drop(); return; }
            issue(available, framebuffer, width, height, s.fullClient);
            available.capturedNanos = now;
            available.sequence = s.issued.incrementAndGet();
            available.metadata = FrameCapture.metadata(client, camera, fov, matrices, pose, s.epoch, width, height, now);
            available.metadata.addProperty("type", "world-stream-frame");
            available.metadata.addProperty("rowOrder", "bottom-to-top");
            available.metadata.addProperty("colorEncoding", "rgba8");
            if (s.fullClient) {
                available.metadata.addProperty("includesHandHud", true);
                available.metadata.addProperty("layer", "client-color-world-depth");
            }
        } catch (RuntimeException failed) {
            LOG.warn("Frame stream stopped", failed);
            s.feed.fail(failed.getMessage());
        } finally {
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, previousPack);
            s.maxHookNs.accumulateAndGet(System.nanoTime() - started, Math::max);
        }
    }
    private static void issue(Slot slot, Framebuffer framebuffer, int width, int height, boolean fullClient) {
        int previousFramebuffer = GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING);
        int[] parameters = {GL11.GL_PACK_ALIGNMENT, GL11.GL_PACK_ROW_LENGTH, GL11.GL_PACK_SKIP_ROWS,
                GL11.GL_PACK_SKIP_PIXELS, GL11.GL_PACK_SWAP_BYTES};
        int[] previous = new int[parameters.length];
        for (int i = 0; i < parameters.length; i++) previous[i] = GL11.glGetInteger(parameters[i]);
        int readBuffer = -1;
        try {
            if (slot.pbo == 0) slot.pbo = GL15.glGenBuffers();
            GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, slot.pbo);
            int bytes = width * height * 8;
            if (slot.bytes != bytes) {
                GL15.glBufferData(GL21.GL_PIXEL_PACK_BUFFER, bytes, GL15.GL_STREAM_READ);
                slot.bytes = bytes;
            }
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, framebuffer.fbo);
            readBuffer = GL11.glGetInteger(GL11.GL_READ_BUFFER);
            GL11.glReadBuffer(GL30.GL_COLOR_ATTACHMENT0);
            for (int i = 0; i < parameters.length; i++) GL11.glPixelStorei(parameters[i], i == 0 ? 1 : 0);
            if (!fullClient) GL11.glReadPixels(0, 0, width, height, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, 0L);
            GL11.glReadPixels(0, 0, width, height, GL11.GL_DEPTH_COMPONENT, GL11.GL_FLOAT, (long) width * height * 4);
            if (fullClient) slot.pendingColor = true;
            else {
                slot.fence = GL32.glFenceSync(GL32.GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                if (slot.fence == 0) throw new IllegalStateException("Cannot create GPU fence");
                GL11.glFlush();
            }
        } finally {
            if (readBuffer != -1) GL11.glReadBuffer(readBuffer);
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, previousFramebuffer);
            for (int i = 0; i < parameters.length; i++) GL11.glPixelStorei(parameters[i], previous[i]);
        }
    }
    /** Final client color includes hand/HUD/GUI; depth remains the earlier world pass. */
    public void afterClient(MinecraftClient client) {
        Session s = rendered;
        if (s == null || !s.fullClient) return;
        RenderSystem.assertOnRenderThread();
        int pack = GL11.glGetInteger(GL21.GL_PIXEL_PACK_BUFFER_BINDING);
        int read = GL11.glGetInteger(GL30.GL_READ_FRAMEBUFFER_BINDING);
        int alignment = GL11.glGetInteger(GL11.GL_PACK_ALIGNMENT), rows = GL11.glGetInteger(GL11.GL_PACK_ROW_LENGTH);
        int skipRows = GL11.glGetInteger(GL11.GL_PACK_SKIP_ROWS), skipPixels = GL11.glGetInteger(GL11.GL_PACK_SKIP_PIXELS);
        int readBuffer = -1;
        try {
            var fb = client.getFramebuffer();
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER, fb.fbo);
            readBuffer = GL11.glGetInteger(GL11.GL_READ_BUFFER); GL11.glReadBuffer(GL30.GL_COLOR_ATTACHMENT0);
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,1); GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,0);
            GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,0); GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,0);
            for (Slot slot : s.slots) if (slot.pendingColor) {
                slot.pendingColor = false;
                int w = slot.metadata.get("width").getAsInt(), h = slot.metadata.get("height").getAsInt();
                if (w != fb.viewportWidth || h != fb.viewportHeight) { s.feed.drop(); continue; }
                GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER, slot.pbo);
                GL11.glReadPixels(0,0,w,h,GL11.GL_RGBA,GL11.GL_UNSIGNED_BYTE,0L);
                slot.metadata.addProperty("guiOpen", client.currentScreen != null);
                slot.fence = GL32.glFenceSync(GL32.GL_SYNC_GPU_COMMANDS_COMPLETE,0);
                if (slot.fence == 0) throw new IllegalStateException("Cannot create client frame fence");
            }
            GL11.glFlush();
        } catch (RuntimeException failed) { s.feed.fail(failed.getMessage()); }
        finally {
            if (readBuffer != -1) GL11.glReadBuffer(readBuffer);
            GL30.glBindFramebuffer(GL30.GL_READ_FRAMEBUFFER,read); GL15.glBindBuffer(GL21.GL_PIXEL_PACK_BUFFER,pack);
            GL11.glPixelStorei(GL11.GL_PACK_ALIGNMENT,alignment); GL11.glPixelStorei(GL11.GL_PACK_ROW_LENGTH,rows);
            GL11.glPixelStorei(GL11.GL_PACK_SKIP_ROWS,skipRows); GL11.glPixelStorei(GL11.GL_PACK_SKIP_PIXELS,skipPixels);
        }
    }
    private static void destroy(Session s) {
        for (Slot slot : s.slots) {
            if (slot.fence != 0) { GL32.glDeleteSync(slot.fence); slot.fence = 0; }
            if (slot.pbo != 0) { GL15.glDeleteBuffers(slot.pbo); slot.pbo = 0; }
            slot.metadata = null;
        }
    }
    public void closeRender() {
        RenderSystem.assertOnRenderThread();
        stop();
        if (rendered != null) { destroy(rendered); rendered = null; }
    }
}
