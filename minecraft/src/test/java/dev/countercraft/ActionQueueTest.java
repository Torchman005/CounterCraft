package dev.countercraft;

import com.google.gson.*;
import org.junit.jupiter.api.Test;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;

class ActionQueueTest {
    private static final long NOW = 2_000_000_000L;
    private static BridgeState.World world(long epoch, boolean offline, long now) {
        return new BridgeState.World(epoch, offline, 0, 64, 0, now);
    }
    private static ActionQueue.Action action(long id, long epoch) {
        return ActionQueue.parse(JsonParser.parseString("{\"id\":"+id+",\"epoch\":"+epoch+",\"action\":\"look\",\"yaw\":0,\"pitch\":0}").getAsJsonObject());
    }
    @Test void orderedAndOnlyExecutedOnTick() {
        var q = new ActionQueue(); var w = world(7, true, NOW);
        var first = q.submit(action(1, 7), w, NOW);
        var second = q.submit(action(2, 7), w, NOW + ActionQueue.MIN_INTERVAL_NS);
        assertFalse(first.isDone()); assertFalse(second.isDone());
        List<Long> ids = new ArrayList<>();
        q.tick(w, NOW + 50_000_000, a -> { ids.add(a.id()); return new JsonObject(); });
        assertEquals(List.of(1L, 2L), ids);
        assertEquals("action-ack", first.join().get("type").getAsString());
        assertEquals(2, second.join().get("id").getAsLong());
    }
    @Test void stalePausedAndChangedWorldNeverExecutes() {
        for (int mode = 0; mode < 3; mode++) {
            var q = new ActionQueue(); var f = q.submit(action(1, 7), world(7, true, NOW), NOW);
            long now = NOW + (mode == 0 ? ActionQueue.TTL_NS + 1 : 1);
            q.tick(world(mode == 1 ? 8 : 7, mode != 2, now), now, a -> { fail("Stale action executed"); return null; });
            assertTrue(f.isCompletedExceptionally());
        }
    }
    @Test void canceledAndDisconnectedRequestsNeverExecuteAndNewSessionResetsIds() {
        var q = new ActionQueue(); var w = world(7, true, NOW);
        var canceled = q.submit(action(1, 7), w, NOW); canceled.cancel(false);
        q.tick(w, NOW, a -> { fail("Canceled action executed"); return null; });
        var f = q.submit(action(2, 7), w, NOW + ActionQueue.MIN_INTERVAL_NS);
        q.reset(); assertTrue(f.isCompletedExceptionally());
        assertFalse(q.submit(action(0, 7), w, NOW).isDone());
    }
    @Test void admissionRejectsEpochReplayRateAndCapacity() {
        var q = new ActionQueue(); var w = world(7, true, NOW);
        assertThrows(IllegalArgumentException.class, () -> q.submit(action(1, 6), w, NOW));
        assertThrows(IllegalArgumentException.class, () -> q.submit(action(1, 7), world(7,false,NOW), NOW));
        q.submit(action(1, 7), w, NOW);
        assertThrows(IllegalArgumentException.class, () -> q.submit(action(1, 7), w, NOW + 30_000_000));
        assertThrows(IllegalArgumentException.class, () -> q.submit(action(2, 7), w, NOW + 1));
        for (int i = 2; i <= ActionQueue.CAPACITY; i++) {
            long now = NOW + i * ActionQueue.MIN_INTERVAL_NS;
            q.submit(action(i, 7), world(7,true,now), now);
        }
        long fullTime = NOW + 40 * ActionQueue.MIN_INTERVAL_NS;
        assertThrows(IllegalArgumentException.class, () -> q.submit(action(40,7), world(7,true,fullTime), fullTime));
    }
    @Test void parseRejectsInvalidBoundsAndTypes() {
        for (String body : List.of(
                "\"action\":\"unknown\"", "\"action\":\"look\",\"yaw\":0,\"pitch\":91",
                "\"action\":\"look\",\"yaw\":1e50,\"pitch\":0",
                "\"action\":\"select\",\"slot\":9", "\"action\":\"select\",\"slot\":0.5",
                "\"action\":\"inspect\",\"block\":[30000000,64,0]",
                "\"action\":\"inspect\",\"block\":[0,-2049,0]",
                "\"action\":\"inspect\",\"block\":[0.5,64,0]",
                "\"action\":\"break\",\"block\":[0,64,0],\"face\":\"bad\"",
                "\"action\":\"move\",\"delta\":[3,0,0]",
                "\"action\":\"move\",\"delta\":[\"0\",0,0]",
                "\"action\":\"click\",\"slot\":0,\"button\":2",
                "\"action\":\"creative\",\"slot\":9,\"item\":\"minecraft:stone\",\"count\":1",
                "\"action\":\"creative\",\"slot\":0,\"item\":\"bad id\",\"count\":1",
                "\"action\":\"creative\",\"slot\":0,\"item\":\"minecraft:stone\",\"count\":65")) {
            assertThrows(RuntimeException.class, () -> ActionQueue.parse(JsonParser.parseString("{\"id\":1,\"epoch\":7,"+body+"}").getAsJsonObject()), body);
        }
    }
    @Test void executionFailureDoesNotDropFollowingAction() {
        var q = new ActionQueue(); var w = world(7,true,NOW);
        var first = q.submit(action(1,7),w,NOW);
        var second = q.submit(action(2,7),w,NOW + 20_000_000);
        q.tick(w,NOW + 50_000_000,a -> { if (a.id()==1) throw new IllegalArgumentException("out of reach"); return new JsonObject(); });
        assertTrue(first.isCompletedExceptionally()); assertFalse(second.isCompletedExceptionally()); assertTrue(second.isDone());
    }
}
