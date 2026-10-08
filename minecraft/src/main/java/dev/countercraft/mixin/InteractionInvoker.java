package dev.countercraft.mixin;
import net.minecraft.client.MinecraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(MinecraftClient.class)
public interface InteractionInvoker {
    @Invoker("doAttack") boolean countercraft$attack();
    @Invoker("doItemUse") void countercraft$use();
    @Invoker("doItemPick") void countercraft$pick();
    @Invoker("handleBlockBreaking") void countercraft$breaking(boolean active);
}
