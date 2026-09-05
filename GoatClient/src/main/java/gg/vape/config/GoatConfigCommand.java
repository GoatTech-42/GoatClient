package gg.vape.config;

import gg.vape.Vape;
import gg.vape.event.EventHandler;
import gg.vape.event.EventListener;
import gg.vape.event.impl.EventChat;
import gg.vape.manager.client.ProfilesManager;
import gg.vape.runtime.NativeBridge;
import gg.vape.wrapper.impl.ITextComponent;

import java.util.List;

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
 *
 * Profile commands:
 *   .goat profile list                - list profiles (active marked with *)
 *   .goat profile create <name>       - create a fresh profile and switch to it
 *   .goat profile switch <name>       - switch to an existing profile
 *   .goat profile rename <name>       - rename the active profile
 *   .goat profile delete <name>       - delete a profile
 *   .goat profile duplicate <name>    - duplicate a profile as "<name> copy"
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
            case "profile": {
                handleProfile(arg);
                break;
            }
            default: {
                warn("Unknown .goat command. Try: save, load, export <path>, import <path>, profile <sub>, path");
                break;
            }
        }
    }

    private void handleProfile(String args) {
        ProfilesManager pm = Vape.INSTANCE.getProfilesManager();
        int space = args.indexOf(' ');
        String sub = (space < 0 ? args : args.substring(0, space)).toLowerCase();
        String name = (space < 0 ? "" : args.substring(space + 1).trim());

        try {
            switch (sub) {
                case "list": {
                    List<Profile> profiles = pm.getProfiles();
                    Profile active = pm.getActiveProfile();
                    if (profiles.isEmpty()) {
                        info("No profiles");
                        break;
                    }
                    StringBuilder sb = new StringBuilder();
                    for (int i = 0; i < profiles.size(); i++) {
                        Profile p = profiles.get(i);
                        if (i > 0) sb.append(", ");
                        sb.append(p.getName());
                        if (p == active) sb.append(" *");
                    }
                    info("Profiles: " + sb.toString());
                    break;
                }
                case "create": {
                    if (name.isEmpty()) {
                        warn("Usage: .goat profile create <name>");
                        break;
                    }
                    if (pm.getProfileByName(name) != null) {
                        warn("Profile already exists: " + name);
                        break;
                    }
                    Profile p = new Profile(name, "4.22");
                    pm.addProfile(p, true);
                    pm.switchProfile(p);
                    Vape.INSTANCE.getSyncThread().saveSettings();
                    info("Created profile: " + name);
                    break;
                }
                case "switch": {
                    if (name.isEmpty()) {
                        warn("Usage: .goat profile switch <name>");
                        break;
                    }
                    Profile p = pm.getProfileByName(name);
                    if (p == null) {
                        warn("Profile not found: " + name);
                        break;
                    }
                    pm.switchProfile(p);
                    info("Switched to profile: " + name);
                    break;
                }
                case "rename": {
                    if (name.isEmpty()) {
                        warn("Usage: .goat profile rename <newName>");
                        break;
                    }
                    Profile active = pm.getActiveProfile();
                    if (active == null) {
                        warn("No active profile");
                        break;
                    }
                    active.setName(name);
                    Vape.INSTANCE.getSyncThread().saveSettings();
                    info("Renamed active profile to: " + name);
                    break;
                }
                case "delete": {
                    if (name.isEmpty()) {
                        warn("Usage: .goat profile delete <name>");
                        break;
                    }
                    Profile p = pm.getProfileByName(name);
                    if (p == null) {
                        warn("Profile not found: " + name);
                        break;
                    }
                    boolean wasActive = p == pm.getActiveProfile();
                    List<Profile> remaining = new java.util.ArrayList<Profile>(pm.getProfiles());
                    remaining.remove(p);
                    if (wasActive && !remaining.isEmpty()) {
                        pm.switchProfile(remaining.get(0));
                    }
                    pm.removeProfile(p);
                    Vape.INSTANCE.getSyncThread().saveSettings();
                    info("Deleted profile: " + name);
                    break;
                }
                case "duplicate": {
                    if (name.isEmpty()) {
                        warn("Usage: .goat profile duplicate <name>");
                        break;
                    }
                    Profile src = pm.getProfileByName(name);
                    if (src == null) {
                        warn("Profile not found: " + name);
                        break;
                    }
                    String copyName = name + " copy";
                    Profile copy = new Profile(copyName, "4.22");
                    com.google.gson.JsonObject srcJson = src.toJson(false);
                    srcJson.remove("uuid");       // keep a fresh localId (avoid key collision)
                    srcJson.remove("profileId");  // offline: no online id
                    copy.loadJson(srcJson);
                    copy.setName(copyName);
                    pm.addProfile(copy, true);
                    Vape.INSTANCE.getSyncThread().saveSettings();
                    info("Duplicated profile as: " + copyName);
                    break;
                }
                default: {
                    warn("Unknown profile command. Try: list, create, switch, rename, delete, duplicate");
                    break;
                }
            }
        }
        catch (Throwable error) {
            warn("Profile command failed: " + error.getClass().getSimpleName());
        }
    }
}
