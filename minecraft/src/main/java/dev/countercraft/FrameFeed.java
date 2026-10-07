package dev.countercraft;

import com.google.gson.JsonObject;
import java.io.IOException;
import java.net.*;
import java.nio.*;
import java.nio.channels.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.zip.CRC32;

/** CPU-copy loopback transport. Two leased buffers and one replaceable pending frame. */
public final class FrameFeed implements AutoCloseable {
    static final int HEADER_BYTES = 64, MAX_METADATA = 4096, MAX_PIXELS = 4_194_304;
    static final long WRITE_TIMEOUT_NS = 500_000_000L, MAX_AGE_NS = 500_000_000L;
    final UUID id = UUID.randomUUID();
    private final ServerSocketChannel listener;
    private final Thread worker;
    private final ArrayDeque<byte[]> free = new ArrayDeque<>();
    private int leased;
    private Packet pending;
    private volatile SocketChannel connection;
    private volatile boolean closed, connected;
    private volatile String reason = "";
    private final AtomicLong sent = new AtomicLong(), dropped = new AtomicLong();
    record Packet(long sequence, long capturedNanos, byte[] metadata, byte[] pixels) { }

    public FrameFeed() throws IOException {
        listener = ServerSocketChannel.open();
        listener.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), 0), 1);
        listener.configureBlocking(false);
        worker = new Thread(this::run, "CounterCraft-FrameFeed");
        worker.setDaemon(true);
        worker.start();
    }
    public int port() throws IOException { return ((InetSocketAddress) listener.getLocalAddress()).getPort(); }
    public boolean ready() { return connected && !closed; }
    public boolean closed() { return closed; }
    public void drop() { dropped.incrementAndGet(); }

    /** Render thread never waits for the socket writer or allocates more than two live payloads. */
    public synchronized byte[] acquire(int bytes) {
        if (closed) return null;
        if (bytes < 8 || bytes > MAX_PIXELS * 8 || bytes % 8 != 0) throw new IllegalArgumentException("Payload size");
        if (leased == 2) {
            if (pending == null) return null;
            // The writer owns the other lease. Replace its waiting frame rather than preserve old pixels.
            recycle(pending.pixels()); pending = null; drop();
        }
        byte[] buffer = free.pollFirst();
        free.removeIf(other -> other.length != bytes);
        leased++;
        return buffer != null && buffer.length == bytes ? buffer : new byte[bytes];
    }
    public synchronized void recycle(byte[] pixels) {
        leased--;
        if (!closed) free.addLast(pixels);
    }
    public synchronized void offer(long sequence, long capturedNanos, JsonObject metadata, byte[] pixels) {
        byte[] json = metadata.toString().getBytes(StandardCharsets.UTF_8);
        if (closed || json.length > MAX_METADATA) { recycle(pixels); drop(); return; }
        if (pending != null) { recycle(pending.pixels()); drop(); }
        pending = new Packet(sequence, capturedNanos, json, pixels);
        notifyAll();
    }
    private synchronized Packet take() throws InterruptedException {
        while (!closed && pending == null) wait(100);
        Packet result = pending;
        pending = null;
        return result;
    }
    public JsonObject status() {
        JsonObject result = new JsonObject();
        result.addProperty("session", id.toString());
        result.addProperty("running", !closed);
        result.addProperty("connected", ready());
        result.addProperty("sent", sent.get());
        result.addProperty("dropped", dropped.get());
        result.addProperty("reason", reason);
        synchronized (this) { result.addProperty("leasedBuffers", leased); }
        return result;
    }
    static ByteBuffer header(UUID id, Packet packet) {
        CRC32 crc = new CRC32();
        crc.update(packet.metadata());
        crc.update(packet.pixels());
        ByteBuffer header = ByteBuffer.allocate(HEADER_BYTES).order(ByteOrder.LITTLE_ENDIAN);
        header.put("CCFRM001".getBytes(StandardCharsets.US_ASCII));
        header.putInt(1).putInt(HEADER_BYTES).putInt(packet.metadata().length);
        header.putInt(packet.pixels().length / 2).putInt(packet.pixels().length / 2).putInt(0);
        header.putLong(id.getMostSignificantBits()).putLong(id.getLeastSignificantBits());
        header.putLong(packet.sequence()).putInt((int) crc.getValue()).putInt(0);
        return header.flip();
    }
    private void write(SocketChannel channel, Selector selector, ByteBuffer buffer, long deadline) throws IOException {
        while (buffer.hasRemaining() && !closed) {
            if (System.nanoTime() >= deadline) throw new SocketTimeoutException("Frame write exceeded 500ms");
            if (channel.write(buffer) == 0) { selector.select(20); selector.selectedKeys().clear(); }
        }
        if (buffer.hasRemaining()) throw new IOException("Stream stopped");
    }
    private void run() {
        try (Selector selector = Selector.open()) {
            listener.register(selector, SelectionKey.OP_ACCEPT);
            long deadline = System.nanoTime() + 2_000_000_000L;
            while (!closed && connection == null) {
                connection = listener.accept();
                if (connection == null) {
                    if (System.nanoTime() > deadline) throw new SocketTimeoutException("No frame receiver in 2s");
                    selector.select(20); selector.selectedKeys().clear();
                }
            }
            listener.close();
            SocketChannel channel = connection;
            if (channel == null || closed) return;
            channel.configureBlocking(false);
            channel.setOption(StandardSocketOptions.TCP_NODELAY, true);
            channel.setOption(StandardSocketOptions.SO_SNDBUF, 65536);
            channel.register(selector, SelectionKey.OP_WRITE);
            connected = true;
            while (!closed) {
                Packet packet = take();
                if (packet == null) continue;
                try {
                    if (System.nanoTime() - packet.capturedNanos() > MAX_AGE_NS) { drop(); continue; }
                    long writeDeadline = System.nanoTime() + WRITE_TIMEOUT_NS;
                    write(channel, selector, header(id, packet), writeDeadline);
                    write(channel, selector, ByteBuffer.wrap(packet.metadata()), writeDeadline);
                    write(channel, selector, ByteBuffer.wrap(packet.pixels()), writeDeadline);
                    sent.incrementAndGet();
                } finally { recycle(packet.pixels()); }
            }
        } catch (IOException | InterruptedException failed) {
            if (!closed) reason = failed.getClass().getSimpleName() + ": " + failed.getMessage();
            if (failed instanceof InterruptedException) Thread.currentThread().interrupt();
        } finally { close(); }
    }
    public void fail(String message) { reason = message; close(); }
    @Override public synchronized void close() {
        closed = true; connected = false;
        if (pending != null) { recycle(pending.pixels()); pending = null; }
        free.clear();
        try { listener.close(); } catch (IOException ignored) { }
        SocketChannel channel = connection;
        if (channel != null) try { channel.close(); } catch (IOException ignored) { }
        notifyAll();
    }
}
