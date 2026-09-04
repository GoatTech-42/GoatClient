#include "vape421_native.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * This DLL is intentionally payload-free.  It is a short-lived diagnostic
 * agent for clients which add their own class transformers (notably Lunar's
 * OptiFine distribution).  Every ClassFileLoadHook callback is written so the
 * transformer ordering can be reconstructed from the resulting directory.
 */

static JavaVM *g_dump_vm;
static jvmtiEnv *g_dump_jvmti;
static HMODULE g_dump_module;
static SRWLOCK g_dump_lock = SRWLOCK_INIT;
static jclass g_dump_target;
static unsigned long g_dump_serial;
static unsigned int g_dump_hook_serial;
static wchar_t g_dump_directory[MAX_PATH];
static wchar_t g_dump_log_path[MAX_PATH];

static void dump_log(const wchar_t *format, ...) {
    wchar_t message[2048];
    wchar_t line[2304];
    SYSTEMTIME now;
    FILE *file = NULL;
    va_list arguments;

    va_start(arguments, format);
    _vsnwprintf_s(message, sizeof(message) / sizeof(message[0]),
            _TRUNCATE, format, arguments);
    va_end(arguments);
    GetLocalTime(&now);
    _snwprintf_s(line, sizeof(line) / sizeof(line[0]), _TRUNCATE,
            L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %ls\r\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
            now.wSecond, now.wMilliseconds, message);
    OutputDebugStringW(line);
    if (g_dump_log_path[0] == L'\0') {
        return;
    }
    if (_wfopen_s(&file, g_dump_log_path, L"a, ccs=UTF-8") == 0
            && file != NULL) {
        fputws(line, file);
        fclose(file);
    }
}

static int make_dump_directory(void) {
    wchar_t temp_root[MAX_PATH];
    DWORD length;
    if (GetTempPathW((DWORD)(sizeof(temp_root) / sizeof(temp_root[0])),
            temp_root) == 0) {
        return 0;
    }
    length = GetTempPathW((DWORD)(sizeof(temp_root) / sizeof(temp_root[0])),
            temp_root);
    if (length == 0 || length >= sizeof(temp_root) / sizeof(temp_root[0])) {
        return 0;
    }
    _snwprintf_s(g_dump_directory,
            sizeof(g_dump_directory) / sizeof(g_dump_directory[0]),
            _TRUNCATE, L"%lsVape421Recovery\\lunar-%lu-dump",
            temp_root, GetCurrentProcessId());
    if (!CreateDirectoryW(g_dump_directory, NULL)
            && GetLastError() != ERROR_ALREADY_EXISTS) {
        return 0;
    }
    _snwprintf_s(g_dump_log_path,
            sizeof(g_dump_log_path) / sizeof(g_dump_log_path[0]),
            _TRUNCATE, L"%ls\\dump.log", g_dump_directory);
    return 1;
}

static int dump_filter(const char *signature) {
    size_t length;
    if (signature == NULL || signature[0] != 'L') {
        return 0;
    }
    if (strncmp(signature, "Lnet/minecraft/", 15) == 0
            || strncmp(signature, "Lnet/optifine/", 14) == 0
            || strncmp(signature, "Loptifine/", 10) == 0
            || strncmp(signature, "Lshadersmod/", 12) == 0
            || strncmp(signature, "Lcom/moonsworth/", 16) == 0
            || strncmp(signature, "Lcom/lunarclient/", 17) == 0) {
        return 1;
    }

    /* Legacy 1.8.9 game classes are usually one to four characters long. */
    length = strlen(signature);
    if (strchr(signature, '/') == NULL && length >= 3 && length <= 10) {
        return 1;
    }
    return 0;
}

static void signature_file_part(const char *signature,
        wchar_t *output, size_t capacity) {
    size_t input_index = 0;
    size_t output_index = 0;
    if (output == NULL || capacity == 0) {
        return;
    }
    while (signature != NULL && signature[input_index] != '\0'
            && output_index + 1 < capacity) {
        unsigned char character = (unsigned char)signature[input_index++];
        if (character == 'L' || character == ';') {
            continue;
        }
        if (character == '/' || character == '\\' || character == ':') {
            character = '_';
        }
        output[output_index++] = (wchar_t)character;
    }
    output[output_index] = L'\0';
}

