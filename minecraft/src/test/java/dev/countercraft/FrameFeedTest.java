package dev.countercraft;

import com.google.gson.JsonObject;
import org.junit.jupiter.api.Test;
import java.io.*;
import java.net.*;
import java.nio.*;
import java.util.UUID;
import java.util.zip.CRC32;
import static org.junit.jupiter.api.Assertions.*;

class FrameFeedTest {
    @Test void binaryHeaderIsLittleEndianAndCoversPayload() {
        UUID id = new UUID(0x0102030405060708L, 0x1112131415161718L);
        byte[] metadata = "{}".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        byte[] pixels = {1,2,3,4,5,6,7,8};
        ByteBuffer h = FrameFeed.header(id, new FrameFeed.Packet(42, 1, metadata, pixels));
        assertEquals(64, h.remaining());
        byte[] magic = new byte[8]; h.get(magic);
        assertEquals("CCFRM001", new String(magic, java.nio.charset.StandardCharsets.US_ASCII));
        assertEquals(1, h.getInt()); assertEquals(64, h.getInt()); assertEquals(2, h.getInt());
        assertEquals(4, h.getInt()); assertEquals(4, h.getInt()); assertEquals(0, h.getInt());
        assertEquals(id.getMostSignificantBits(), h.getLong()); assertEquals(id.getLeastSignificantBits(), h.getLong());
        assertEquals(42, h.getLong());
        CRC32 crc = new CRC32(); crc.update(metadata); crc.update(pixels);
        assertEquals(crc.getValue(), Integer.toUnsignedLong(h.getInt())); assertEquals(0, h.getInt());
    }

    @Test void poolIsBoundedAndPendingIsLatestOnly() throws Exception {
        try (FrameFeed feed = new FrameFeed()) {
            byte[] a = feed.acquire(8), b = feed.acquire(8);
            assertNotNull(a); assertNotNull(b); assertNull(feed.acquire(8));
            feed.offer(1, System.nanoTime(), new JsonObject(), a);
            feed.offer(2, System.nanoTime(), new JsonObject(), b);
            assertEquals(1, feed.status().get("dropped").getAsInt());
            assertEquals(1, feed.status().get("leasedBuffers").getAsInt());
            byte[] reused = feed.acquire(8);
            assertSame(a, reused); feed.recycle(reused);
            feed.close();
            assertEquals(0, feed.status().get("leasedBuffers").getAsInt());
            assertNull(feed.acquire(8));
        }
    }

    @Test void realLoopbackPublishesCompletePacketAndRecycles() throws Exception {
        try (FrameFeed feed = new FrameFeed(); Socket receiver = new Socket("127.0.0.1", feed.port())) {
            receiver.setSoTimeout(2000);
            await(() -> feed.ready());
            byte[] pixels = feed.acquire(8); pixels[0] = 127;
            feed.offer(5, System.nanoTime(), new JsonObject(), pixels);
            InputStream in = receiver.getInputStream();
            byte[] header = in.readNBytes(64);
            ByteBuffer h = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
            assertEquals(5, h.getLong(48));
            assertArrayEquals("{}".getBytes(), in.readNBytes(h.getInt(16)));
            assertArrayEquals(pixels, in.readNBytes(8));
            await(() -> feed.status().get("sent").getAsInt() == 1);
            await(() -> feed.status().get("leasedBuffers").getAsInt() == 0);
        }
    }

    @Test void backpressureReclaimsPendingLeaseForNewestPixels() throws Exception {
        try (FrameFeed feed = new FrameFeed()) {
            byte[] writing = feed.acquire(8), waiting = feed.acquire(8);
            feed.offer(1, System.nanoTime(), new JsonObject(), waiting);
            byte[] newest = feed.acquire(8);
            assertSame(waiting, newest);
            assertEquals(2, feed.status().get("leasedBuffers").getAsInt());
            assertEquals(1, feed.status().get("dropped").getAsInt());
            feed.offer(2, System.nanoTime(), new JsonObject(), newest);
            feed.recycle(writing);
            feed.close();
            assertEquals(0, feed.status().get("leasedBuffers").getAsInt());
        }
    }

    @Test void staleFramesAreNotPublished() throws Exception {
        try (FrameFeed feed = new FrameFeed(); Socket receiver = new Socket("127.0.0.1", feed.port())) {
            await(() -> feed.ready());
            feed.offer(1, System.nanoTime() - FrameFeed.MAX_AGE_NS - 1, new JsonObject(), feed.acquire(8));
            await(() -> feed.status().get("dropped").getAsInt() == 1);
            assertEquals(0, feed.status().get("sent").getAsInt());
        }
    }

    @Test void stalledReceiverTimesOutWithoutUnboundedBuffers() throws Exception {
        try (FrameFeed feed = new FrameFeed(); Socket receiver = new Socket()) {
            receiver.setReceiveBufferSize(1024);
            receiver.connect(new InetSocketAddress("127.0.0.1", feed.port()));
            await(() -> feed.ready());
            feed.offer(1, System.nanoTime(), new JsonObject(), feed.acquire(8 * 1024 * 1024));
            await(() -> feed.closed());
            assertTrue(feed.status().get("reason").getAsString().contains("500ms"));
            assertEquals(0, feed.status().get("leasedBuffers").getAsInt());
        }
    }

    private static void await(java.util.function.BooleanSupplier condition) throws Exception {
        long deadline = System.nanoTime() + 3_000_000_000L;
        while (!condition.getAsBoolean() && System.nanoTime() < deadline) Thread.sleep(5);
        assertTrue(condition.getAsBoolean(), "Timed out waiting for frame feed");
    }
}
