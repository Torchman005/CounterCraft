package dev.countercraft;
import com.google.gson.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class RemoteControlTest {
    private static final long NOW = 2_000_000_000;
    private static BridgeState.World world(long epoch, boolean live, long now) { return new BridgeState.World(epoch,live,0,64,0,now); }
    private static JsonObject message(int id) {
        return JsonParser.parseString("{\"id\":"+id+",\"epoch\":7,\"yaw\":90,\"pitch\":30,\"forward\":1,\"sideways\":0,\"slot\":0,\"attack\":true}").getAsJsonObject();
    }
    @Test void latestExpiresOnReleasePauseWorldChangeAndTickStaleness() {
        var remote = new RemoteControl(); var w = world(7,true,NOW);
        var accepted = remote.accept(message(1),w,NOW);
        assertTrue(accepted.attack()); assertEquals(1,remote.live(w,NOW).id());
        assertNull(remote.live(w,NOW+RemoteControl.TIMEOUT_NS+1));
        assertNull(remote.live(world(8,true,NOW),NOW));
        assertNull(remote.live(world(7,false,NOW),NOW));
        assertNull(remote.live(world(7,true,NOW-BridgeState.TIMEOUT_NS-1),NOW));
        remote.release(); assertNull(remote.live(w,NOW));
        assertThrows(IllegalArgumentException.class,()->remote.accept(message(1),w,NOW));
        remote.reset(); assertNotNull(remote.accept(message(1),w,NOW));
    }
    @Test void validationAndRateBounds() {
        var remote = new RemoteControl(); var w = world(7,true,NOW);
        remote.accept(message(1),w,NOW);
        assertThrows(IllegalArgumentException.class,()->remote.accept(message(2),w,NOW+1));
        for (String key : new String[]{"yaw","pitch","forward","sideways","slot","mouseX","mouseY"}) {
            JsonObject m = message(2); m.addProperty(key, 1e100);
            assertThrows(IllegalArgumentException.class,()->remote.accept(m,w,NOW+20_000_000));
        }
        JsonObject m = message(2); m.addProperty("jump",1);
        assertThrows(IllegalArgumentException.class,()->remote.accept(m,w,NOW+20_000_000));
        JsonObject wrong = message(2); wrong.addProperty("epoch",8);
        assertThrows(IllegalArgumentException.class,()->remote.accept(wrong,w,NOW+20_000_000));
        for(long scroll:new long[]{Long.MIN_VALUE,Long.MAX_VALUE,-1_000_001,1_000_001}) {
            JsonObject invalid=message(2);invalid.addProperty("scroll",scroll);
            assertThrows(IllegalArgumentException.class,()->remote.accept(invalid,w,NOW+20_000_000));
        }
    }
    @Test void publishedPlayerSnapshotIsImmutableAcrossThreads() {
        var remote = new RemoteControl(); var source = new JsonObject(); source.addProperty("health",20);
        remote.publish(source); source.addProperty("health",0);
        var copy = remote.player(); copy.addProperty("health",1);
        assertEquals(20,remote.player().get("health").getAsInt());
    }
}
