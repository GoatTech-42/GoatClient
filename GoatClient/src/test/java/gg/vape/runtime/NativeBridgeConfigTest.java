package gg.vape.runtime;

import gg.vape.utils.Base64Util;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;
import static org.junit.Assert.*;

public class NativeBridgeConfigTest {
    private static Path home;
    private static Path config;
    private static final String ORIGINAL = "{\"friends\":[],\"profiles\":{},\"otherdata\":[]}";

    @BeforeClass
    public static void isolateHome() throws Exception {
        home = Files.createTempDirectory("goat-config-test-");
        config = home.resolve(".goat-client/config.json");
        String previous = System.getProperty("user.home");
        try {
            System.setProperty("user.home", home.toString());
            Class.forName("gg.vape.runtime.NativeBridge");
        } finally {
            System.setProperty("user.home", previous);
        }
    }

    @AfterClass
    public static void removeTemporaryFiles() throws Exception {
        try (java.util.stream.Stream<Path> files = Files.walk(home)) {
            Path[] paths = files.sorted(java.util.Comparator.reverseOrder()).toArray(Path[]::new);
            for (Path path : paths) {
                Files.deleteIfExists(path);
            }
        }
    }

    @Before
    public void resetConfig() throws Exception {
        Files.deleteIfExists(config);
        Field cache = NativeBridge.class.getDeclaredField("cachedConfigText");
        cache.setAccessible(true);
        cache.set(null, null);
        NativeBridge.sp("all", Base64Util.encodeUtf8Base64(ORIGINAL));
    }

    @Test
    public void saveDoesNotAppendZeroBytes() throws Exception {
        assertEquals(ORIGINAL, new String(Files.readAllBytes(config), StandardCharsets.UTF_8));
    }

    @Test
    public void invalidSavePreservesCurrentConfig() throws Exception {
        NativeBridge.sp("all", Base64Util.encodeUtf8Base64("{broken"));
        assertEquals(ORIGINAL, new String(Files.readAllBytes(config), StandardCharsets.UTF_8));
        assertEquals(ORIGINAL, Base64Util.decodeUtf8Base64(NativeBridge.gp("all")));
    }

    @Test
    public void importAndExportUtf8() throws Exception {
        String json = "{\"profiles\":{},\"name\":\"caf\u00e9\"}";
        Path source = home.resolve("source.json");
        Files.write(source, json.getBytes(StandardCharsets.UTF_8));
        assertTrue(NativeBridge.importConfig(source.toString()));
        Path exported = home.resolve("nested/export.json");
        assertTrue(NativeBridge.exportConfig(exported.toString()));
        assertEquals(json, new String(Files.readAllBytes(exported), StandardCharsets.UTF_8));
        assertEquals(json, Base64Util.decodeUtf8Base64(NativeBridge.gp("all")));
    }

    @Test
    public void exportToBareFilename() throws Exception {
        String filename = "goat-config-test-" + System.nanoTime() + ".json";
        Path output = java.nio.file.Paths.get(filename);
        try {
            assertTrue(NativeBridge.exportConfig(filename));
            assertEquals(ORIGINAL, new String(Files.readAllBytes(output), StandardCharsets.UTF_8));
        } finally {
            Files.deleteIfExists(output);
        }
    }

    @Test
    public void invalidImportPreservesCurrentConfig() throws Exception {
        Path source = home.resolve("invalid.json");
        for (String invalid : new String[]{"", "{broken", "[]", "null", "42",
                "{name:'abc'}", "{/* hi */\"a\":1}", "{\"a\":NaN}", "{} {}"}) {
            Files.write(source, invalid.getBytes(StandardCharsets.UTF_8));
            assertFalse("accepted invalid config: " + invalid, NativeBridge.importConfig(source.toString()));
            assertEquals(ORIGINAL, new String(Files.readAllBytes(config), StandardCharsets.UTF_8));
            assertEquals(ORIGINAL, Base64Util.decodeUtf8Base64(NativeBridge.gp("all")));
        }
    }

    @Test
    public void failedWriteIsNotReportedAsSuccessfulImport() throws Exception {
        Path source = home.resolve("valid.json");
        Files.write(source, "{\"profiles\":{}}".getBytes(StandardCharsets.UTF_8));
        Files.delete(config);
        Files.createDirectory(config);
        try {
            assertFalse(NativeBridge.importConfig(source.toString()));
            assertEquals(ORIGINAL, Base64Util.decodeUtf8Base64(NativeBridge.gp("all")));
            try (java.util.stream.Stream<Path> files = Files.list(config.getParent())) {
                assertEquals("temporary files leaked", 1L, files.count());
            }
        } finally {
            Files.delete(config);
        }
    }

    @Test
    public void missingImportPreservesCurrentConfig() {
        assertFalse(NativeBridge.importConfig(home.resolve("missing.json").toString()));
        assertEquals(ORIGINAL, Base64Util.decodeUtf8Base64(NativeBridge.gp("all")));
    }
}
