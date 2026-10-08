package dev.countercraft;

import com.google.gson.*;
import org.junit.jupiter.api.Test;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicReference;
import static org.junit.jupiter.api.Assertions.*;

class HostServerTest {
    @Test void watchdogAndWorldChangeReleaseControl() {
        BridgeState state = new BridgeState();
        long now = System.nanoTime();
        state.update(new BridgeState.World(1, true, 0, 64, 0, now));
        state.accept(1, 0, 64, 0, 90, 10, 70, now);
        assertNotNull(state.live(now));
        assertNull(state.live(now + BridgeState.TIMEOUT_NS + 1));
        state.update(new BridgeState.World(2, true, 0, 64, 0, now));
        assertNull(state.live(now));
        state.update(new BridgeState.World(2, false, 0, 64, 0, now));
        assertThrows(IllegalArgumentException.class, () -> state.accept(2, 0, 64, 0, 0, 0, 70, now));
    }

    @Test void rejectNonFiniteAndDistantCamera() {
        BridgeState state = new BridgeState();
        long now = System.nanoTime();
        state.update(new BridgeState.World(1, true, 0, 64, 0, now));
        assertThrows(IllegalArgumentException.class, () -> state.accept(1, Double.NaN, 64, 0, 0, 0, 70, now));
        assertThrows(IllegalArgumentException.class, () -> state.accept(1, 65, 64, 0, 0, 0, 70, now));
        assertThrows(IllegalArgumentException.class, () -> state.accept(1, 0, 64, 0, 0, 0, 180, now));
    }