static int write_bytes(const wchar_t *path,
        const unsigned char *bytes, jint length) {
    HANDLE file;
    DWORD written;
    if (path == NULL || bytes == NULL || length <= 0) {
        return 0;
    }
    file = CreateFileW(path, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    written = 0;
    if (!WriteFile(file, bytes, (DWORD)length, &written, NULL)
            || written != (DWORD)length) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    return 1;
}

static void dump_hook_bytes(JNIEnv *env, jclass target,
        const char *name, const unsigned char *bytes, jint length) {
    char *signature = NULL;
    wchar_t class_part[512];
    wchar_t path[MAX_PATH];
    unsigned long serial;
    unsigned int hook_serial;
    jvmtiError error;

    if (env == NULL || target == NULL || bytes == NULL || length <= 0
            || g_dump_jvmti == NULL || g_dump_directory[0] == L'\0'
            || g_dump_target == NULL
            || !(*env)->IsSameObject(env, target, g_dump_target)) {
        return;
    }
    error = (*g_dump_jvmti)->GetClassSignature(g_dump_jvmti, target,
            &signature, NULL);
    if (error != JVMTI_ERROR_NONE || signature == NULL) {
        return;
    }
    signature_file_part(signature, class_part,
            sizeof(class_part) / sizeof(class_part[0]));
    serial = g_dump_serial;
    hook_serial = ++g_dump_hook_serial;
    _snwprintf_s(path, sizeof(path) / sizeof(path[0]), _TRUNCATE,
            L"%ls\\%06lu-hook-%03u-%ls.class", g_dump_directory,
            serial, hook_serial, class_part);
    if (!write_bytes(path, bytes, length)) {
        dump_log(L"write failed for %hs (%d bytes)", signature, length);
    }
    dump_log(L"class=%hs hook=%u callbackName=%hs bytes=%d file=%ls",
            signature, hook_serial, name == NULL ? "<null>" : name,
            length, path);
    (*g_dump_jvmti)->Deallocate(g_dump_jvmti, (unsigned char *)signature);
}

static void JNICALL dump_class_file_load_hook(
        jvmtiEnv *jvmti_env,
        JNIEnv *env,
        jclass class_being_redefined,
        jobject loader,
        const char *name,
        jobject protection_domain,
        jint class_data_len,
        const unsigned char *class_data,
        jint *new_class_data_len,
        unsigned char **new_class_data) {
    (void)jvmti_env;
    (void)loader;
    (void)protection_domain;
    if (new_class_data_len != NULL) {
        *new_class_data_len = 0;
    }
    if (new_class_data != NULL) {
        *new_class_data = NULL;
    }
    if (class_being_redefined == NULL || class_data == NULL
            || class_data_len <= 0) {
        return;
    }
    dump_hook_bytes(env, class_being_redefined, name,
            class_data, class_data_len);
}

static int get_vm_and_jvmti(void) {
    HMODULE jvm_module;
    FARPROC created_vms_address;
    typedef jint (JNICALL *get_created_vms_fn)(JavaVM **, jsize, jsize *);
    get_created_vms_fn get_created_vms;
    JNIEnv *env = NULL;
    jsize vm_count = 0;
    int attempt;
    jvmtiCapabilities potential;
    jvmtiCapabilities requested;
    jvmtiEventCallbacks callbacks;
    jvmtiError error;

    for (attempt = 0; attempt < 600; ++attempt) {
        jvm_module = GetModuleHandleW(L"jvm.dll");
        if (jvm_module != NULL) {
            break;
        }
        Sleep(100);
    }
    if (jvm_module == NULL) {
        dump_log(L"jvm.dll was not found");
        return 0;
    }
    created_vms_address = GetProcAddress(jvm_module, "JNI_GetCreatedJavaVMs");
    if (created_vms_address == NULL) {
        dump_log(L"JNI_GetCreatedJavaVMs is unavailable");
        return 0;
    }
    get_created_vms = (get_created_vms_fn)created_vms_address;
    for (attempt = 0; attempt < 600; ++attempt) {
        if (get_created_vms(&g_dump_vm, 1, &vm_count) == JNI_OK
                && g_dump_vm != NULL && vm_count > 0) {
            break;
        }
        g_dump_vm = NULL;
        vm_count = 0;
        Sleep(100);
    }
    if (g_dump_vm == NULL) {
        dump_log(L"JNI_GetCreatedJavaVMs returned no VM");
        return 0;
    }
    if ((*g_dump_vm)->AttachCurrentThreadAsDaemon(
            g_dump_vm, (void **)&env, NULL) != JNI_OK || env == NULL) {
        dump_log(L"AttachCurrentThreadAsDaemon failed");
        return 0;
    }
    if ((*g_dump_vm)->GetEnv(g_dump_vm, (void **)&g_dump_jvmti,
            JVMTI_VERSION_1_2) != JNI_OK || g_dump_jvmti == NULL) {
        dump_log(L"JVMTI 1.2 is unavailable");
        return 0;
    }
    memset(&potential, 0, sizeof(potential));
    memset(&requested, 0, sizeof(requested));
    error = (*g_dump_jvmti)->GetPotentialCapabilities(
            g_dump_jvmti, &potential);
    if (error != JVMTI_ERROR_NONE) {
        dump_log(L"GetPotentialCapabilities failed: %d", error);
        return 0;
    }
    requested.can_retransform_classes = potential.can_retransform_classes;
    requested.can_retransform_any_class = potential.can_retransform_any_class;
    requested.can_get_source_file_name = potential.can_get_source_file_name;
    error = (*g_dump_jvmti)->AddCapabilities(g_dump_jvmti, &requested);
    if (error != JVMTI_ERROR_NONE) {
        dump_log(L"AddCapabilities failed: %d", error);
        return 0;
    }
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.ClassFileLoadHook = dump_class_file_load_hook;
    error = (*g_dump_jvmti)->SetEventCallbacks(
            g_dump_jvmti, &callbacks, sizeof(callbacks));
    if (error != JVMTI_ERROR_NONE) {
        dump_log(L"SetEventCallbacks failed: %d", error);
        return 0;
    }
    error = (*g_dump_jvmti)->SetEventNotificationMode(g_dump_jvmti,
            JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
    if (error != JVMTI_ERROR_NONE) {
        dump_log(L"ClassFileLoadHook enable failed: %d", error);
        return 0;
    }
    return 1;
}

static int dump_loaded_classes(JNIEnv *env) {
    jint class_count = 0;
    jclass *classes = NULL;
    jvmtiError error;
    jint index;
    unsigned int selected = 0;

    error = (*g_dump_jvmti)->GetLoadedClasses(
            g_dump_jvmti, &class_count, &classes);
    if (error != JVMTI_ERROR_NONE || classes == NULL) {
        dump_log(L"GetLoadedClasses failed: %d", error);
        return 0;
    }
    dump_log(L"loaded class count=%d", class_count);
    for (index = 0; index < class_count; ++index) {
        char *signature = NULL;
        jclass target = classes[index];
        jint status = 0;
        if ((*g_dump_jvmti)->GetClassSignature(g_dump_jvmti, target,
                &signature, NULL) != JVMTI_ERROR_NONE || signature == NULL) {
            continue;
        }
        if (!dump_filter(signature)
                || (*g_dump_jvmti)->GetClassStatus(
                        g_dump_jvmti, target, &status) != JVMTI_ERROR_NONE) {
            (*g_dump_jvmti)->Deallocate(g_dump_jvmti,
                    (unsigned char *)signature);
            continue;
        }
        ++selected;
        AcquireSRWLockExclusive(&g_dump_lock);
        g_dump_target = (jclass)(*env)->NewGlobalRef(env, target);
        g_dump_serial = selected;
        g_dump_hook_serial = 0;
        if (g_dump_target == NULL) {
            ReleaseSRWLockExclusive(&g_dump_lock);
            (*g_dump_jvmti)->Deallocate(g_dump_jvmti,
                    (unsigned char *)signature);
            continue;
        }
        dump_log(L"retransform begin class=%hs status=0x%x", signature, status);
        error = (*g_dump_jvmti)->RetransformClasses(
                g_dump_jvmti, 1, &target);
        if (error != JVMTI_ERROR_NONE) {
            dump_log(L"retransform failed class=%hs jvmti=%d",
                    signature, error);
        }
        dump_log(L"retransform end class=%hs hooks=%u", signature,
                g_dump_hook_serial);
        (*env)->DeleteGlobalRef(env, g_dump_target);
        g_dump_target = NULL;
        ReleaseSRWLockExclusive(&g_dump_lock);
        (*g_dump_jvmti)->Deallocate(g_dump_jvmti,
                (unsigned char *)signature);
    }
    (*g_dump_jvmti)->Deallocate(g_dump_jvmti, (unsigned char *)classes);
    dump_log(L"selected classes=%u", selected);
    return 1;
}

static DWORD WINAPI dump_thread(LPVOID parameter) {
    JNIEnv *env = NULL;
    (void)parameter;
    if (!make_dump_directory()) {
        return 1;
    }
    dump_log(L"Lunar/OptiFine JVMTI dump started for PID %lu",
            GetCurrentProcessId());
    if (!get_vm_and_jvmti()) {
        return 2;
    }
    if ((*g_dump_vm)->GetEnv(g_dump_vm, (void **)&env,
            JNI_VERSION_1_8) != JNI_OK || env == NULL) {
        dump_log(L"unable to resolve attached JNIEnv");
        return 3;
    }
    dump_loaded_classes(env);
    dump_log(L"dump complete; files are in %ls", g_dump_directory);
    (*g_dump_vm)->DetachCurrentThread(g_dump_vm);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    HANDLE thread;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_dump_module = instance;
        DisableThreadLibraryCalls(instance);
        thread = CreateThread(NULL, 0, dump_thread, instance, 0, NULL);
        if (thread != NULL) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
