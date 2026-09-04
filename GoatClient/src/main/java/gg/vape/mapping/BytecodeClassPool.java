package gg.vape.mapping;

import gg.vape.mapping.MappingClassBytecodeResolver;
import javassist.ByteArrayClassPath;
import javassist.ClassPool;
import javassist.CtClass;
import javassist.NotFoundException;
import javassist.bytecode.Descriptor;
import java.util.HashSet;
import java.util.Set;

public class BytecodeClassPool
extends ClassPool {
    private final MappingClassBytecodeResolver W;
    private final Set<String> runtimeClassPaths = new HashSet<String>();

    private static Exception a(Exception exception) {
        return exception;
    }

    @Override
    public synchronized CtClass get(String string) throws NotFoundException {
        this.ensureRuntimeClassPath(string);
        try {
            return super.get(string);
        }
        catch (NotFoundException notFoundException) {
            if (this.N(string)) {
                return super.get(string);
            }
            throw notFoundException;
        }
    }

    public BytecodeClassPool(MappingClassBytecodeResolver mappingClassBytecodeResolver) {
        super(true);
        this.W = mappingClassBytecodeResolver;
    }

    @Override
    public synchronized CtClass getOrNull(String string) {
        this.ensureRuntimeClassPath(string);
        CtClass ctClass = super.getOrNull(string);
        if (ctClass == null && this.N(string)) {
            ctClass = super.getOrNull(string);
        }
        return ctClass;
    }

    private void ensureRuntimeClassPath(String requestedName) {
        String className = normalizeClassName(requestedName);
        if (!className.startsWith("net.minecraft.")
                || !this.runtimeClassPaths.add(className)) {
            return;
        }
        byte[] bytecode = this.W.y(className);
        if (bytecode == null) {
            this.runtimeClassPaths.remove(className);
            return;
        }
        this.removeCached(className);
        this.insertClassPath(new ByteArrayClassPath(className, bytecode));
    }

    private boolean N(String string) {
        String className = normalizeClassName(string);
        byte[] byArray = this.W.y(className);
        if (byArray != null) {
            this.insertClassPath(new ByteArrayClassPath(className, byArray));
            return true;
        }
        return false;
    }

    private static String normalizeClassName(String requestedName) {
        String className = requestedName;
        if (className.startsWith("[")) {
            className = Descriptor.toClassName(className);
        }
        while (className.endsWith("[]")) {
            className = className.substring(0, className.length() - 2);
        }
        return className;
    }
}