    @Test void fragmentedHandshakeCameraAndRelease() throws Exception {
        BridgeState state = new BridgeState();
        try (HostServer server = new HostServer(state, 0);
             Socket client = new Socket("127.0.0.1", server.port())) {
            client.setSoTimeout(3000);
            BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream(), StandardCharsets.UTF_8));
            OutputStream out = client.getOutputStream();
            out.write("{\"v\":1,".getBytes(StandardCharsets.UTF_8));
            out.flush();
            out.write("\"type\":\"hello\",\"role\":\"test\"}\n".getBytes(StandardCharsets.UTF_8));
            out.flush();
            assertEquals("ready", reply(reader).get("type").getAsString());
            state.update(new BridgeState.World(1, true, 0, 64, 0, System.nanoTime()));
            send(out, "{\"v\":1,\"type\":\"camera\",\"frame\":1,\"position\":[1,64,0],\"rotation\":[0,0,0],\"fov\":70}");
            assertEquals("ack", reply(reader).get("type").getAsString());
            assertNotNull(state.live(System.nanoTime()));
            send(out, "{\"v\":1,\"type\":\"release\"}");
            assertEquals("released", reply(reader).get("type").getAsString());
            assertNull(state.live(System.nanoTime()));
            send(out, "{\"v\":1,\"type\":\"camera\",\"frame\":1,\"position\":[1,64,0],\"rotation\":[0,0,0],\"fov\":70}");
            assertEquals("error", reply(reader).get("type").getAsString());
        }
    }

    @Test void invalidClientDoesNotPreventReconnect() throws Exception {
        try (HostServer server = new HostServer(new BridgeState(), 0)) {
            try (Socket client = new Socket("127.0.0.1", server.port())) {
                client.setSoTimeout(3000);
                send(client.getOutputStream(), "[]");
                assertEquals("error", reply(new BufferedReader(new InputStreamReader(client.getInputStream()))).get("type").getAsString());
            }
            try (Socket client = new Socket("127.0.0.1", server.port())) {
                client.setSoTimeout(3000);
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"hello\",\"role\":\"cs2\"}");
                assertEquals("ready", reply(new BufferedReader(new InputStreamReader(client.getInputStream()))).get("type").getAsString());
            }
        }
    }

    @Test void boundsAndTruncatedInput() {
        assertThrows(IOException.class, () -> HostServer.readLine(new ByteArrayInputStream(new byte[65536])));
        assertThrows(IOException.class, () -> HostServer.readLine(new ByteArrayInputStream("{}".getBytes())));
    }

    @Test void captureReturnsOnlyAfterProviderCompletes() throws Exception {
        BridgeState state = new BridgeState();
        CompletableFuture<JsonObject> completedFrame = new CompletableFuture<>();
        CountDownLatch requested = new CountDownLatch(1);
        AtomicReference<Thread> caller = new AtomicReference<>();
        try (HostServer server = new HostServer(state, 0, epoch -> {
            assertEquals(42, epoch);
            caller.set(Thread.currentThread());
            requested.countDown();
            return completedFrame;
        }); Socket client = new Socket("127.0.0.1", server.port())) {
            client.setSoTimeout(3000);
            BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream()));
            send(client.getOutputStream(), "{\"v\":1,\"type\":\"hello\",\"role\":\"test\"}");
            assertTrue(reply(reader).get("capture").getAsBoolean());
            state.update(new BridgeState.World(42, true, 0, 64, 0, System.nanoTime()));
            send(client.getOutputStream(), "{\"v\":1,\"type\":\"capture\"}");
            assertTrue(requested.await(1, TimeUnit.SECONDS));
            assertNotEquals(Thread.currentThread(), caller.get());
            assertFalse(completedFrame.isDone());
            JsonObject frame = new JsonObject();
            frame.addProperty("type", "captured");
            frame.addProperty("manifest", "lab/frame.json");
            completedFrame.complete(frame);
            JsonObject result = reply(reader);
            assertEquals("captured", result.get("type").getAsString());
            assertEquals("lab/frame.json", result.get("manifest").getAsString());
            assertEquals(1, result.get("v").getAsInt());
        }
    }

    @Test void captureCannotReadFromMenuOrStaleWorld() throws Exception {
        for (BridgeState.World world : new BridgeState.World[] {
                new BridgeState.World(1, false, 0, 64, 0, System.nanoTime()),
                new BridgeState.World(1, true, 0, 64, 0, System.nanoTime() - BridgeState.TIMEOUT_NS * 2)}) {
            BridgeState state = new BridgeState();
            state.update(world);
            try (HostServer server = new HostServer(state, 0, epoch -> {
                fail("Unavailable world must not be captured");
                return new CompletableFuture<>();
            }); Socket client = new Socket("127.0.0.1", server.port())) {
                client.setSoTimeout(3000);
                BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream()));
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"hello\",\"role\":\"test\"}");
                assertEquals("ready", reply(reader).get("type").getAsString());
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"capture\"}");
                assertEquals("error", reply(reader).get("type").getAsString());
                assertNull(reader.readLine());
            }
        }
    }

    @Test void captureTimeoutCancelsRequestAndAllowsReconnect() throws Exception {
        BridgeState state = new BridgeState();
        CompletableFuture<JsonObject> neverRendered = new CompletableFuture<>();
        try (HostServer server = new HostServer(state, 0, epoch -> neverRendered)) {
            try (Socket client = new Socket("127.0.0.1", server.port())) {
                client.setSoTimeout(5000);
                BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream()));
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"hello\",\"role\":\"test\"}");
                assertEquals("ready", reply(reader).get("type").getAsString());
                state.update(new BridgeState.World(1, true, 0, 64, 0, System.nanoTime()));
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"capture\"}");
                assertEquals("error", reply(reader).get("type").getAsString());
                assertTrue(neverRendered.isCancelled());
                assertNull(reader.readLine());
            }
            try (Socket client = new Socket("127.0.0.1", server.port())) {
                client.setSoTimeout(3000);
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"hello\",\"role\":\"test\"}");
                assertEquals("ready", reply(new BufferedReader(new InputStreamReader(client.getInputStream()))).get("type").getAsString());
            }
        }
    }

    private static JsonObject reply(BufferedReader reader) throws IOException {
        return JsonParser.parseString(reader.readLine()).getAsJsonObject();
    }

    @Test void streamControlsAndDisconnectBelongToHostSession() throws Exception {
        BridgeState state = new BridgeState();
        java.util.concurrent.atomic.AtomicInteger stopped = new java.util.concurrent.atomic.AtomicInteger();
        HostServer.StreamProvider provider = new HostServer.StreamProvider() {
            public JsonObject start(long epoch, int fps) {
                assertEquals(7, epoch); assertEquals(20, fps);
                JsonObject reply = new JsonObject(); reply.addProperty("type", "stream-started"); return reply;
            }
            public JsonObject status() { JsonObject result = new JsonObject(); result.addProperty("running", false); return result; }
            public void stop() { stopped.incrementAndGet(); }
        };
        try (HostServer server = new HostServer(state, 0, null, provider)) {
            try (Socket client = new Socket("127.0.0.1", server.port())) {
                client.setSoTimeout(3000);
                BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream()));
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"hello\",\"role\":\"test\"}");
                assertTrue(reply(reader).get("stream").getAsBoolean());
                state.update(new BridgeState.World(7, true, 0, 64, 0, System.nanoTime()));
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"stream-start\",\"fps\":20}");
                assertEquals("stream-started", reply(reader).get("type").getAsString());
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"status\"}");
                JsonObject status = reply(reader);
                assertTrue(status.has("serverMonotonicNanos"));
                assertFalse(status.getAsJsonObject("stream").get("running").getAsBoolean());
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"stream-stop\"}");
                assertEquals("stream-stopped", reply(reader).get("type").getAsString());
                assertEquals(1, stopped.get());
            }
            long deadline = System.nanoTime() + 1_000_000_000;
            while (stopped.get() < 2 && System.nanoTime() < deadline) Thread.sleep(5);
            assertEquals(2, stopped.get());
        }
    }

    @Test void streamRejectsMenuStaleWorldAndInvalidRate() throws Exception {
        for (int scenario = 0; scenario < 4; scenario++) {
            BridgeState state = new BridgeState();
            state.update(new BridgeState.World(1, scenario != 0, 0, 64, 0,
                    System.nanoTime() - (scenario == 1 ? BridgeState.TIMEOUT_NS * 2 : 0)));
            HostServer.StreamProvider provider = new HostServer.StreamProvider() {
                public JsonObject start(long epoch, int fps) { fail("Invalid stream must not start"); return null; }
                public JsonObject status() { return new JsonObject(); }
                public void stop() { }
            };
            try (HostServer server = new HostServer(state, 0, null, provider);
                 Socket client = new Socket("127.0.0.1", server.port())) {
                client.setSoTimeout(2000);
                BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream()));
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"hello\",\"role\":\"test\"}");
                assertEquals("ready", reply(reader).get("type").getAsString());
                int fps = scenario == 2 ? 0 : scenario == 3 ? 31 : 20;
                send(client.getOutputStream(), "{\"v\":1,\"type\":\"stream-start\",\"fps\":" + fps + "}");
                assertEquals("error", reply(reader).get("type").getAsString());
                assertNull(reader.readLine());
            }
        }
    }
    private static void send(OutputStream out, String value) throws IOException {
        out.write((value + "\n").getBytes(StandardCharsets.UTF_8));
        out.flush();
    }

    @Test void actionAcknowledgesExecutionAndReportsFailureReason() throws Exception {
        BridgeState state = new BridgeState(); ActionQueue queue = new ActionQueue();
        try (HostServer server = new HostServer(state, 0, null, null, queue);
             Socket client = new Socket("127.0.0.1", server.port())) {
            client.setSoTimeout(2000);
            BufferedReader reader = new BufferedReader(new InputStreamReader(client.getInputStream()));
            send(client.getOutputStream(), "{\"v\":1,\"type\":\"hello\",\"role\":\"test\"}");
            assertTrue(reply(reader).get("actions").getAsBoolean());
            state.update(new BridgeState.World(7,true,0,64,0,System.nanoTime()));
            send(client.getOutputStream(), "{\"v\":1,\"type\":\"action\",\"id\":1,\"epoch\":7,\"action\":\"look\",\"yaw\":0,\"pitch\":0}");
            AtomicReference<Thread> executor = new AtomicReference<>();
            long deadline = System.nanoTime()+500_000_000;
            while (executor.get()==null && System.nanoTime()<deadline) {
                queue.tick(state.world(),System.nanoTime(),a -> { executor.set(Thread.currentThread()); return new JsonObject(); });
                Thread.sleep(2);
            }
            assertSame(Thread.currentThread(), executor.get());
            assertEquals("action-ack", reply(reader).get("type").getAsString());
            send(client.getOutputStream(), "{\"v\":1,\"type\":\"action\",\"id\":2,\"epoch\":6,\"action\":\"inventory\"}");
            var error = reply(reader);
            assertEquals("error", error.get("type").getAsString());
            assertTrue(error.get("message").getAsString().contains("epoch"));
            assertNull(reader.readLine());
        }
    }
    @Test void inputLifecycleBelongsToControlConnection() throws Exception {
        var state = new BridgeState(); var controls = new RemoteControl();
        try (HostServer server = new HostServer(state,0,null,null,null,controls)) {
            try (Socket client = new Socket("127.0.0.1",server.port())) {
                client.setSoTimeout(2000);
                var reader = new BufferedReader(new InputStreamReader(client.getInputStream()));
                send(client.getOutputStream(),"{\"v\":1,\"type\":\"hello\",\"role\":\"cs2\"}");
                assertTrue(reply(reader).get("input").getAsBoolean());
                state.update(new BridgeState.World(7,true,0,64,0,System.nanoTime()));
                send(client.getOutputStream(),"{\"v\":1,\"type\":\"input\",\"id\":1,\"epoch\":7,\"yaw\":0,\"pitch\":0,\"forward\":1,\"sideways\":0,\"slot\":0}");
                assertEquals("input-ack",reply(reader).get("type").getAsString());
                assertNotNull(controls.live(state.world(),System.nanoTime()));
                send(client.getOutputStream(),"{\"v\":1,\"type\":\"release\"}");
                assertEquals("released",reply(reader).get("type").getAsString());
                assertNull(controls.live(state.world(),System.nanoTime()));
            }
        }
    }
}
