package dev.countercraft;

import com.google.gson.*;
import org.junit.jupiter.api.Test;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
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

    private static JsonObject reply(BufferedReader reader) throws IOException {
        return JsonParser.parseString(reader.readLine()).getAsJsonObject();
    }
    private static void send(OutputStream out, String value) throws IOException {
        out.write((value + "\n").getBytes(StandardCharsets.UTF_8));
        out.flush();
    }
}
