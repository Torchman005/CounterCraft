package dev.countercraft;

import com.google.gson.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;

/** One local host at a time. Bounded JSON Lines with a reconnectable session. */
public final class HostServer implements AutoCloseable {
    @FunctionalInterface public interface CaptureProvider {
        CompletableFuture<JsonObject> request(long epoch);
    }
    private final BridgeState state;
    private final CaptureProvider capture;
    private final ServerSocket listener;
    private volatile Socket active;
    private final Thread worker;

    public HostServer(BridgeState state, int port) throws IOException {
        this(state, port, null);
    }

    public HostServer(BridgeState state, int port, CaptureProvider capture) throws IOException {
        this.state = state;
        this.capture = capture;
        listener = new ServerSocket();
        listener.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), port), 1);
        worker = new Thread(this::run, "CounterCraft-Host");
        worker.setDaemon(true);
        worker.start();
    }
    public int port() { return listener.getLocalPort(); }

    private void run() {
        while (!listener.isClosed()) {
            try (Socket socket = listener.accept()) {
                active = socket;
                socket.setSoTimeout(2000);
                socket.setTcpNoDelay(true);
                serve(socket);
            } catch (IOException ignored) {
                // A host can disconnect at any time; a later connection starts fresh.
            } finally {
                state.release();
                active = null;
            }
        }
    }

    private void serve(Socket socket) throws IOException {
        InputStream in = new BufferedInputStream(socket.getInputStream());
        OutputStream out = socket.getOutputStream();
        boolean greeted = false;
        long last = -1;
        while (!listener.isClosed()) {
            String line = readLine(in);
            if (line == null) return;
            try {
                JsonObject m = JsonParser.parseString(line).getAsJsonObject();
                if (integer(m, "v") != 1) throw new IllegalArgumentException("Unsupported protocol");
                String type = m.get("type").getAsString();
                JsonObject reply = new JsonObject();
                if (!greeted) {
                    if (!type.equals("hello") || !(m.get("role").getAsString().equals("cs2")
                            || m.get("role").getAsString().equals("test")))
                        throw new IllegalArgumentException("First message must be hello from cs2 or test");
                    greeted = true;
                    reply.addProperty("type", "ready");
                    reply.addProperty("mode", "minecraft-camera-lab");
                    reply.addProperty("renderer", false);
                    reply.addProperty("capture", capture != null);
                } else if (type.equals("status") || type.equals("ping")) {
                    BridgeState.World w = state.world();
                    reply.addProperty("type", "status");
                    reply.addProperty("offline", w.offline() && System.nanoTime() - w.time() <= BridgeState.TIMEOUT_NS);
                    reply.addProperty("active", state.live(System.nanoTime()) != null);
                    reply.addProperty("epoch", w.epoch());
                    JsonArray pos = new JsonArray();
                    pos.add(w.x()); pos.add(w.y()); pos.add(w.z());
                    reply.add("position", pos);
                } else if (type.equals("camera")) {
                    long frame = integer(m, "frame");
                    if (frame <= last) throw new IllegalArgumentException("Frame must increase");
                    JsonArray p = vector(m, "position"), r = vector(m, "rotation");
                    if (number(r.get(2)) != 0) throw new IllegalArgumentException("Roll is not implemented");
                    state.accept(frame, number(p.get(0)), number(p.get(1)), number(p.get(2)),
                            (float) number(r.get(0)), (float) number(r.get(1)),
                            (float) number(m.get("fov")), System.nanoTime());
                    last = frame;
                    reply.addProperty("type", "ack");
                    reply.addProperty("frame", frame);
                } else if (type.equals("release")) {
                    state.release();
                    reply.addProperty("type", "released");
                } else if (type.equals("capture")) {
                    BridgeState.World w = state.world();
                    if (capture == null || !w.offline()
                            || System.nanoTime() - w.time() > BridgeState.TIMEOUT_NS)
                        throw new IllegalArgumentException("Capture requires a fresh single-player world");
                    CompletableFuture<JsonObject> pending = capture.request(w.epoch());
                    try {
                        reply = pending.get(3, TimeUnit.SECONDS);
                    } catch (InterruptedException interrupted) {
                        pending.cancel(false);
                        Thread.currentThread().interrupt();
                        throw new IllegalStateException("Capture interrupted", interrupted);
                    } catch (ExecutionException | TimeoutException failed) {
                        pending.cancel(false);
                        throw new IllegalStateException("Capture unavailable", failed);
                    }
                } else throw new IllegalArgumentException("Unsupported message type");
                send(out, reply);
            } catch (RuntimeException invalid) {
                JsonObject error = new JsonObject();
                error.addProperty("type", "error");
                error.addProperty("message", "Invalid or unavailable request: " + invalid.getClass().getSimpleName());
                send(out, error);
                return;
            }
        }
    }

    private static JsonArray vector(JsonObject m, String key) {
        JsonArray a = m.getAsJsonArray(key);
        if (a == null || a.size() != 3) throw new IllegalArgumentException("Expected three components");
        return a;
    }
    private static double number(JsonElement e) {
        if (e == null || !e.isJsonPrimitive() || !e.getAsJsonPrimitive().isNumber())
            throw new IllegalArgumentException("Expected number");
        double n = e.getAsDouble();
        if (!Double.isFinite(n)) throw new IllegalArgumentException("Expected finite number");
        return n;
    }
    private static long integer(JsonObject m, String key) {
        JsonElement value = m.get(key);
        number(value);
        return value.getAsBigDecimal().longValueExact();
    }
    private static void send(OutputStream out, JsonObject m) throws IOException {
        m.addProperty("v", 1);
        out.write((m + "\n").getBytes(StandardCharsets.UTF_8));
        out.flush();
    }
    static String readLine(InputStream in) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        for (int count = 0; count < 65536; count++) {
            int next = in.read();
            if (next < 0) {
                if (bytes.size() == 0) return null;
                throw new IOException("Incomplete frame");
            }
            if (next == '\n') return bytes.toString(StandardCharsets.UTF_8);
            bytes.write(next);
        }
        throw new IOException("Frame exceeds 64 KiB");
    }
    @Override public void close() throws IOException {
        listener.close();
        Socket socket = active;
        if (socket != null) socket.close();
        state.release();
    }
}
