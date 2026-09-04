package gg.vape.config;

import gg.vape.Vape;
import gg.vape.event.EventHandler;
import gg.vape.event.EventListener;
import gg.vape.event.impl.EventChat;
import gg.vape.runtime.NativeBridge;
import gg.vape.wrapper.impl.ITextComponent;

/**
 * Offline config commands for Goat Client.
 *
 * Type in chat:
 *   .goat save            - save current config to ~/.goat-client/config.json
 *   .goat load            - reload config from ~/.goat-client/config.json
 *   .goat export <path>   - export config to a JSON file
 *   .goat import <path>   - import config from a JSON file (then reload)
 *   .goat path            - print the active config path
 *
 * Commands are matched on incoming/echoed chat (the server echoes the player's
 * own messages), the same mechanism most chat-command clients use.
 */
public final class GoatConfigCommand implements EventListener {

    private static final String PREFIX = ".goat";

    private void info(String message) {
        Vape.INSTANCE.getNotificationManager().showInfo("Goat Client", message, 5000L);
    }

    private void warn(String message) {
        Vape.INSTANCE.getNotificationManager().show("Goat Client", message,
                gg.vape.notification.NotificationType.WARNING, 5000L);
    }

    @EventHandler
    public void onChat(EventChat event) {
        ITextComponent component = event.getMessage();
        if (component == null || component.isNull()) {
            return;
        }
        String text;
        try {
            text = component.getFormattedText();
        }
        catch (Throwable error) {
            return;
        }
        if (text == null) {
            return;
        }
        String line = text.trim();
        if (!line.startsWith(PREFIX)) {
            return;
        }
        String rest = line.substring(PREFIX.length()).trim();
        int space = rest.indexOf(' ');
        String command = (space < 0 ? rest : rest.substring(0, space)).toLowerCase();
        String arg = (space < 0 ? "" : rest.substring(space + 1).trim());

        switch (command) {
            case "save": {
                Vape.INSTANCE.getSyncThread().saveSettings();
                info("Config saved");
                break;
            }
            case "load": {
                Vape.INSTANCE.getSyncThread().loadConfig();
                info("Config reloaded");
                break;
            }
            case "export": {
                if (arg.isEmpty()) {
                    warn("Usage: .goat export <path>");
                    break;
                }
                if (NativeBridge.exportConfig(arg)) {
                    info("Exported config to " + arg);
                } else {
                    warn("Export failed (see log)");
                }
                break;
            }
            case "import": {
                if (arg.isEmpty()) {
                    warn("Usage: .goat import <path>");
                    break;
                }
                if (NativeBridge.importConfig(arg)) {
                    Vape.INSTANCE.getSyncThread().loadConfig();
                    info("Imported config from " + arg);
                } else {
                    warn("Import failed (file not found?)");
                }
                break;
            }
            case "path": {
                info("Config: ~/.goat-client/config.json");
                break;
            }
            default: {
                warn("Unknown .goat command. Try: save, load, export <path>, import <path>, path");
                break;
            }
        }
    }
}
